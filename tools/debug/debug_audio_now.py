#!/usr/bin/env python3
"""
USB Audio debug script - plays audio and monitors counters.
Uses only variables that exist in current build.
"""

import sys
import time
import numpy as np
import sounddevice as sd
from pyocd.core.helpers import ConnectHelper

# Current debug variable addresses (from symbol table)
# Run: arm-none-eabi-nm -C build/stm32f4_kernel/release/stm32f4_kernel | grep dbg_
ADDR_ISR_COUNT    = 0x200010b8
ADDR_OUT_BUFFERED = 0x200010bc
ADDR_STREAMING    = 0x200010c0
ADDR_PLL_PPM      = 0x200010c4
ADDR_USB_RX_COUNT = 0x200010c8
ADDR_SET_IFACE    = 0x200010cc
ADDR_SOF_COUNT    = 0x200010d0
ADDR_FEEDBACK     = 0x200010d4

def find_umi_device():
    """Find UMI audio device index."""
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        if 'UMI' in dev['name'] or 'Kernel' in dev['name']:
            return i, dev['name'], dev['max_output_channels'], dev['max_input_channels']
    return None, None, 0, 0

def read_signed32(val):
    """Convert unsigned to signed 32-bit."""
    return val if val < 0x80000000 else val - 0x100000000

def main():
    print("=" * 70)
    print("USB Audio Debug Monitor")
    print("=" * 70)
    
    # Find device
    idx, name, out_ch, in_ch = find_umi_device()
    if idx is None:
        print("ERROR: UMI device not found!")
        return 1
    print(f"Device: {name} (index {idx})")
    print(f"  Output channels: {out_ch}")
    print(f"  Input channels: {in_ch}")
    
    # Generate test tone
    duration = 8.0
    sample_rate = 48000
    freq = 440
    n_samples = int(duration * sample_rate)
    t = np.linspace(0, duration, n_samples, endpoint=False)
    amplitude = 0.3
    mono = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    stereo = np.column_stack((mono, mono))
    
    print(f"\nStarting {freq}Hz sine wave playback for {duration}s...")
    
    # Start playback (non-blocking)
    sd.play(stereo, samplerate=sample_rate, device=idx, blocking=False)
    
    # Wait a bit for audio to start
    time.sleep(0.3)
    
    print("\nConnecting to target...")
    try:
        with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
            target = session.target
            
            print("\nMonitoring counters (press Ctrl+C to stop):")
            print("-" * 85)
            print(f"{'Time':>6}  {'ISR':>8}  {'Buffered':>8}  {'Stream':>6}  {'USB_RX':>8}  {'SetIf':>6}  {'SOF':>8}  {'PLL':>6}")
            print("-" * 85)
            
            prev_isr = 0
            prev_rx = 0
            prev_sof = 0
            start_time = time.time()
            
            for _ in range(int(duration * 2)):  # Check every 0.5s
                time.sleep(0.5)
                
                target.halt()
                isr = target.read32(ADDR_ISR_COUNT)
                buffered = target.read32(ADDR_OUT_BUFFERED)
                streaming = target.read32(ADDR_STREAMING)
                pll_ppm = read_signed32(target.read32(ADDR_PLL_PPM))
                usb_rx = target.read32(ADDR_USB_RX_COUNT)
                set_iface = target.read32(ADDR_SET_IFACE)
                sof = target.read32(ADDR_SOF_COUNT)
                target.resume()
                
                elapsed = time.time() - start_time
                delta_isr = isr - prev_isr
                delta_rx = usb_rx - prev_rx
                delta_sof = sof - prev_sof
                prev_isr = isr
                prev_rx = usb_rx
                prev_sof = sof
                
                status = "YES" if streaming else "NO"
                print(f"{elapsed:6.1f}s  {delta_isr:8}  {buffered:8}  {status:>6}  {delta_rx:8}  {set_iface:6}  {delta_sof:8}  {pll_ppm:6}")
            
            sd.stop()
            
            print("-" * 85)
            print(f"\nFinal: ISR={isr}, USB_RX={usb_rx}, Buffered={buffered}, SetIface={set_iface}, SOF={sof}")
            
            if set_iface == 0:
                print("\n*** WARNING: set_interface never called! ***")
                print("Check: USB enumeration failed? Descriptors incorrect?")
            elif usb_rx == 0:
                print("\n*** WARNING: No USB packets received! ***")
                print("Check: Endpoint not ready? Host not sending?")
            elif buffered == 0 and streaming == 0:
                print("\n*** WARNING: Not streaming! ***")
                
    except Exception as e:
        sd.stop()
        print(f"Error: {e}")
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
