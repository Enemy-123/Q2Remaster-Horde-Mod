#!/usr/bin/env python3
"""
Automated debugging script for Q2 Horde Mod
Builds the mod and launches winedbg with auto-configured GDB in ONE terminal
"""

import os
import sys
import subprocess
import argparse

# Configuration
GAME_DIR = "/home/perrobjorn/Games/Heroic/Quake II Enhanced"
WINEPREFIX = "/home/perrobjorn/Games/Heroic/Prefixes/default/Quake II"
GAME_EXE = "quake2ex_gog.exe"
GAME_EXE_FULL_PATH = f"{GAME_DIR}/{GAME_EXE}"
GAME_ARGS = ["-skipmovies", "+exec", "horde.cfg", "+map", "q2ctf5"]
DEPLOY_PATH = f"{GAME_DIR}/baseq2"
REPO_DIR = "/home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod"

def check_pexpect():
    """Check if pexpect is available."""
    try:
        import pexpect
        return True
    except ImportError:
        return False

def build_mod(sanitizer='none'):
    """Build the mod using build_clang.py."""
    print("=== Building Mod ===")
    build_script = os.path.join(REPO_DIR, "build_clang.py")

    build_cmd = [
        "python3", build_script,
        DEPLOY_PATH,
        "RelWithDebInfo"
    ]

    if sanitizer != 'none':
        build_cmd.extend(["--sanitizer", sanitizer])

    try:
        result = subprocess.run(build_cmd, check=True)
        print("✓ Build successful!")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ Build failed with exit code {e.returncode}")
        return False

def launch_debugger_pexpect():
    """Launch debugger using pexpect for automated GDB commands."""
    import pexpect

    print("\n=== Launching Debugger (pexpect mode) ===")
    print(f"WINEPREFIX: {WINEPREFIX}")
    print(f"Game: {GAME_DIR}/{GAME_EXE}")
    print(f"Arguments: {' '.join(GAME_ARGS)}")
    print("\nWhen the game crashes, type 'bt' to see the backtrace.\n")

    # Build the winedbg command (use full path with quotes for paths with spaces)
    cmd = f'winedbg --gdb "{GAME_EXE_FULL_PATH}" {" ".join(GAME_ARGS)}'

    # Set up environment
    env = os.environ.copy()
    env['WINEPREFIX'] = WINEPREFIX
    env['WINEDLLOVERRIDES'] = 'mscoree,mshtml='  # Disable wine-mono/gecko popups

    # Launch winedbg with pexpect
    try:
        child = pexpect.spawn('/bin/bash', ['-c', cmd], env=env, encoding='utf-8')
        child.logfile = sys.stdout

        # Wait for Wine-gdb> prompt (handle pager and debuginfod)
        print("Waiting for GDB to initialize...")

        # Handle potential pager output first
        index = child.expect(['Wine-gdb>', '--Type.*for more', r'Enable debuginfod.*\?'], timeout=30)

        # If we hit a pager or debuginfod prompt, handle it
        while index != 0:
            if index == 1:  # Pager (use space+enter to skip one page at a time, then q at Wine-gdb>)
                print("[AUTO] Skipping pager...")
                # Keep pressing space until we see Wine-gdb> prompt or another pager
                while True:
                    child.send(' ')  # Space = show next page
                    idx = child.expect(['Wine-gdb>', '--Type.*for more'], timeout=5)
                    if idx == 0:  # Reached prompt
                        index = 0
                        break
                # index is now 0 (Wine-gdb>)
            elif index == 2:  # debuginfod
                print("[AUTO] Disabling debuginfod...")
                child.sendline('n')
                index = child.expect(['Wine-gdb>', '--Type.*for more'], timeout=10)

        # Now at Wine-gdb> prompt, send GDB commands
        print("\n[AUTO] Disabling pagination...")
        child.sendline('set pagination off')
        child.expect('Wine-gdb>', timeout=5)

        print("[AUTO] Setting solib-search-path...")
        child.sendline(f'set solib-search-path "{DEPLOY_PATH}"')
        child.expect('Wine-gdb>', timeout=5)

        print("[AUTO] Setting source directory...")
        child.sendline(f'directory {REPO_DIR}')
        child.expect('Wine-gdb>', timeout=5)

        print("[AUTO] Starting game (continue)...\n")
        child.sendline('c')

        # Hand over to interactive mode (remove logfile to avoid encoding issues)
        child.logfile = None
        try:
            child.interact()
        except OSError as e:
            if e.errno == 25:  # Inappropriate ioctl for device
                print("\n✗ Cannot enter interactive mode (not running in a TTY)")
                print("   This happens when running via pipes or in VS Code terminal")
                print("   Run directly in a real terminal for interactive debugging")
                # Keep process alive so user can see output
                child.wait()
            else:
                raise

    except pexpect.exceptions.TIMEOUT:
        print("\n✗ Timeout waiting for GDB prompt. Try manual mode with --no-auto")
        sys.exit(1)
    except pexpect.exceptions.EOF:
        print("\n✗ winedbg exited unexpectedly")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Error: {e}")
        sys.exit(1)

