#!/usr/bin/env python3
"""
Clang build script for Q2 Horde Mod
Uses Clang for cross-compilation on Linux
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

def should_copy_file(src, dst):
    """Check if file should be copied (if dst doesn't exist or src is newer)."""
    # Skip if destination is a symlink (preserve user's manual links)
    if os.path.islink(dst):
        return False
    if not os.path.exists(dst):
        return True
    src_stat = os.stat(src)
    dst_stat = os.stat(dst)
    # Copy if source is newer or different size
    return src_stat.st_mtime > dst_stat.st_mtime or src_stat.st_size != dst_stat.st_size

def smart_copy_tree(src_dir, dst_dir):
    """Recursively copy directory tree, only copying new/modified files."""
    if not os.path.exists(src_dir):
        print(f"Warning: Source directory {src_dir} doesn't exist, skipping")
        return

    copied_count = 0
    skipped_count = 0

    for root, dirs, files in os.walk(src_dir):
        rel_path = os.path.relpath(root, src_dir)
        dst_root = os.path.join(dst_dir, rel_path) if rel_path != '.' else dst_dir

        os.makedirs(dst_root, exist_ok=True)

        for file in files:
            src_file = os.path.join(root, file)
            dst_file = os.path.join(dst_root, file)

            if should_copy_file(src_file, dst_file):
                shutil.copy2(src_file, dst_file)
                copied_count += 1
            else:
                skipped_count += 1

    return copied_count, skipped_count

def deploy_data_files(script_dir, deploy_path):
    """Deploy bots, ents, and Lua config files to the game directory."""
    print("\n=== Deploying Data Files ===")
    deploy_src = os.path.join(script_dir, "deploy")
    game_dir = os.path.normpath(deploy_path)

    total_copied = 0
    total_skipped = 0

    # Copy bots folder
    bots_src = os.path.join(deploy_src, "bots")
    bots_dst = os.path.join(game_dir, "bots")
    if os.path.exists(bots_src):
        copied, skipped = smart_copy_tree(bots_src, bots_dst)
        total_copied += copied
        total_skipped += skipped
        print(f"bots/: {copied} copied, {skipped} skipped")

    # Copy ents folder
    ents_src = os.path.join(deploy_src, "ents")
    ents_dst = os.path.join(game_dir, "ents")
    if os.path.exists(ents_src):
        copied, skipped = smart_copy_tree(ents_src, ents_dst)
        total_copied += copied
        total_skipped += skipped
        print(f"ents/: {copied} copied, {skipped} skipped")

    # Copy config files
    config_dst_dir = os.path.join(game_dir, "config")
    os.makedirs(config_dst_dir, exist_ok=True)

    config_src_dir = os.path.join(deploy_src, "config")
    for filename in sorted(os.listdir(config_src_dir)) if os.path.exists(config_src_dir) else []:
        if not filename.endswith(".lua"):
            continue
        config_src = os.path.join(config_src_dir, filename)
        config_dst = os.path.join(config_dst_dir, filename)
        if should_copy_file(config_src, config_dst):
            shutil.copy2(config_src, config_dst)
            total_copied += 1
            print(f"config/{filename}: copied")
        else:
            total_skipped += 1
            print(f"config/{filename}: skipped (unchanged)")

    print(f"\nTotal: {total_copied} files copied, {total_skipped} files skipped")

def check_clang_support():
    """Check if Clang is available."""
    try:
        result = subprocess.run(
            ['clang++', '--version'],
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

def build_with_clang(script_dir, deploy_path, build_type, sanitizer='none'):
    """Build using Clang with improved compatibility flags."""
    print("=== Building with Clang ===")
    if sanitizer != 'none':
        print(f"*** Sanitizer enabled: {sanitizer.upper()} ***")

    build_dir = os.path.join(script_dir, "build_clang")
    vcpkg_installed_dir = os.path.join(script_dir, "vcpkg_installed")
    clang_toolchain_file = os.path.join(script_dir, "clang-w64-x86_64.cmake")
    vcpkg_toolchain_file = os.path.join(script_dir, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake")

    fake_bin_dir = setup_fake_powershell(script_dir)
    build_env = os.environ.copy()
    build_env["PATH"] = f"{fake_bin_dir}{os.pathsep}{build_env['PATH']}"
    # Set package DIRs to help cmake find the packages
    build_env["jsoncpp_DIR"] = os.path.join(vcpkg_installed_dir, "x64-mingw-static", "share", "jsoncpp")
    build_env["fmt_DIR"] = os.path.join(vcpkg_installed_dir, "x64-mingw-static", "share", "fmt")

    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir)

    # Enhanced linker flags for better Windows compatibility - let toolchain handle this
    enhanced_linker_flags = "-lpthread"

    # Add sanitizer flags if requested
    sanitizer_flags = ""
    if sanitizer == 'asan':
        sanitizer_flags = "-fsanitize=address -fno-omit-frame-pointer -g"
        enhanced_linker_flags += f" {sanitizer_flags}"
    elif sanitizer == 'ubsan':
        sanitizer_flags = "-fsanitize=undefined -fno-omit-frame-pointer -g"
        enhanced_linker_flags += f" {sanitizer_flags}"

    cmake_configure_command = [
        "cmake", "..",
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DDEPLOY_DIRECTORY={deploy_path}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain_file}",
        f"-DVCPKG_INSTALLED_DIR={vcpkg_installed_dir}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={clang_toolchain_file}",
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-static",
        f"-Dfmt_DIR={os.path.join(vcpkg_installed_dir, 'x64-mingw-static', 'share', 'fmt')}",
        f"-Djsoncpp_DIR={os.path.join(vcpkg_installed_dir, 'x64-mingw-static', 'share', 'jsoncpp')}",
        f"-DCMAKE_SHARED_LINKER_FLAGS={enhanced_linker_flags}",
    ]

    # Only add sanitizer flag for CXX if requested - don't override all the toolchain flags
    if sanitizer_flags:
        cmake_configure_command.append(f"-DCMAKE_CXX_FLAGS={sanitizer_flags}")

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
            # Copy to parent directory (where game executable is)
            game_dir = os.path.dirname(os.path.normpath(deploy_path))
            print(f"Copying {pthread_dll} to {game_dir}")
            shutil.copy(source_path, game_dir)

    shutil.rmtree(fake_bin_dir)
    return os.path.join(deploy_path, "game_x64.dll")

def main():
    parser = argparse.ArgumentParser(description='Build Q2 Horde Mod with Clang')
    parser.add_argument('deploy_path', help='Directory where game_x64.dll should be installed')
    parser.add_argument('build_type', choices=['Debug', 'Release', 'RelWithDebInfo'],
                       help='CMake build type')
    parser.add_argument('--sanitizer', choices=['asan', 'ubsan', 'none'], default='none',
                       help='Enable sanitizer (asan=Address Sanitizer, ubsan=Undefined Behavior Sanitizer)')

    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))

    if not os.path.isdir(args.deploy_path):
        print(f"Creating deployment directory: {args.deploy_path}")
        os.makedirs(args.deploy_path, exist_ok=True)

    # Check Clang support
    if not check_clang_support():
        print("Error: Clang compiler not available!")
        print("Install with: sudo pacman -S clang")
        sys.exit(1)

    print("Using Clang compiler")

    # Build with Clang
    try:
        dll_path = build_with_clang(script_dir, args.deploy_path, args.build_type, args.sanitizer)
        print(f"\n✓ Clang build successful!")
        print(f"✓ DLL: {dll_path}")
        print("✓ Requires libwinpthread-1.dll")
        if args.sanitizer != 'none':
            print(f"✓ Sanitizer {args.sanitizer.upper()} enabled - expect performance impact and detailed error reporting")

        if not os.path.isfile(dll_path):
            print(f"❌ Error: Expected DLL not found at {dll_path}")
            sys.exit(1)

        # Deploy data files (bots, ents, config/*.lua)
        deploy_data_files(script_dir, args.deploy_path)

    except Exception as e:
        print(f"❌ Build failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
