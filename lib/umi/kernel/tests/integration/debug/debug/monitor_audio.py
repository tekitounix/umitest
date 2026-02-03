#!/usr/bin/env python3
"""
Real-time audio counter monitoring.
Plays audio to UMI device and monitors counters every 200ms.
"""

import sys
import time
import numpy as np
import sounddevice as sd
from pyocd.core.helpers import ConnectHelper

def find_umi_device():
    """Find UMI audio device index."""
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        name = dev['name']
        if 'UMI' in name or 'Kernel Synth' in name:
            if dev['max_output_channels'] > 0:
                return i, name
    return None, None

def main():
    umi_idx, umi_name = find_umi_device()
    if umi_idx is None:
        print("ERROR: UMI device not found!")
        return 1
    print(f"Using device: {umi_name} (index {umi_idx})")
    
    # Generate sine wave
    duration = 10  # Longer for monitoring
    sample_rate = 48000
    freq = 440
    n_samples = int(duration * sample_rate)
    t = np.linspace(0, duration, n_samples, endpoint=False)
    amplitude = 0.3
    mono = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    stereo = np.column_stack((mono, mono))
    
    print(f"Starting audio playback ({freq}Hz sine wave)...")
    sd.play(stereo, samplerate=sample_rate, device=umi_idx, blocking=False)
    
    time.sleep(0.5)  # Let audio start
    
    print("Connecting to target...")
    with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
        target = session.target
        
        print("\nMonitoring (every 500ms):")
        print("Time    ISR     Buffered  UsbRx     Underruns  Missed  Streaming  PLL-ppm")
        print("=" * 90)
        
        prev_underrun = 0
        prev_missed = 0
        prev_usb_rx = 0
        start_time = time.time()
        
        for i in range(16):  # 8 seconds of monitoring
            time.sleep(0.5)
            
            target.halt()
            # Read 12 counters now (added dbg_usb_rx_count)
            data = target.read_memory_block32(0x200010c0, 12)
            target.resume()
            
            elapsed = time.time() - start_time
            isr = data[0]
            buffered = data[2]
            underrun = data[5]
            streaming = data[6]
            pll_ppm = data[7] if data[7] < 0x80000000 else data[7] - 0x100000000
            missed = data[9]
            process_time = data[10]
            usb_rx = data[11]
            
            delta_ur = underrun - prev_underrun
            delta_missed = missed - prev_missed
            delta_rx = usb_rx - prev_usb_rx
            prev_underrun = underrun
            prev_missed = missed
            prev_usb_rx = usb_rx
            
            print(f"{elapsed:5.1f}s  {isr:6}  {buffered:8}  {delta_rx:8}  {underrun:9}  {delta_missed:6}  {streaming:9}  {pll_ppm:7}")
        
        sd.stop()
        
        # Final stats
        print(f"\nTotal missed: {missed}")
        print(f"Total USB RX packets: {usb_rx}")
        print("\nDone")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
