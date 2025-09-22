#!/usr/bin/env python3
"""
Universal build script for Q2 Horde Mod
Supports both MinGW and MSVC-compatible (clang-cl) compilation
"""

import os
import sys
import shutil
import subprocess
import multiprocessing
import argparse

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

def check_clang_msvc_support():
    """Check if clang supports MSVC target."""
    try:
        result = subprocess.run(
            ['clang++', '-target', 'x86_64-pc-windows-msvc', '--version'],
            check=True, capture_output=True, text=True
        )
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def check_mingw_support():
    """Check if MinGW cross-compiler is available."""
    try:
        result = subprocess.run(
            ['/usr/bin/x86_64-w64-mingw32-g++', '--version'],
            check=True, capture_output=True, text=True
        )
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def setup_fake_powershell(script_dir):
    """Create fake PowerShell for vcpkg workaround."""
    fake_bin_dir = os.path.join(script_dir, "fake_bin")
    os.makedirs(fake_bin_dir, exist_ok=True)
    powershell_path = os.path.join(fake_bin_dir, "powershell.exe")
    with open(powershell_path, "w") as f:
        f.write("#!/bin/bash\n")
        f.write("exit 0\n")
    os.chmod(powershell_path, 0o755)
    return fake_bin_dir

def build_with_clang_msvc(script_dir, deploy_path, build_type):
    """Build using Clang with MSVC ABI compatibility."""
    print("=== Building with Clang (MSVC ABI) ===")

    build_dir = os.path.join(script_dir, "build_msvc")
    vcpkg_installed_dir = os.path.join(script_dir, "vcpkg_installed")
    clang_toolchain_file = os.path.join(script_dir, "clang-msvc-x86_64.cmake")
    vcpkg_toolchain_file = os.path.join(script_dir, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake")
    cmake_file = os.path.join(script_dir, "CMakeLists_msvc.txt")

    # Copy MSVC-optimized CMakeLists.txt
    shutil.copy(cmake_file, os.path.join(script_dir, "CMakeLists.txt"))

    fake_bin_dir = setup_fake_powershell(script_dir)
    build_env = os.environ.copy()
    build_env["PATH"] = f"{fake_bin_dir}{os.pathsep}{build_env['PATH']}"

    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir)

    cmake_configure_command = [
        "cmake", "..",
        "-G", "Unix Makefiles",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DDEPLOY_DIRECTORY={deploy_path}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain_file}",
        f"-DVCPKG_INSTALLED_DIR={vcpkg_installed_dir}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={clang_toolchain_file}",
        "-DVCPKG_TARGET_TRIPLET=x64-windows-clang",  # Use our custom triplet
        f"-DVCPKG_OVERLAY_TRIPLETS={script_dir}/vcpkg/triplets",  # Point to our custom triplets
    ]

    run_command(cmake_configure_command, cwd=build_dir, env=build_env)

    cpu_count = multiprocessing.cpu_count()
    run_command(["cmake", "--build", ".", "--", f"-j{cpu_count}"], cwd=build_dir, env=build_env)
    run_command(["cmake", "--build", ".", "--target", "install"], cwd=build_dir, env=build_env)

    shutil.rmtree(fake_bin_dir)
    return os.path.join(deploy_path, "game_x64.dll")

