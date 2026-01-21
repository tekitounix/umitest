#!/usr/bin/env python3
"""
UMI Debug Tool - Unified CLI for STM32 USB Audio debugging.

Commands:
    symbols     List debug symbols in ELF file
    read        Read debug variable(s) from target
    monitor     Continuously monitor debug variables
    audio-test  Play test tone and monitor audio counters
    flash       Flash firmware to target
    status      Show target status (CPU state, LEDs, memory)

Examples:
    # List all debug symbols
    umi_debug.py symbols build/stm32f4_kernel/release/stm32f4_kernel

    # Read specific variable
    umi_debug.py read build/stm32f4_kernel/release/stm32f4_kernel dbg_streaming

    # Monitor all dbg_* variables every 500ms
    umi_debug.py monitor build/stm32f4_kernel/release/stm32f4_kernel

    # Play test tone and monitor audio counters
    umi_debug.py audio-test build/stm32f4_kernel/release/stm32f4_kernel

    # Flash firmware
    umi_debug.py flash build/stm32f4_kernel/release/stm32f4_kernel
"""

import argparse
import sys
import time
from pathlib import Path


def cmd_symbols(args):
    """List debug symbols in ELF file."""
    from stm32_debug import parse_symbols

    symbols = parse_symbols(args.elf)
    prefix = args.prefix or ''

    filtered = {k: v for k, v in symbols.items() if k.startswith(prefix)}

    print(f"Symbols in {args.elf} (prefix='{prefix}'):")
    print("-" * 50)
    for name, addr in sorted(filtered.items(), key=lambda x: x[1]):
        print(f"  0x{addr:08x}  {name}")
    print(f"\nTotal: {len(filtered)} symbols")


def cmd_read(args):
    """Read debug variable(s) from target."""
    from stm32_debug import TargetDebugger

    with TargetDebugger(args.elf, target=args.target) as dbg:
        if args.symbols:
            for sym in args.symbols:
                try:
                    value = dbg.read(sym, signed=args.signed)
                    print(f"{sym} = {value} (0x{value & 0xFFFFFFFF:08x})")
                except KeyError as e:
                    print(f"ERROR: {e}", file=sys.stderr)
        else:
            # Read all debug symbols
            snapshot = dbg.snapshot(prefix=args.prefix or 'dbg_')
            for name, value in sorted(snapshot.items()):
                print(f"{name} = {value}")


def cmd_monitor(args):
    """Continuously monitor debug variables."""
    from stm32_debug import TargetDebugger

    with TargetDebugger(args.elf, target=args.target) as dbg:
        symbols = args.symbols or list(dbg.list_debug_symbols(args.prefix or 'dbg_').keys())

        if not symbols:
            print("No symbols to monitor")
            return 1

        # Print header
        print(f"Monitoring {len(symbols)} variables (interval={args.interval}s, Ctrl+C to stop)")
        print("-" * 80)

        # Truncate long names for display
        max_name_len = 20
        header = "Time   " + "  ".join(f"{s[:max_name_len]:>{max_name_len}}" for s in symbols)
        print(header)
        print("-" * 80)

        prev_values = {s: 0 for s in symbols}
        start_time = time.time()

        try:
            while True:
                dbg.halt()
                values = {}
                for sym in symbols:
                    values[sym] = dbg.read(sym, signed=args.signed)
                dbg.resume()

                elapsed = time.time() - start_time

                if args.delta:
                    # Show delta from previous reading
                    display = []
                    for sym in symbols:
                        delta = values[sym] - prev_values[sym]
                        display.append(f"{delta:>{max_name_len}}")
                        prev_values[sym] = values[sym]
                    row = f"{elapsed:5.1f}s " + "  ".join(display)
                else:
                    row = f"{elapsed:5.1f}s " + "  ".join(
                        f"{values[sym]:>{max_name_len}}" for sym in symbols
                    )

                print(row)
                time.sleep(args.interval)

        except KeyboardInterrupt:
            print("\nStopped")

    return 0


