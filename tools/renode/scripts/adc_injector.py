#!/usr/bin/env python3
"""
ADC Value Injector for Renode

This script allows injecting analog values into Renode's simulated ADC,
enabling testing of analog inputs like knobs, CV inputs, and sensors.

Usage:
    python3 adc_injector.py [--socket /tmp/renode_ctrl.sock]

The script provides:
1. Interactive command line for setting ADC values
2. LFO generators for automated testing
3. External control via socket for integration with other tools

Commands:
    set <channel> <voltage>  - Set ADC channel to voltage (0-3.3V)
    lfo <channel> <freq> <min> <max>  - Start LFO on channel
    stop <channel>           - Stop LFO on channel
    noise <channel> <amplitude>  - Add noise to channel
    quit                     - Exit

Example:
    set 0 1.65              # Set channel 0 to 1.65V (mid-point)
    lfo 1 0.5 0.0 3.3       # 0.5Hz LFO on channel 1, 0-3.3V range
"""

import argparse
import os
import socket
import struct
import sys
import threading
import time
import math
from typing import Dict, Optional, Callable

# Renode RPC protocol
CMD_SET_ADC = 0x01
CMD_GET_ADC = 0x02
CMD_SET_GPIO = 0x03
CMD_GET_GPIO = 0x04

class LFO:
    """Low Frequency Oscillator for automated ADC modulation"""

    def __init__(self, frequency: float, min_val: float, max_val: float,
                 waveform: str = 'sine'):
        self.frequency = frequency
        self.min_val = min_val
        self.max_val = max_val
        self.waveform = waveform
        self.phase = 0.0
        self.running = True

    def get_value(self, dt: float) -> float:
        """Get current LFO value and advance phase"""
        self.phase += self.frequency * dt * 2 * math.pi
        if self.phase > 2 * math.pi:
            self.phase -= 2 * math.pi

        # Generate waveform
        if self.waveform == 'sine':
            normalized = (math.sin(self.phase) + 1) / 2
        elif self.waveform == 'triangle':
            normalized = abs((self.phase / math.pi) - 1)
        elif self.waveform == 'square':
            normalized = 1.0 if self.phase < math.pi else 0.0
        elif self.waveform == 'saw':
            normalized = self.phase / (2 * math.pi)
        else:
            normalized = 0.5

        return self.min_val + normalized * (self.max_val - self.min_val)


class ADCInjector:
    """Injects ADC values into Renode simulation"""

    def __init__(self, renode_socket: str = "/tmp/renode_ctrl.sock"):
        self.renode_socket = renode_socket
        self.socket: Optional[socket.socket] = None
        self.running = False

        # ADC state
        self.num_channels = 16
        self.adc_values: Dict[int, float] = {i: 0.0 for i in range(self.num_channels)}
        self.adc_noise: Dict[int, float] = {i: 0.0 for i in range(self.num_channels)}
        self.lfos: Dict[int, LFO] = {}

        # ADC parameters
        self.vref = 3.3  # Reference voltage
        self.resolution = 12  # 12-bit ADC
        self.max_value = (1 << self.resolution) - 1

        # Update thread
        self.update_thread: Optional[threading.Thread] = None
        self.update_rate = 100  # Hz

    def connect(self) -> bool:
        """Connect to Renode control socket"""
        try:
            if not os.path.exists(self.renode_socket):
                print(f"Renode socket not found: {self.renode_socket}")
                print("Make sure Renode is running with control socket enabled")
                return False

            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.connect(self.renode_socket)
            print(f"Connected to Renode at {self.renode_socket}")
            return True
        except Exception as e:
            print(f"Failed to connect to Renode: {e}")
            self.socket = None
            return False

    def voltage_to_adc(self, voltage: float) -> int:
        """Convert voltage to ADC value"""
        voltage = max(0.0, min(self.vref, voltage))
        return int((voltage / self.vref) * self.max_value)

    def adc_to_voltage(self, value: int) -> float:
        """Convert ADC value to voltage"""
        return (value / self.max_value) * self.vref

    def set_channel(self, channel: int, voltage: float):
        """Set ADC channel to specified voltage"""
        if 0 <= channel < self.num_channels:
            self.adc_values[channel] = voltage
            self._send_adc_value(channel)
            print(f"ADC{channel} = {voltage:.3f}V ({self.voltage_to_adc(voltage)})")

    def _send_adc_value(self, channel: int):
        """Send ADC value to Renode"""
        voltage = self.adc_values[channel]

        # Add noise if configured
        noise = self.adc_noise.get(channel, 0.0)
        if noise > 0:
            import random
            voltage += random.gauss(0, noise)
            voltage = max(0.0, min(self.vref, voltage))

        adc_value = self.voltage_to_adc(voltage)

        if self.socket:
            try:
                # Protocol: CMD(1) + Channel(1) + Value(2)
                data = struct.pack('<BBH', CMD_SET_ADC, channel, adc_value)
                self.socket.send(data)
            except Exception as e:
                print(f"Failed to send ADC value: {e}")

    def start_lfo(self, channel: int, frequency: float, min_val: float,
                  max_val: float, waveform: str = 'sine'):
        """Start LFO modulation on channel"""
        self.lfos[channel] = LFO(frequency, min_val, max_val, waveform)
        print(f"LFO started on ADC{channel}: {frequency}Hz, {min_val}-{max_val}V, {waveform}")

    def stop_lfo(self, channel: int):
        """Stop LFO on channel"""
        if channel in self.lfos:
            del self.lfos[channel]
            print(f"LFO stopped on ADC{channel}")

    def set_noise(self, channel: int, amplitude: float):
        """Set noise amplitude for channel (in volts)"""
        self.adc_noise[channel] = amplitude
        print(f"Noise set on ADC{channel}: ±{amplitude:.3f}V")

    def _update_loop(self):
        """Background thread for LFO updates"""
        dt = 1.0 / self.update_rate
        last_time = time.time()

        while self.running:
            current_time = time.time()
            actual_dt = current_time - last_time
            last_time = current_time

            # Update all LFOs
            for channel, lfo in list(self.lfos.items()):
                if lfo.running:
                    voltage = lfo.get_value(actual_dt)
                    self.adc_values[channel] = voltage
                    self._send_adc_value(channel)

            time.sleep(dt)

    def start(self):
        """Start the update thread"""
        self.running = True
        self.update_thread = threading.Thread(target=self._update_loop, daemon=True)
        self.update_thread.start()

    def stop(self):
        """Stop the update thread"""
        self.running = False
        if self.update_thread:
            self.update_thread.join(timeout=1.0)
        if self.socket:
            self.socket.close()


