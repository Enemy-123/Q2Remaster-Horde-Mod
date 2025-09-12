import os
import sys
import shutil
import subprocess
import multiprocessing

def run_command(command, cwd=None):
    """Runs a command and exits if it fails."""
    print(f"Executing: {' '.join(command)}")
    try:
        # Using capture_output=True to hide verbose output unless there's an error
        result = subprocess.run(command, cwd=cwd, check=True, text=True, capture_output=True)
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
    try:
        # Ask the compiler where it is, then navigate to the lib dir
        compiler_path = shutil.which("x86_64-w64-mingw32-gcc")
        if not compiler_path:
            return None
        # The path is usually /usr/bin/..., we want /usr/x86_64-w64-mingw32/lib
        compiler_dir = os.path.dirname(compiler_path)
        # Go up from /bin to /
        root_path = os.path.dirname(compiler_dir)
        lib_path = os.path.join(root_path, "x86_64-w64-mingw32", "lib")
        if os.path.isdir(lib_path):
            return lib_path
        # Fallback for other possible structures
        result = subprocess.run(
            ["find", "/usr/lib/gcc/x86_64-w64-mingw32", "-name", "libgcc_s_seh-1.dll"],
            capture_output=True, text=True, check=True
        )
        if result.stdout:
            return os.path.dirname(result.stdout.strip().split('\n')[0])
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return None


def main():
    # --- Configuration ---
    script_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(script_dir, "build")
    mingw_toolchain_file = os.path.join(script_dir, "mingw-w64-x86_64.cmake")
    vcpkg_toolchain_file = os.path.join(script_dir, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake")

    # --- Get Arguments ---
    if len(sys.argv) < 3:
        print("Usage: python3 build.py <deploy_path> <build_type>")
        sys.exit(1)

    deploy_path = sys.argv[1]
    build_type = sys.argv[2]
    print(f"--- Build Type set to: {build_type} ---")

    # --- Validate Paths ---
    for f in [mingw_toolchain_file, vcpkg_toolchain_file]:
        if not os.path.isfile(f):
            print(f"Error: Toolchain file not found at '{f}'")
            sys.exit(1)

    if not os.path.isdir(deploy_path):
        print(f"Warning: Deployment directory does not exist: '{deploy_path}'. Creating it...")
        os.makedirs(deploy_path, exist_ok=True)

    # --- Clean and Configure ---
    print("--- Starting GCC MinGW Cross-Compile Build ---")
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir)

    cmake_configure_command = [
        "cmake",
        "..",
        "-G", "Unix Makefiles",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DDEPLOY_DIRECTORY={deploy_path}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain_file}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={mingw_toolchain_file}",
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-static"
    ]
    run_command(cmake_configure_command, cwd=build_dir)

    # --- Build and Install ---
    cpu_count = multiprocessing.cpu_count()
    print(f"--- Building with {cpu_count} jobs ---")
    run_command(["cmake", "--build", ".", "--", f"-j{cpu_count}"], cwd=build_dir)
    run_command(["cmake", "--build", ".", "--target", "install"], cwd=build_dir)

    final_dll_path = os.path.join(deploy_path, "game_x64.dll")
    if not os.path.isfile(final_dll_path):
        print(f"Error: Expected DLL not found at '{final_dll_path}' after installation.")
        sys.exit(1)
    print(f"Output DLL successfully installed to: '{final_dll_path}'")

    # --- Handle GCC Runtime Dependencies ---
    print("\n--- Handling GCC Runtime Dependencies ---")
    game_root_dir = os.path.dirname(deploy_path)
    required_dlls = ["libgcc_s_seh-1.dll", "libstdc++-6.dll"]
    gcc_lib_dir = find_mingw_runtime_path()

    if not gcc_lib_dir:
        print("!!! CRITICAL ERROR: Could not find the GCC MinGW runtime library directory.")
        sys.exit(1)

    for dll_name in required_dlls:
        source_path = os.path.join(gcc_lib_dir, dll_name)
        if os.path.isfile(source_path):
            print(f"Found '{dll_name}', copying to game root directory...")
            shutil.copy(source_path, game_root_dir)
        else:
            print(f"!!! CRITICAL WARNING: Could not find required runtime DLL: '{dll_name}'")
            print(f"Searched in: '{gcc_lib_dir}'")
            sys.exit(1)

    print("All required runtime DLLs copied successfully.")

if __name__ == "__main__":
    main()