def cmd_audio_test(args):
    """Play test tone and monitor audio counters."""
    from stm32_debug import TargetDebugger, find_usb_audio_device

    # Find USB audio device
    dev_idx, dev_name, out_ch, in_ch = find_usb_audio_device(args.device_pattern)
    if dev_idx is None:
        print(f"ERROR: USB audio device matching '{args.device_pattern}' not found")
        return 1

    print(f"Audio device: {dev_name} (index {dev_idx})")
    print(f"  Output channels: {out_ch}, Input channels: {in_ch}")

    try:
        import sounddevice as sd
        import numpy as np
    except ImportError:
        print("ERROR: sounddevice and numpy required for audio test")
        print("  pip install sounddevice numpy")
        return 1

    # Generate test tone
    duration = args.duration
    sample_rate = 48000
    freq = args.frequency
    n_samples = int(duration * sample_rate)
    t = np.linspace(0, duration, n_samples, endpoint=False)
    amplitude = args.amplitude
    mono = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    stereo = np.column_stack((mono, mono))

    print(f"\nPlaying {freq}Hz sine wave for {duration}s (amplitude={amplitude})...")

    # Audio counter symbols to monitor
    audio_symbols = [
        'dbg_i2s_isr_count',
        'dbg_out_buffered',
        'dbg_streaming',
        'dbg_underrun',
        'dbg_overrun',
        'dbg_pll_ppm',
    ]

    with TargetDebugger(args.elf, target=args.target) as dbg:
        # Filter to only existing symbols
        available = dbg.list_debug_symbols('dbg_')
        symbols = [s for s in audio_symbols if s in available]

        if not symbols:
            print("WARNING: No audio debug symbols found. Using all dbg_* symbols.")
            symbols = list(available.keys())[:8]

        # Read initial values
        dbg.halt()
        initial = {s: dbg.read(s) for s in symbols}
        dbg.resume()

        # Start audio playback
        sd.play(stereo, samplerate=sample_rate, device=dev_idx, blocking=False)
        time.sleep(0.3)  # Let audio start

        # Monitor
        print("\nMonitoring:")
        print("-" * 80)
        header = "Time   " + "  ".join(f"{s.replace('dbg_', ''):>12}" for s in symbols)
        print(header)
        print("-" * 80)

        start_time = time.time()
        interval = 0.5
        iterations = int(duration / interval)

        for _ in range(iterations):
            time.sleep(interval)

            dbg.halt()
            values = {s: dbg.read(s) for s in symbols}
            dbg.resume()

            elapsed = time.time() - start_time

            # Format values (handle signed ppm)
            display = []
            for s in symbols:
                v = values[s]
                if 'ppm' in s and v >= 0x80000000:
                    v -= 0x100000000
                display.append(f"{v:>12}")

            print(f"{elapsed:5.1f}s " + "  ".join(display))

        sd.stop()

        # Final analysis
        dbg.halt()
        final = {s: dbg.read(s) for s in symbols}
        dbg.resume()

        print("-" * 80)
        print("\nSummary:")

        if 'dbg_streaming' in final:
            streaming = final['dbg_streaming']
            print(f"  Streaming: {'YES' if streaming else 'NO'}")

        if 'dbg_underrun' in final and 'dbg_underrun' in initial:
            underruns = final['dbg_underrun'] - initial['dbg_underrun']
            print(f"  Underruns during test: {underruns}")
            if underruns > 0:
                print("  WARNING: Underruns detected - buffer may be too small")

        if 'dbg_overrun' in final and 'dbg_overrun' in initial:
            overruns = final['dbg_overrun'] - initial['dbg_overrun']
            print(f"  Overruns during test: {overruns}")

        if 'dbg_i2s_isr_count' in final and 'dbg_i2s_isr_count' in initial:
            isr_calls = final['dbg_i2s_isr_count'] - initial['dbg_i2s_isr_count']
            expected = duration * 48000 / 32  # Assuming 32 samples per ISR
            print(f"  ISR calls: {isr_calls} (expected ~{expected:.0f})")

    return 0


def cmd_flash(args):
    """Flash firmware to target."""
    from stm32_debug import flash_firmware

    print(f"Flashing {args.elf} to {args.target}...")
    flash_firmware(args.elf, target=args.target, reset_after=not args.no_reset)
    print("Done")
    return 0


def cmd_status(args):
    """Show target status including CPU state, LEDs, and memory regions."""
    from stm32_debug import TargetDebugger
    import struct

    # STM32F4 GPIO registers
    GPIOD_ODR = 0x40020C14  # Output data register
    GPIOD_IDR = 0x40020C10  # Input data register

    # LED pin definitions for STM32F4-Discovery
    LEDS = {
        12: ('Green', 'Running/OK'),
        13: ('Orange', 'Warning'),
        14: ('Red', 'Error'),
        15: ('Blue', 'Startup/Busy'),
    }

    with TargetDebugger(args.elf, target=args.target, auto_resume=False) as dbg:
        was_running = not dbg.target.is_halted()
        dbg.halt()

        print("=" * 60)
        print("Target Status")
        print("=" * 60)

        # CPU registers
        pc = dbg.target.read_core_register('pc')
        sp = dbg.target.read_core_register('sp')
        lr = dbg.target.read_core_register('lr')
        print(f"\nCPU Registers:")
        print(f"  PC = 0x{pc:08X}")
        print(f"  SP = 0x{sp:08X}")
        print(f"  LR = 0x{lr:08X}")

        # LED status
        odr = dbg.target.read32(GPIOD_ODR)
        print(f"\nLED Status (GPIOD ODR=0x{odr:08X}):")
        for pin, (color, meaning) in LEDS.items():
            state = "ON" if odr & (1 << pin) else "OFF"
            marker = "●" if odr & (1 << pin) else "○"
            print(f"  {marker} {color:6} (PD{pin}): {state:3} - {meaning}")

        # SharedMemory if available (kernel specific)
        if args.shared_mem:
            try:
                addr = int(args.shared_mem, 0)
                sample_rate = dbg.target.read32(addr)
                buffer_size = dbg.target.read32(addr + 4)
                sample_pos_lo = dbg.target.read32(addr + 8)
                sample_pos_hi = dbg.target.read32(addr + 12)
                sample_pos = sample_pos_lo | (sample_pos_hi << 32)

                print(f"\nSharedMemory @ 0x{addr:08X}:")
                print(f"  sample_rate:    {sample_rate}")
                print(f"  buffer_size:    {buffer_size}")
                print(f"  sample_position: {sample_pos}")
            except Exception as e:
                print(f"\nSharedMemory read failed: {e}")

        # Debug symbols summary
        dbg_symbols = dbg.list_debug_symbols('dbg_')
        if dbg_symbols:
            print(f"\nDebug Variables ({len(dbg_symbols)} symbols):")
            # Read key counters if they exist
            key_vars = ['dbg_streaming', 'dbg_underrun', 'dbg_overrun',
                        'dbg_i2s_isr_count', 'dbg_out_buffered']
            for var in key_vars:
                if var in dbg_symbols:
                    val = dbg.target.read32(dbg_symbols[var])
                    print(f"  {var}: {val}")

        # Memory regions
        if args.memory:
            print(f"\nMemory Dump:")
            for region in args.memory:
                try:
                    addr, size = region.split(':')
                    addr = int(addr, 0)
                    size = int(size)
                    data = dbg.target.read_memory_block8(addr, size)
                    print(f"  0x{addr:08X}: {bytes(data).hex()}")
                except Exception as e:
                    print(f"  Error reading {region}: {e}")

        if was_running and not args.halt:
            dbg.resume()
            print(f"\nTarget resumed")
        else:
            print(f"\nTarget halted")

    return 0


