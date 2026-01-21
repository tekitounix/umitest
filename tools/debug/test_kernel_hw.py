#!/usr/bin/env python3
"""
Quick debug test for stm32f4_kernel
Runs with timeout and proper cleanup
"""
import subprocess
import time
import sys

# Kill any remaining debugger processes first
subprocess.run(['pkill', '-f', 'pyocd'], capture_output=True)
time.sleep(0.5)

try:
    from pyocd.core.helpers import ConnectHelper
except ImportError:
    print("pyocd not installed. Run: pip install pyocd")
    sys.exit(1)

try:
    with ConnectHelper.session_with_chosen_probe(
        target_override='stm32f407vg',
        options={'connect_mode': 'under-reset'}
    ) as session:
        target = session.target
        
        # Reset and halt
        target.reset_and_halt()
        
        # Read initial state
        pc = target.read_core_register('pc')
        sp = target.read_core_register('sp')
        print(f"Initial PC: 0x{pc:08X}")
        print(f"Initial SP: 0x{sp:08X}")
        
        # Read vector table (use read32 for 32-bit read)
        reset_addr = target.read32(0x08000004)
        print(f"Reset Handler: 0x{reset_addr:08X}")
        
        # Step 100 instructions
        print("\nStepping 100 instructions...")
        for i in range(100):
            target.step()
        
        pc_after = target.read_core_register('pc')
        print(f"PC after 100 steps: 0x{pc_after:08X}")
        
        # Check if we're in main (0x080001A0 area)
        if 0x08000100 <= pc_after <= 0x08000600:
            print("✓ Execution progressing in code section")
        else:
            print(f"? PC at unexpected location")
        
        # Run for a bit and check state
        print("\nRunning for 100ms...")
        target.resume()
        time.sleep(0.1)
        target.halt()
        
        pc_running = target.read_core_register('pc')
        print(f"PC after running: 0x{pc_running:08X}")
        
        # Check if we're in the kernel code range
        if 0x08000000 <= pc_running <= 0x08000700:
            print("✓ CPU running in kernel code")
        else:
            print(f"? PC at unexpected location: 0x{pc_running:08X}")
        
        # Read app flash region to verify app is there
        app_header = target.read_memory_block8(0x08060000, 16)
        print(f"\nApp at 0x08060000: {bytes(app_header).hex()}")
        
        # Check LED state (GPIOD ODR at 0x40020C14)
        odr_val = target.read32(0x40020C14)
        print(f"GPIOD ODR: 0x{odr_val:08X}")
        if odr_val & (1 << 15):
            print("  Blue LED (PD15) ON - Startup phase")
        if odr_val & (1 << 14):
            print("  Red LED (PD14) ON - Error!")
        if odr_val & (1 << 12):
            print("  Green LED (PD12) ON - Running")
        
        print("\n=== Test Complete ===")
        
except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
