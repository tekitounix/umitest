#!/usr/bin/env python3
"""
UMI Audio Debug Test - Two-phase approach
Phase 1: Flash firmware and let USB stabilize
Phase 2: Play audio and read counters
"""

from pyocd.core.helpers import ConnectHelper
from pyocd.flash.file_programmer import FileProgrammer
import time
import numpy as np
import sys
import os

def find_umi_device():
    """Find UMI USB Audio device index using sounddevice"""
    import sounddevice as sd
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        if 'UMI' in dev['name'] and dev['max_output_channels'] >= 2:
            return i, dev['name']
    return None, None

def phase1_flash():
    """Phase 1: Flash firmware and exit to let USB stabilize"""
    print("=== Phase 1: Flash Firmware ===\n")
    
    with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
        target = session.target
        target.reset_and_halt()
        
        print("Flashing firmware...")
        FileProgrammer(session).program(
            'build/stm32f4_kernel/release/stm32f4_kernel.bin', 
            base_address=0x08000000
        )
        
        print("Starting firmware...")
        target.reset()
        print("Firmware running. Wait 5 seconds for USB to stabilize...")
    
    time.sleep(5)
    
    # Check device
    umi_idx, umi_name = find_umi_device()
    if umi_idx is None:
        print("ERROR: UMI device not found!")
        return 1
    print(f"OK: UMI device found: {umi_name} (index {umi_idx})")
    print("\nPhase 1 complete. Run with --phase2 to continue.\n")
    return 0

def phase2_test():
    """Phase 2: Play audio and read counters"""
    print("=== Phase 2: Audio Test ===\n")
    
    import sounddevice as sd
    
    # Find device
    umi_idx, umi_name = find_umi_device()
    if umi_idx is None:
        print("ERROR: UMI device not found! Run --phase1 first.")
        return 1
    print(f"Using device: {umi_name} (index {umi_idx})")
    
    # Generate sine wave
    duration = 6
    sample_rate = 48000
    freq = 440
    n_samples = int(duration * sample_rate)
    t = np.linspace(0, duration, n_samples, endpoint=False)
    amplitude = 0.3
    mono = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    stereo = np.column_stack((mono, mono))
    
    # Connect to target to read initial counters
    print("Connecting to target...")
    with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
        target = session.target
        target.halt()
        
        # Read initial counters
        data_before = target.read_memory_block32(0x200010c0, 9)
        isr_before = data_before[0]
        underrun_before = data_before[5]
        print(f"Initial: ISR={isr_before}, underruns={underrun_before}")
        
        target.resume()
    
    # Start audio playback
    print(f"Starting audio playback ({freq}Hz sine wave)...")
    try:
        sd.play(stereo, samplerate=sample_rate, device=umi_idx, blocking=False)
    except Exception as e:
        print(f"ERROR: Could not start audio: {e}")
        return 1
    
    print("Playing for 5 seconds...")
    time.sleep(5)
    
    # Connect to target and read counters
    print("\nReading debug counters...")
    with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
        target = session.target
        target.halt()
        
        # Stop audio
        sd.stop()
        
        # Read debug counters
        data = target.read_memory_block32(0x200010c0, 9)
        
        # Calculate delta
        isr_delta = data[0] - isr_before
        underrun_delta = data[5] - underrun_before
        
        print(f"\n=== Debug Counters ===")
        print(f"dbg_i2s_isr_count:    {data[0]} (delta: {isr_delta})")
        print(f"dbg_fill_audio_count: {data[1]}")
        print(f"dbg_fill_audio_count: {data[1]}")
        print(f"dbg_out_buffered:     {data[2]}")
        print(f"dbg_in_buffered:      {data[3]}")
        print(f"dbg_read_frames:      {data[4]}")
        print(f"dbg_underrun:         {data[5]} (delta: {underrun_delta})")
        print(f"dbg_streaming:        {data[6]} {'(streaming)' if data[6] else '(NOT streaming)'}")
        # data[7] is int32, need to convert from unsigned
        pll_ppm = data[7] if data[7] < 0x80000000 else data[7] - 0x100000000
        print(f"dbg_pll_ppm:          {pll_ppm} ppm")
        print(f"dbg_overrun:          {data[8]}")
        
        # Analysis
        print(f"\n=== Analysis ===")
        if data[0] == 0:
            print("ERROR: I2S ISR not firing!")
        elif data[0] != data[1]:
            print(f"WARNING: Missed frames! ISR={data[0]}, Fill={data[1]}")
        else:
            print(f"OK: I2S ISR and fill_audio in sync ({data[0]} calls)")
        
        if data[6] == 0:
            print("WARNING: USB Audio OUT not streaming - audio not reaching device")
        else:
            print("OK: USB Audio OUT is streaming")
        
        # Buffer level analysis
        target_level = 128
        if data[2] < target_level:
            print(f"WARNING: Buffer below target ({data[2]} < {target_level})")
        else:
            print(f"OK: Buffer level at {data[2]} (target={target_level})")
        
        if data[5] > 0:
            underrun_rate = underrun_delta / isr_delta * 100 if isr_delta > 0 else 0
            print(f"WARNING: {underrun_delta} new underruns during test ({underrun_rate:.2f}%)")
        else:
            print("OK: No underruns")
        
        if data[4] == 0 and data[6] == 1:
            print("ERROR: Streaming but read_audio_asrc returning 0!")
        elif data[4] > 0:
            print(f"OK: read_audio_asrc returning {data[4]} frames")
    
    print("\nDone")
    return 0

def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--phase2':
        return phase2_test()
    elif len(sys.argv) > 1 and sys.argv[1] == '--phase1':
        return phase1_flash()
    else:
        # Run both phases
        ret = phase1_flash()
        if ret != 0:
            return ret
        return phase2_test()

if __name__ == '__main__':
    exit(main())
