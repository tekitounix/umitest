#!/usr/bin/env python3
"""
Deep debug test - verify app process() is being called
"""
import subprocess
import time

subprocess.run(['pkill', '-f', 'pyocd'], capture_output=True)
time.sleep(0.5)

from pyocd.core.helpers import ConnectHelper

with ConnectHelper.session_with_chosen_probe(
    target_override='stm32f407vg',
    options={'connect_mode': 'under-reset'}
) as session:
    target = session.target
    
    # Reset and let it run
    target.reset_and_halt()
    
    # Let it run for a bit to initialize
    target.resume()
    time.sleep(0.5)
    target.halt()
    
    pc = target.read_core_register('pc')
    print(f"PC after 500ms: 0x{pc:08X}")
    
    # Check SharedMemory (at 0x20018000)
    # sample_rate should be 48000 (0xBB80)
    sample_rate = target.read32(0x20018000)
    buffer_size = target.read32(0x20018004)
    sample_pos_lo = target.read32(0x20018008)
    sample_pos_hi = target.read32(0x2001800C)
    sample_pos = sample_pos_lo | (sample_pos_hi << 32)
    
    print(f"\nSharedMemory:")
    print(f"  sample_rate: {sample_rate} (expected: 48000)")
    print(f"  buffer_size: {buffer_size} (expected: 256)")
    print(f"  sample_position: {sample_pos}")
    
    # Check if loader has process function registered
    # g_loader is at some address - check via symbol if available
    # For now, read loader struct area (around 0x20000000)
    
    # Check audio output buffer (first few samples)
    print(f"\nAudio output buffer (0x20018010):")
    for i in range(4):
        addr = 0x20018010 + i * 4
        val = target.read32(addr)
        # Interpret as float
        import struct
        float_val = struct.unpack('<f', struct.pack('<I', val))[0]
        print(f"  [{i}]: 0x{val:08X} = {float_val:.6f}")
    
    # Check LED state
    odr_val = target.read32(0x40020C14)
    print(f"\nGPIOD ODR: 0x{odr_val:08X}")
    if odr_val & (1 << 12):
        print("  ✓ Green LED ON - Kernel running, app loaded")
    if odr_val & (1 << 14):
        print("  ✗ Red LED ON - Error state")
    if odr_val & (1 << 15):
        print("  ? Blue LED ON - Still in startup")
    
    # Check if we're in audio_loop (WFI)
    # Run a bit more and see if sample_position increases
    target.resume()
    time.sleep(0.2)
    target.halt()
    
    sample_pos2_lo = target.read32(0x20018008)
    sample_pos2_hi = target.read32(0x2001800C)
    sample_pos2 = sample_pos2_lo | (sample_pos2_hi << 32)
    
    print(f"\nAfter 200ms more:")
    print(f"  sample_position: {sample_pos2}")
    
    if sample_pos2 > sample_pos:
        print("  ✓ Audio processing running! sample_position increased")
    else:
        print("  ? sample_position not changing (audio not running or DMA not configured)")
    
    print("\n=== Deep Test Complete ===")