def launch_debugger_manual():
    """Launch debugger manually (prints instructions)."""
    print("\n=== Launching Debugger (manual mode) ===")
    print(f"WINEPREFIX: {WINEPREFIX}")
    print(f"Game: {GAME_DIR}/{GAME_EXE}")
    print(f"Arguments: {' '.join(GAME_ARGS)}\n")

    print("Setting up environment and launching winedbg...")
    print("After GDB initializes, you'll need to run these commands:\n")
    print('  set pagination off')
    print(f'  set solib-search-path "{DEPLOY_PATH}"')
    print(f'  directory {REPO_DIR}')
    print('  c\n')
    print("TIP: If you see '--Type <RET> for more--', press 'c' to skip paging.\n")
    print("When the game crashes, type 'bt' to see the backtrace.\n")

    # Set up environment
    env = os.environ.copy()
    env['WINEPREFIX'] = WINEPREFIX
    env['WINEDLLOVERRIDES'] = 'mscoree,mshtml='  # Disable wine-mono/gecko popups

    # Build and run the winedbg command (as shell string with full path)
    cmd = f'winedbg --gdb "{GAME_EXE_FULL_PATH}" {" ".join(GAME_ARGS)}'

    try:
        subprocess.run(cmd, env=env, shell=True)
    except KeyboardInterrupt:
        print("\n\nDebugger interrupted by user")
    except Exception as e:
        print(f"\n✗ Error launching debugger: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(
        description='Automated debugging for Q2 Horde Mod',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 debug.py                    # Build + auto-debug
  python3 debug.py --skip-build       # Skip build, just debug
  python3 debug.py --sanitizer ubsan  # Build with UBSan + debug
  python3 debug.py --no-auto          # Manual GDB commands (no pexpect)
        """
    )

    parser.add_argument('--skip-build', action='store_true',
                       help='Skip building, just launch debugger')
    parser.add_argument('--sanitizer', choices=['ubsan', 'none'], default='none',
                       help='Enable UndefinedBehaviorSanitizer for debugging')
    parser.add_argument('--no-auto', action='store_true',
                       help='Manual mode: print GDB commands instead of auto-sending')

    args = parser.parse_args()

    # Build if requested
    if not args.skip_build:
        if not build_mod(args.sanitizer):
            print("\nBuild failed. Fix errors and try again.")
            sys.exit(1)
        print()
    else:
        print("=== Skipping build (using existing DLL) ===\n")

    # Check for pexpect if auto mode requested
    if not args.no_auto:
        if not check_pexpect():
            print("⚠ pexpect not found. Install with: pip install pexpect")
            print("⚠ Falling back to manual mode...\n")
            args.no_auto = True

    # Launch debugger
    if args.no_auto:
        launch_debugger_manual()
    else:
        launch_debugger_pexpect()

if __name__ == "__main__":
    main()
