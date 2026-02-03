#!/usr/bin/env python3
"""
Audio Bridge for Renode I2S Peripheral

This script receives audio samples from Renode via Unix socket
and plays them through the host audio system using PyAudio.

Usage:
    python3 audio_bridge.py [--sample-rate 48000] [--channels 2]

Requirements:
    pip install pyaudio

The bridge creates a Unix socket at /tmp/renode_audio.sock that the
Renode I2S peripheral connects to. Audio samples are received as
16-bit signed integers and played in real-time.
"""

import argparse
import os
import socket
import struct
import sys
import threading
import queue
from typing import Optional

# Try to import PyAudio, fall back to sounddevice if available
try:
    import pyaudio
    AUDIO_BACKEND = 'pyaudio'
except ImportError:
    try:
        import sounddevice as sd
        import numpy as np
        AUDIO_BACKEND = 'sounddevice'
    except ImportError:
        AUDIO_BACKEND = None
        print("Warning: No audio backend available. Install pyaudio or sounddevice.")
        print("  pip install pyaudio")
        print("  pip install sounddevice")

class AudioPlayer:
    """Plays audio samples received from Renode"""

    def __init__(self, sample_rate: int = 48000, channels: int = 2,
                 buffer_size: int = 256):
        self.sample_rate = sample_rate
        self.channels = channels
        self.buffer_size = buffer_size
        self.audio_queue: queue.Queue = queue.Queue(maxsize=32)
        self.running = False
        self.stream = None
        self.audio = None

        if AUDIO_BACKEND == 'pyaudio':
            self._init_pyaudio()
        elif AUDIO_BACKEND == 'sounddevice':
            self._init_sounddevice()

    def _init_pyaudio(self):
        """Initialize PyAudio backend"""
        self.audio = pyaudio.PyAudio()
        self.stream = self.audio.open(
            format=pyaudio.paInt16,
            channels=self.channels,
            rate=self.sample_rate,
            output=True,
            frames_per_buffer=self.buffer_size,
            stream_callback=self._pyaudio_callback
        )

    def _init_sounddevice(self):
        """Initialize sounddevice backend"""
        def callback(outdata, frames, time, status):
            if status:
                print(f"Audio status: {status}")
            try:
                data = self.audio_queue.get_nowait()
                # Convert to numpy array
                samples = np.frombuffer(data, dtype=np.int16)
                samples = samples.reshape(-1, self.channels)
                # Normalize to float32
                outdata[:len(samples)] = samples.astype(np.float32) / 32768.0
                if len(samples) < frames:
                    outdata[len(samples):] = 0
            except queue.Empty:
                outdata[:] = 0

        self.stream = sd.OutputStream(
            samplerate=self.sample_rate,
            channels=self.channels,
            dtype=np.float32,
            blocksize=self.buffer_size,
            callback=callback
        )

    def _pyaudio_callback(self, in_data, frame_count, time_info, status):
        """PyAudio stream callback"""
        try:
            data = self.audio_queue.get_nowait()
            return (data, pyaudio.paContinue)
        except queue.Empty:
            # Return silence if no data
            silence = bytes(frame_count * self.channels * 2)
            return (silence, pyaudio.paContinue)

    def start(self):
        """Start audio playback"""
        self.running = True
        if self.stream:
            if AUDIO_BACKEND == 'pyaudio':
                self.stream.start_stream()
            else:
                self.stream.start()
        print(f"Audio player started: {self.sample_rate}Hz, {self.channels}ch")

    def stop(self):
        """Stop audio playback"""
        self.running = False
        if self.stream:
            if AUDIO_BACKEND == 'pyaudio':
                self.stream.stop_stream()
                self.stream.close()
            else:
                self.stream.stop()
                self.stream.close()
        if self.audio:
            self.audio.terminate()

    def play(self, data: bytes):
        """Queue audio data for playback"""
        try:
            self.audio_queue.put_nowait(data)
        except queue.Full:
            # Drop oldest buffer if queue is full (prevents latency buildup)
            try:
                self.audio_queue.get_nowait()
                self.audio_queue.put_nowait(data)
            except:
                pass


class AudioBridge:
    """Bridge between Renode I2S peripheral and host audio"""

    def __init__(self, socket_path: str = "/tmp/renode_audio.sock",
                 sample_rate: int = 48000, channels: int = 2):
        self.socket_path = socket_path
        self.sample_rate = sample_rate
        self.channels = channels
        self.server_socket: Optional[socket.socket] = None
        self.client_socket: Optional[socket.socket] = None
        self.running = False
        self.player: Optional[AudioPlayer] = None

        # Statistics
        self.bytes_received = 0
        self.samples_played = 0
        self.buffer_underruns = 0

    def start(self):
        """Start the audio bridge server"""
        # Remove existing socket file
        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)

        # Create Unix socket server
        self.server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server_socket.bind(self.socket_path)
        self.server_socket.listen(1)
        self.server_socket.settimeout(1.0)

        # Start audio player
        if AUDIO_BACKEND:
            self.player = AudioPlayer(self.sample_rate, self.channels)
            self.player.start()

        self.running = True
        print(f"Audio bridge listening on {self.socket_path}")
        print("Waiting for Renode connection...")

        # Accept connections
        while self.running:
            try:
                self.client_socket, addr = self.server_socket.accept()
                print("Renode connected!")
                self._handle_client()
            except socket.timeout:
                continue
            except KeyboardInterrupt:
                break

    def _handle_client(self):
        """Handle connected Renode client"""
        buffer_size = 256 * self.channels * 2  # samples * channels * bytes

        while self.running and self.client_socket:
            try:
                data = self.client_socket.recv(buffer_size)
                if not data:
                    print("Renode disconnected")
                    break

                self.bytes_received += len(data)
                samples = len(data) // 2
                self.samples_played += samples

                # Play audio
                if self.player:
                    self.player.play(data)

                # Print stats periodically
                if self.samples_played % (self.sample_rate * self.channels) == 0:
                    seconds = self.samples_played / (self.sample_rate * self.channels)
                    print(f"Played {seconds:.1f}s of audio ({self.bytes_received} bytes)")

            except Exception as e:
                print(f"Error receiving data: {e}")
                break

        if self.client_socket:
            self.client_socket.close()
            self.client_socket = None

    def stop(self):
        """Stop the audio bridge"""
        self.running = False

        if self.player:
            self.player.stop()

        if self.client_socket:
            self.client_socket.close()

        if self.server_socket:
            self.server_socket.close()

        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)

        print(f"\nAudio bridge stopped. Total: {self.bytes_received} bytes, "
              f"{self.samples_played} samples")


def main():
    parser = argparse.ArgumentParser(
        description='Audio bridge for Renode I2S peripheral')
    parser.add_argument('--sample-rate', type=int, default=48000,
                        help='Sample rate in Hz (default: 48000)')
    parser.add_argument('--channels', type=int, default=2,
                        help='Number of channels (default: 2)')
    parser.add_argument('--socket', type=str, default='/tmp/renode_audio.sock',
                        help='Unix socket path')
    args = parser.parse_args()

    bridge = AudioBridge(
        socket_path=args.socket,
        sample_rate=args.sample_rate,
        channels=args.channels
    )

    try:
        bridge.start()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        bridge.stop()


if __name__ == '__main__':
    main()
