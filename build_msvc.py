#!/usr/bin/env python3
"""
MSVC-compatible build script using Clang with MSVC ABI
This should provide better Windows compatibility than MinGW while building on Linux
"""

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

def check_dependencies():
    """Check if required tools are available."""
    required_tools = ['clang++', 'lld-link', 'llvm-rc']
    missing_tools = []

    for tool in required_tools:
        try:
            subprocess.run([tool, '--version'], check=True, capture_output=True)
            print(f"✓ Found {tool}")
        except (subprocess.CalledProcessError, FileNotFoundError):
            missing_tools.append(tool)

    if missing_tools:
        print(f"❌ Missing required tools: {', '.join(missing_tools)}")
        print("Install with: sudo pacman -S clang lld llvm")
        return False

    return True

def main():
    # --- Configuration ---
    script_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(script_dir, "build_msvc")
    vcpkg_installed_dir = os.path.join(script_dir, "vcpkg_installed")
    clang_toolchain_file = os.path.join(script_dir, "clang-msvc-x86_64.cmake")
    vcpkg_toolchain_file = os.path.join(script_dir, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake")

    # --- Get Arguments ---
    if len(sys.argv) < 3:
        print("Usage: python3 build_msvc.py <deploy_path> <build_type>")
        print("Example: python3 build_msvc.py /path/to/game/baseq2 Release")
        sys.exit(1)

    deploy_path = sys.argv[1]
    build_type = sys.argv[2]

    print(f"--- MSVC-Compatible Build using Clang ---")
    print(f"Build Type: {build_type}")
    print(f"Deploy Path: {deploy_path}")

    # --- Check Dependencies ---
    if not check_dependencies():
        sys.exit(1)

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
    print("--- Starting Clang MSVC-Compatible Cross-Compile Build ---")
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
        f"-DVCPKG_INSTALLED_DIR={vcpkg_installed_dir}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={clang_toolchain_file}",
        "-DVCPKG_TARGET_TRIPLET=x64-windows-static",  # Use Windows static libraries
        "-DCMAKE_SYSTEM_NAME=Windows",
        "-DCMAKE_CXX_COMPILER=clang++",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc",
        "-DCMAKE_C_COMPILER_TARGET=x86_64-pc-windows-msvc"
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

    print("\n--- BUILD SUCCESSFUL ---")
    print(f"MSVC-compatible DLL successfully installed to: '{final_dll_path}'")
    print("This build should have better Windows compatibility than MinGW!")

    # --- Cleanup ---
    if os.path.exists(fake_bin_dir):
        shutil.rmtree(fake_bin_dir)

if __name__ == "__main__":
    main()