def interactive_mode(injector: ADCInjector):
    """Interactive command line mode"""
    print("\nADC Injector Interactive Mode")
    print("Commands:")
    print("  set <ch> <voltage>           - Set channel voltage (0-3.3V)")
    print("  lfo <ch> <freq> <min> <max>  - Start LFO")
    print("  stop <ch>                    - Stop LFO")
    print("  noise <ch> <amplitude>       - Set noise level")
    print("  status                       - Show all channels")
    print("  quit                         - Exit")
    print()

    while True:
        try:
            line = input("adc> ").strip()
            if not line:
                continue

            parts = line.split()
            cmd = parts[0].lower()

            if cmd == 'quit' or cmd == 'q':
                break

            elif cmd == 'set' and len(parts) >= 3:
                channel = int(parts[1])
                voltage = float(parts[2])
                injector.set_channel(channel, voltage)

            elif cmd == 'lfo' and len(parts) >= 5:
                channel = int(parts[1])
                freq = float(parts[2])
                min_v = float(parts[3])
                max_v = float(parts[4])
                waveform = parts[5] if len(parts) > 5 else 'sine'
                injector.start_lfo(channel, freq, min_v, max_v, waveform)

            elif cmd == 'stop' and len(parts) >= 2:
                channel = int(parts[1])
                injector.stop_lfo(channel)

            elif cmd == 'noise' and len(parts) >= 3:
                channel = int(parts[1])
                amplitude = float(parts[2])
                injector.set_noise(channel, amplitude)

            elif cmd == 'status':
                print("\nADC Channel Status:")
                for ch in range(injector.num_channels):
                    v = injector.adc_values[ch]
                    lfo_info = f" [LFO {injector.lfos[ch].frequency}Hz]" if ch in injector.lfos else ""
                    noise_info = f" [±{injector.adc_noise[ch]:.2f}V noise]" if injector.adc_noise.get(ch, 0) > 0 else ""
                    print(f"  ADC{ch:2d}: {v:5.3f}V ({injector.voltage_to_adc(v):4d}){lfo_info}{noise_info}")
                print()

            else:
                print(f"Unknown command: {cmd}")

        except ValueError as e:
            print(f"Invalid value: {e}")
        except KeyboardInterrupt:
            print()
            break
        except EOFError:
            break


def main():
    parser = argparse.ArgumentParser(description='ADC Value Injector for Renode')
    parser.add_argument('--socket', type=str, default='/tmp/renode_ctrl.sock',
                        help='Renode control socket path')
    parser.add_argument('--demo', action='store_true',
                        help='Run demo mode with simulated values')
    args = parser.parse_args()

    injector = ADCInjector(args.socket)

    if not args.demo:
        if not injector.connect():
            print("\nRunning in demo mode (no Renode connection)")

    injector.start()

    try:
        interactive_mode(injector)
    finally:
        injector.stop()


if __name__ == '__main__':
    main()