def build_with_mingw(script_dir, deploy_path, build_type):
    """Build using MinGW with improved compatibility flags."""
    print("=== Building with Enhanced MinGW ===")

    build_dir = os.path.join(script_dir, "build_mingw")
    vcpkg_installed_dir = os.path.join(script_dir, "vcpkg_installed")
    mingw_toolchain_file = os.path.join(script_dir, "mingw-w64-x86_64.cmake")
    vcpkg_toolchain_file = os.path.join(script_dir, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake")

    fake_bin_dir = setup_fake_powershell(script_dir)
    build_env = os.environ.copy()
    build_env["PATH"] = f"{fake_bin_dir}{os.pathsep}{build_env['PATH']}"
    # Set jsoncpp_DIR to help cmake find the package
    build_env["jsoncpp_DIR"] = os.path.join(vcpkg_installed_dir, "x64-mingw-static", "share", "jsoncpp")

    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir)

    # Enhanced linker flags for better Windows compatibility
    enhanced_linker_flags = "-static-libgcc -static-libstdc++ -lpthread"
    enhanced_cxx_flags = "-mthreads -fexceptions -DWINVER=0x0601 -D_WIN32_WINNT=0x0601"

    cmake_configure_command = [
        "cmake", "..",
        "-G", "Unix Makefiles",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DDEPLOY_DIRECTORY={deploy_path}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain_file}",
        f"-DVCPKG_INSTALLED_DIR={vcpkg_installed_dir}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={mingw_toolchain_file}",
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-static",
        f"-DCMAKE_SHARED_LINKER_FLAGS={enhanced_linker_flags}",
        f"-DCMAKE_CXX_FLAGS={enhanced_cxx_flags}"
    ]

    run_command(cmake_configure_command, cwd=build_dir, env=build_env)

    cpu_count = multiprocessing.cpu_count()
    run_command(["cmake", "--build", ".", "--", f"-j{cpu_count}"], cwd=build_dir, env=build_env)
    run_command(["cmake", "--build", ".", "--target", "install"], cwd=build_dir, env=build_env)

    # Handle MinGW runtime dependencies
    mingw_runtime_path = "/usr/x86_64-w64-mingw32/bin"
    if os.path.isdir(mingw_runtime_path):
        pthread_dll = "libwinpthread-1.dll"
        source_path = os.path.join(mingw_runtime_path, pthread_dll)
        if os.path.isfile(source_path):
            game_dir = os.path.dirname(os.path.normpath(deploy_path))
            print(f"Copying {pthread_dll} to {game_dir}")
            shutil.copy(source_path, game_dir)

    shutil.rmtree(fake_bin_dir)
    return os.path.join(deploy_path, "game_x64.dll")

def main():
    parser = argparse.ArgumentParser(description='Build Q2 Horde Mod with different compilers')
    parser.add_argument('deploy_path', help='Directory where game_x64.dll should be installed')
    parser.add_argument('build_type', choices=['Debug', 'Release', 'RelWithDebInfo'],
                       help='CMake build type')
    parser.add_argument('--compiler', choices=['auto', 'clang-msvc', 'mingw'], default='auto',
                       help='Compiler to use (default: auto)')

    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))

    if not os.path.isdir(args.deploy_path):
        print(f"Creating deployment directory: {args.deploy_path}")
        os.makedirs(args.deploy_path, exist_ok=True)

    # Determine which compiler to use
    if args.compiler == 'auto':
        if check_mingw_support():
            compiler = 'mingw'
            print("Auto-detected: Using Enhanced MinGW (recommended)")
        elif check_clang_msvc_support():
            compiler = 'clang-msvc'
            print("Auto-detected: Using Clang with MSVC ABI (experimental - may fail)")
        else:
            print("Error: No suitable cross-compiler found!")
            print("Install MinGW with: sudo pacman -S mingw-w64-gcc")
            sys.exit(1)
    else:
        compiler = args.compiler

    # Validate compiler choice
    if compiler == 'clang-msvc':
        print("Warning: Clang MSVC mode requires Windows SDK libraries")
        print("This will likely fail without proper Windows development environment")
        if not check_clang_msvc_support():
            print("Error: Clang with MSVC target not available!")
            print("Install with: sudo pacman -S clang lld llvm")
            sys.exit(1)
    elif compiler == 'mingw' and not check_mingw_support():
        print("Error: MinGW cross-compiler not available!")
        print("Install with: sudo pacman -S mingw-w64-gcc")
        sys.exit(1)

    # Build with selected compiler
    try:
        if compiler == 'clang-msvc':
            dll_path = build_with_clang_msvc(script_dir, args.deploy_path, args.build_type)
            print(f"\n✓ MSVC-compatible build successful!")
            print(f"✓ DLL: {dll_path}")
            print("✓ Better Windows compatibility expected")
        else:
            dll_path = build_with_mingw(script_dir, args.deploy_path, args.build_type)
            print(f"\n✓ MinGW build successful!")
            print(f"✓ DLL: {dll_path}")
            print("✓ Requires libwinpthread-1.dll")

        if not os.path.isfile(dll_path):
            print(f"❌ Error: Expected DLL not found at {dll_path}")
            sys.exit(1)

    except Exception as e:
        print(f"❌ Build failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()