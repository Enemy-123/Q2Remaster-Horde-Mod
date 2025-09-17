import os
import sys
import shutil
import subprocess
import multiprocessing

def run_command(command, cwd=None, env=None):
    """Runs a command with a custom environment and exits if it fails."""
    print(f"Executing: {' '.join(command)}")
    try:
        result = subprocess.run(command, cwd=cwd, check=True, text=True, capture_output=True, env=env)
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)
    except subprocess.CalledProcessError as e:
        print(f"--- ERROR ---")
        print(f"Command failed with exit code {e.returncode}")
        print(f"STDOUT:\n{e.stdout}")
        print(f"STDERR:\n{e.stderr}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"Error: Command '{command[0]}' not found. Is it in your PATH?")
        sys.exit(1)

def find_mingw_runtime_path():
    """Finds the directory containing the MinGW runtime DLLs."""
    mingw_bin_path = "/usr/x86_64-w64-mingw32/bin"
    if os.path.isdir(mingw_bin_path):
        return mingw_bin_path
    return None

def main():
    # --- Configuration ---
    script_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(script_dir, "build")
    vcpkg_installed_dir = os.path.join(script_dir, "vcpkg_installed")
    # This file is now configured to use Clang
    clang_toolchain_file = os.path.join(script_dir, "mingw-w64-x86_64.cmake")
    vcpkg_toolchain_file = os.path.join(script_dir, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake")

    # --- Get Arguments ---
    if len(sys.argv) < 3:
        print("Usage: python3 build.py <deploy_path> <build_type>")
        sys.exit(1)

    deploy_path = sys.argv[1]
    build_type = sys.argv[2]
    print(f"--- Build Type set to: {build_type} ---")

    # --- Validate Paths ---
    for f in [clang_toolchain_file, vcpkg_toolchain_file]:
        if not os.path.isfile(f):
            print(f"Error: Toolchain file not found at '{f}'")
            sys.exit(1)

    if not os.path.isdir(deploy_path):
        print(f"Warning: Deployment directory does not exist: '{deploy_path}'. Creating it...")
        os.makedirs(deploy_path, exist_ok=True)

    # --- FAKE POWERSHELL WORKAROUND for VCPKG ---
    fake_bin_dir = os.path.join(script_dir, "fake_bin")
    os.makedirs(fake_bin_dir, exist_ok=True)
    powershell_path = os.path.join(fake_bin_dir, "powershell.exe")
    with open(powershell_path, "w") as f:
        f.write("#!/bin/bash\n")
        f.write("exit 0\n")
    os.chmod(powershell_path, 0o755)

    build_env = os.environ.copy()
    build_env["PATH"] = f"{fake_bin_dir}{os.pathsep}{build_env['PATH']}"
    # --- END WORKAROUND ---

    # --- Clean and Configure ---
    print("--- Starting Clang MinGW Cross-Compile Build ---")
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir)

    # Clang does not use -static-libgcc or -static-libstdc++.
    # The vcpkg static triplet handles this correctly. We still need to link pthreads.
    hybrid_linker_flags = "-lpthread"

    cmake_configure_command = [
        "cmake",
        "..",
        "-G", "Unix Makefiles",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DDEPLOY_DIRECTORY={deploy_path}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain_file}",
        f"-DVCPKG_INSTALLED_DIR={vcpkg_installed_dir}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={clang_toolchain_file}",
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-static",
        f"-DCMAKE_SHARED_LINKER_FLAGS={hybrid_linker_flags}",
    ]
    run_command(cmake_configure_command, cwd=build_dir, env=build_env)

    # --- Build and Install ---
    cpu_count = multiprocessing.cpu_count()
    print(f"--- Building with {cpu_count} jobs ---")
    run_command(["cmake", "--build", ".", "--", f"-j{cpu_count}"], cwd=build_dir, env=build_env)
    run_command(["cmake", "--build", ".", "--target", "install"], cwd=build_dir, env=build_env)

    final_dll_path = os.path.join(deploy_path, "game_x64.dll")
    if not os.path.isfile(final_dll_path):
        print(f"Error: Expected DLL not found at '{final_dll_path}' after installation.")
        sys.exit(1)
    
    # --- Copy the one remaining required DLL ---
    print("\n--- Handling Final Runtime Dependency ---")
    
    # Get the parent directory of deploy_path (e.g., 'rerelease' instead of 'rerelease/baseq2')
    game_executable_dir = os.path.dirname(os.path.normpath(deploy_path))

    pthread_dll = "libwinpthread-1.dll"
    toolchain_lib_dir = find_mingw_runtime_path()

    if toolchain_lib_dir:
        source_path = os.path.join(toolchain_lib_dir, pthread_dll)
        if os.path.isfile(source_path):
            print(f"Found '{pthread_dll}', copying to game executable directory: '{game_executable_dir}'")
            shutil.copy(source_path, game_executable_dir)
        else:
            print(f"!!! CRITICAL WARNING: Could not find required runtime DLL: '{pthread_dll}'")
    else:
        print("!!! CRITICAL ERROR: Could not find the toolchain runtime library directory.")

    print("\n--- BUILD SUCCESSFUL ---")
    print(f"Mostly static DLL successfully installed to: '{final_dll_path}'")
    print(f"Requires one additional DLL: '{pthread_dll}' in the game's root folder.")
    
    # --- Cleanup ---
    shutil.rmtree(fake_bin_dir)

if __name__ == "__main__":
    main()