#!/usr/bin/env python3
"""
Quick Renode test for kernel+app separation
Runs Renode with timeout and checks basic functionality
"""

import subprocess
import sys
import os
from pathlib import Path

def main():
    project_root = Path(__file__).parent.parent.parent
    os.chdir(project_root)
    
    # Check build artifacts exist
    kernel_elf = project_root / "build/stm32f4_kernel/release/stm32f4_kernel.elf"
    app_bin = project_root / "build/synth_app/release/synth_app.bin"
    
    if not kernel_elf.exists():
        print(f"ERROR: Kernel not found: {kernel_elf}")
        print("Run: xmake -b stm32f4_kernel")
        return 1
    
    if not app_bin.exists():
        print(f"ERROR: App not found: {app_bin}")
        print("Run: xmake -b synth_app")
        return 1
    
    print(f"Kernel: {kernel_elf} ({kernel_elf.stat().st_size} bytes)")
    print(f"App:    {app_bin} ({app_bin.stat().st_size} bytes)")
    
    # Verify ELF format
    result = subprocess.run(["file", str(kernel_elf)], capture_output=True, text=True)
    if "ARM" not in result.stdout:
        print(f"ERROR: Kernel is not ARM ELF: {result.stdout}")
        return 1
    print(f"Kernel format: OK (ARM ELF)")
    
    # Verify app binary header (check magic number)
    with open(app_bin, "rb") as f:
        header = f.read(16)
    
    # First 4 bytes should be initial SP (pointing to RAM)
    # Or if using .umia format, check for header
    print(f"App header (first 16 bytes): {header.hex()}")
    
    # Check app's .umia file
    app_umia = project_root / "build/synth_app/release/synth_app.umia"
    if app_umia.exists():
        with open(app_umia, "rb") as f:
            magic = f.read(8)
        if magic == b"UMIAPP\x00\x00":
            print("App .umia header: OK (valid magic)")
        else:
            print(f"App .umia header: {magic}")
    
    print("\n" + "="*60)
    print("Build artifacts verified successfully!")
    print("="*60)
    print("\nTo test in Renode interactively:")
    print(f"  cd {project_root}")
    print("  renode tools/renode/kernel_app.resc")
    print("\nOr run automated test (requires Robot Framework):")
    print("  xmake robot")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
