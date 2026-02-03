#!/usr/bin/env python3
"""Audio debug counters checker using pyOCD with automated audio playback"""

from pyocd.core.helpers import ConnectHelper
from pyocd.flash.file_programmer import FileProgrammer
import time
import subprocess
import threading
import numpy as np
import tempfile
import os

def find_umi_device():
    """Find UMI USB Audio device index using sounddevice"""
    import sounddevice as sd
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        if 'UMI' in dev['name'] and dev['max_output_channels'] >= 2:
            return i, dev['name']
    return None, None

def play_sine_to_device(device_idx, duration=5, freq=440, sample_rate=48000):
    """Play sine wave directly to specified device"""
    import sounddevice as sd
    
    n_samples = int(duration * sample_rate)
    t = np.linspace(0, duration, n_samples, endpoint=False)
    amplitude = 0.3
    mono = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    stereo = np.column_stack((mono, mono))
    
    sd.play(stereo, samplerate=sample_rate, device=device_idx, blocking=False)
    return sd

def main():
    print("=== UMI Audio Debug Test ===\n")
    
    # Find UMI device first
    umi_idx, umi_name = find_umi_device()
    if umi_idx is None:
        print("ERROR: UMI USB Audio device not found!")
        print("Make sure the device is connected and enumerated.")
        return 1
    print(f"Found UMI device: {umi_name} (index {umi_idx})")
    
    print("\nConnecting to target...")
    
    with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
        target = session.target
        
        # Reset & halt
        target.reset_and_halt()
        
        # Flash firmware
        print("Flashing firmware...")
        FileProgrammer(session).program('build/stm32f4_kernel/release/stm32f4_kernel.bin', base_address=0x08000000)
        
        # Start execution
        target.reset()
        print("Firmware running, waiting for USB enumeration...")
        time.sleep(3)  # USB enumeration needs more time
        
        # Re-check device after reset with retry
        for retry in range(5):
            time.sleep(1)
            umi_idx, umi_name = find_umi_device()
            if umi_idx is not None:
                break
            print(f"  Waiting for device... ({retry+1}/5)")
        
        if umi_idx is None:
            print("ERROR: UMI device not found after firmware flash!")
            return 1
        print(f"UMI device ready: {umi_name} (index {umi_idx})")
        
        # Start audio playback directly to UMI device
        print("Starting audio playback to UMI device...")
        import sounddevice as sd
        
        # Retry audio playback on error
        for retry in range(3):
            try:
                play_sine_to_device(umi_idx, duration=6, freq=440)
                break
            except Exception as e:
                print(f"  Audio error (retry {retry+1}/3): {e}")
                time.sleep(1)
                # Re-find device
                umi_idx, umi_name = find_umi_device()
                if umi_idx is None:
                    print("ERROR: Device lost!")
                    return 1
        else:
            print("ERROR: Could not start audio playback after retries")
            return 1
        
        # Wait for audio to play
        print("Playing test tone (440Hz) for 5 seconds...")
        time.sleep(5)
        
        # Stop playback
        sd.stop()
        
        # Stop target and read counters
        target.halt()
        
        # Read debug counters
        data = target.read_memory_block32(0x200010c0, 7)
        
        print(f"\n=== Debug Counters ===")
        print(f"dbg_i2s_isr_count:    {data[0]}")
        print(f"dbg_fill_audio_count: {data[1]}")
        print(f"dbg_out_buffered:     {data[2]}")
        print(f"dbg_in_buffered:      {data[3]}")
        print(f"dbg_read_frames:      {data[4]}")
        print(f"dbg_underrun:         {data[5]}")
        print(f"dbg_streaming:        {data[6]} {'(streaming)' if data[6] else '(NOT streaming)'}")
        
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
        
        if data[5] > 0:
            underrun_rate = data[5] / data[0] * 100 if data[0] > 0 else 0
            print(f"WARNING: {data[5]} underruns ({underrun_rate:.1f}%)")
        else:
            print("OK: No underruns")
        
        if data[4] == 0 and data[6] == 1:
            print("ERROR: Streaming but read_audio_asrc returning 0!")
        elif data[4] > 0:
            print(f"OK: read_audio_asrc returning {data[4]} frames")
        
        print("\nDone")
        return 0

if __name__ == '__main__':
    exit(main())