def main():
    parser = argparse.ArgumentParser(
        description='UMI Debug Tool - STM32 USB Audio debugging',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('--target', '-t', default='stm32f407vg',
                        help='PyOCD target override (default: stm32f407vg)')

    subparsers = parser.add_subparsers(dest='command', required=True)

    # symbols command
    p_symbols = subparsers.add_parser('symbols', help='List debug symbols')
    p_symbols.add_argument('elf', help='Path to ELF file')
    p_symbols.add_argument('--prefix', '-p', default='dbg_',
                           help='Symbol prefix filter (default: dbg_)')

    # read command
    p_read = subparsers.add_parser('read', help='Read debug variable(s)')
    p_read.add_argument('elf', help='Path to ELF file')
    p_read.add_argument('symbols', nargs='*', help='Symbol name(s) to read')
    p_read.add_argument('--prefix', '-p', default='dbg_',
                        help='Symbol prefix for listing all (default: dbg_)')
    p_read.add_argument('--signed', '-s', action='store_true',
                        help='Interpret as signed integer')

    # monitor command
    p_monitor = subparsers.add_parser('monitor', help='Monitor debug variables')
    p_monitor.add_argument('elf', help='Path to ELF file')
    p_monitor.add_argument('symbols', nargs='*', help='Symbol name(s) to monitor')
    p_monitor.add_argument('--prefix', '-p', default='dbg_',
                           help='Symbol prefix (default: dbg_)')
    p_monitor.add_argument('--interval', '-i', type=float, default=0.5,
                           help='Update interval in seconds (default: 0.5)')
    p_monitor.add_argument('--delta', '-d', action='store_true',
                           help='Show delta from previous reading')
    p_monitor.add_argument('--signed', '-s', action='store_true',
                           help='Interpret as signed integer')

    # audio-test command
    p_audio = subparsers.add_parser('audio-test', help='Test USB audio with monitoring')
    p_audio.add_argument('elf', help='Path to ELF file')
    p_audio.add_argument('--device-pattern', '-D', default='UMI',
                         help='USB audio device name pattern (default: UMI)')
    p_audio.add_argument('--frequency', '-f', type=float, default=440,
                         help='Test tone frequency in Hz (default: 440)')
    p_audio.add_argument('--duration', '-d', type=float, default=5,
                         help='Test duration in seconds (default: 5)')
    p_audio.add_argument('--amplitude', '-a', type=float, default=0.3,
                         help='Test tone amplitude 0-1 (default: 0.3)')

    # flash command
    p_flash = subparsers.add_parser('flash', help='Flash firmware to target')
    p_flash.add_argument('elf', help='Path to ELF file')
    p_flash.add_argument('--no-reset', action='store_true',
                         help='Do not reset after flashing')

    # status command
    p_status = subparsers.add_parser('status', help='Show target status')
    p_status.add_argument('elf', help='Path to ELF file')
    p_status.add_argument('--shared-mem', '-S', metavar='ADDR',
                          help='SharedMemory address (e.g., 0x20018000)')
    p_status.add_argument('--memory', '-m', action='append', metavar='ADDR:SIZE',
                          help='Memory region to dump (e.g., 0x08060000:16)')
    p_status.add_argument('--halt', action='store_true',
                          help='Leave target halted after reading')

    args = parser.parse_args()

    commands = {
        'symbols': cmd_symbols,
        'read': cmd_read,
        'monitor': cmd_monitor,
        'audio-test': cmd_audio_test,
        'flash': cmd_flash,
        'status': cmd_status,
    }

    return commands[args.command](args)


if __name__ == '__main__':
    sys.exit(main() or 0)
