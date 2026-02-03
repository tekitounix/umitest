#!/usr/bin/env python3
"""
Renode Audio Web Bridge - Full I/O Bridge

Bridges all I/O between Renode simulation and browser:
- Audio (I2S) output: Renode -> Browser (for playback)
- MIDI input: Browser -> Renode (for control)
- MIDI output: Renode -> Browser (for feedback)
- Status/State: Bidirectional (params, kernel state, etc.)

Architecture:
  ┌─────────────────────────────────────────────────────────────────┐
  │                         Browser                                  │
  │  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │
  │  │ synth_sim   │  │  WebAudio    │  │      WebMIDI           │  │
  │  │ .html (UI)  │  │  API         │  │                        │  │
  │  └──────┬──────┘  └──────┬───────┘  └────────────┬───────────┘  │
  │         └────────────────┼───────────────────────┘              │
  │                          │ WebSocket (:8089)                    │
  └──────────────────────────┼──────────────────────────────────────┘
                             │
  ┌──────────────────────────┼──────────────────────────────────────┐
  │                          │                                      │
  │           ┌──────────────▼──────────────┐                       │
  │           │     Web Bridge Server       │                       │
  │           │     (this script)           │                       │
  │           └──────────────┬──────────────┘                       │
  │                          │                                      │
  │    ┌─────────────────────┼─────────────────────┐                │
  │    │ TCP :9001           │ TCP :9002           │ TCP :9003      │
  │    │ Audio               │ MIDI                │ Control        │
  │    ▼                     ▼                     ▼                │
  │  ┌────────────┐   ┌────────────┐       ┌────────────┐           │
  │  │ I2S Periph │   │ MIDI Periph│       │ Ctrl Periph│           │
  │  │ (Python)   │   │ (Python)   │       │ (Python)   │           │
  │  └─────┬──────┘   └─────┬──────┘       └─────┬──────┘           │
  │        └────────────────┼────────────────────┘                  │
  │                         │                                       │
  │              ┌──────────▼───────────┐                           │
  │              │       Renode         │                           │
  │              │  (STM32 Emulation)   │                           │
  │              └──────────────────────┘                           │
  │                    Host System                                  │
  └─────────────────────────────────────────────────────────────────┘

Protocol (TCP from Renode peripherals):
- Audio v1: [AUD][ver:1][count:u16][reserved:u16][timestamp:u64][samples:i16[]]
- MIDI TX:  [MID\0][len:u16][reserved:u16][midi_bytes[]]
- Status:   [STA\0][json_len:u16][reserved:u16][json_utf8[]]

Protocol (WebSocket to browser):
- Audio:   Binary: [0x01][pad][count:u16][timestamp:u64][samples:f32[]]
- MIDI:    JSON: {"type":"midi","data":[status,d1,d2],"timestamp":ms}
- Status:  JSON: {"type":"status",...}

Usage:
    # Terminal 1: Start Web Bridge
    python3 renode/scripts/web_bridge.py

    # Terminal 2: Start Renode
    renode --console --disable-xwt -e "i $CWD/renode/synth_audio.resc"

    # Browser: Open
    http://localhost:8088/workbench/synth_sim.html?backend=renode
"""

import asyncio
import json
import struct
import sys
import array
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, Set

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

try:
    import websockets
except ImportError:
    print("Install websockets: pip install websockets")
    sys.exit(1)

try:
    from aiohttp import web
except ImportError:
    print("Install aiohttp: pip install aiohttp")
    sys.exit(1)


# Configuration
@dataclass
class Config:
    http_port: int = 8088
    ws_port: int = 8089
    audio_port: int = 9001     # TCP port for I2S audio from Renode
    midi_port: int = 9002      # TCP port for MIDI from/to Renode
    control_port: int = 9003   # TCP port for control/status
    static_dir: Path = field(default_factory=lambda: Path(__file__).parent.parent.parent / "examples")
    sample_rate: int = 48000
    channels: int = 2


@dataclass
class State:
    """Bridge state"""
    audio_connected: bool = False
    midi_connected: bool = False
    control_connected: bool = False
    sample_count: int = 0
    buffer_count: int = 0
    midi_rx_count: int = 0
    midi_tx_count: int = 0

    # Kernel state (from Renode)
    uptime: int = 0
    cpu_load: float = 0.0
    task_count: int = 0
    heap_used: int = 0
    heap_total: int = 0
    audio_running: bool = False


class WebBridge:
    def __init__(self, config: Config):
        self.config = config
        self.state = State()
        self.ws_clients: Set[websockets.WebSocketServerProtocol] = set()
        self.running = False

        # Client connections from Renode
        self.audio_writer: Optional[asyncio.StreamWriter] = None
        self.midi_writer: Optional[asyncio.StreamWriter] = None
        self.control_writer: Optional[asyncio.StreamWriter] = None

    # ===== WebSocket Handlers =====

    async def ws_handler(self, websocket):
        """Handle WebSocket connection from browser"""
        addr = websocket.remote_address
        print(f"[WS] Client connected: {addr}")
        self.ws_clients.add(websocket)

        # Send initial status
        await self.send_status(websocket)

        try:
            async for message in websocket:
                await self.handle_ws_message(websocket, message)
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            self.ws_clients.discard(websocket)
            print(f"[WS] Client disconnected: {addr}")

    async def handle_ws_message(self, websocket, message):
        """Handle message from browser"""
        try:
            if isinstance(message, bytes):
                await self.handle_ws_binary(message)
            else:
                data = json.loads(message)
                await self.handle_ws_json(data)
        except Exception as e:
            print(f"[WS] Message error: {e}")

    async def handle_ws_binary(self, data: bytes):
        """Handle binary message from browser"""
        if len(data) < 1:
            return

        msg_type = data[0]
        if msg_type == 0x10:  # MIDI from browser
            midi_bytes = data[1:]
            await self.send_midi_to_renode(midi_bytes)

    async def handle_ws_json(self, data: dict):
        """Handle JSON message from browser"""
        msg_type = data.get('type')

        if msg_type == 'midi':
            # MIDI message from browser
            midi_bytes = bytes(data.get('data', []))
            await self.send_midi_to_renode(midi_bytes)
            self.state.midi_rx_count += 1

        elif msg_type == 'note-on':
            # Note on from browser keyboard/MIDI
            note = data.get('note', 60)
            velocity = data.get('velocity', 100)
            channel = data.get('channel', 0)
            midi_bytes = bytes([0x90 | channel, note, velocity])
            await self.send_midi_to_renode(midi_bytes)

        elif msg_type == 'note-off':
            # Note off
            note = data.get('note', 60)
            channel = data.get('channel', 0)
            midi_bytes = bytes([0x80 | channel, note, 0])
            await self.send_midi_to_renode(midi_bytes)

        elif msg_type == 'set-param':
            # Parameter change
            param_id = data.get('id', 0)
            value = data.get('value', 0)
            await self.send_param_to_renode(param_id, value)

        elif msg_type == 'get-status':
            # Request status
            for ws in self.ws_clients:
                await self.send_status(ws)

        elif msg_type == 'start':
            # Start audio
            await self.send_control_to_renode('start', 1)

        elif msg_type == 'stop':
            # Stop audio
            await self.send_control_to_renode('stop', 1)

    async def send_status(self, websocket):
        """Send current status to a client"""
        status = {
            'type': 'status',
            'backend': 'renode',
            'audioConnected': self.state.audio_connected,
            'midiConnected': self.state.midi_connected,
            'controlConnected': self.state.control_connected,
            'sampleCount': self.state.sample_count,
            'bufferCount': self.state.buffer_count,
            'midiRx': self.state.midi_rx_count,
            'midiTx': self.state.midi_tx_count,
            'sampleRate': self.config.sample_rate,
            'uptime': self.state.uptime,
            'cpuLoad': self.state.cpu_load,
            'taskCount': self.state.task_count,
            'heapUsed': self.state.heap_used,
            'heapTotal': self.state.heap_total,
            'audioRunning': self.state.audio_running,
        }
        try:
            await websocket.send(json.dumps(status))
        except:
            pass

    async def broadcast_json(self, data: dict):
        """Broadcast JSON to all WebSocket clients"""
        if not self.ws_clients:
            return
        msg = json.dumps(data)
        dead = set()
        for ws in self.ws_clients:
            try:
                await ws.send(msg)
            except:
                dead.add(ws)
        self.ws_clients -= dead

    async def broadcast_binary(self, data: bytes):
        """Broadcast binary to all WebSocket clients (non-blocking)"""
        if not self.ws_clients:
            return
        # Fire and forget - don't wait for send to complete
        for ws in self.ws_clients:
            asyncio.create_task(self._send_ws_binary(ws, data))

    async def _send_ws_binary(self, ws, data: bytes):
        """Send binary to a single WebSocket client"""
        try:
            await ws.send(data)
        except:
            self.ws_clients.discard(ws)

    # ===== Renode TCP Handlers =====

    async def handle_audio_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Handle TCP connection from Renode I2S peripheral"""
        addr = writer.get_extra_info('peername')
        print(f"[Audio] Renode connected: {addr}")
        self.state.audio_connected = True
        self.audio_writer = writer

        try:
            while True:
                # Read first 4 bytes to detect version
                header_start = await reader.readexactly(4)
                if header_start[:3] != b'AUD':
                    print(f"[Audio] Invalid header: {header_start[:3]!r}")
                    continue

                version = header_start[3]

                if version == 1:
                    # Version 1: 16-byte header with timestamp
                    # [AUD][ver:1][count:u16][reserved:u16][timestamp:u64]
                    header_rest = await reader.readexactly(12)
                    sample_count = struct.unpack('<H', header_rest[0:2])[0]
                    timestamp_us = struct.unpack('<Q', header_rest[4:12])[0]
                else:
                    # Version 0: 8-byte header (legacy)
                    header_rest = await reader.readexactly(4)
                    sample_count = struct.unpack('<H', header_rest[0:2])[0]
                    timestamp_us = 0

                # Read samples (16-bit signed) directly as bytes
                data = await reader.readexactly(sample_count * 2)

                self.state.sample_count += sample_count
                self.state.buffer_count += 1

                # Send immediately to browser with timestamp
                if self.ws_clients:
                    await self._send_audio_batch(data, timestamp_us)

        except asyncio.IncompleteReadError:
            print("[Audio] Renode disconnected")
        except Exception as e:
            print(f"[Audio] Error: {e}")
        finally:
            self.state.audio_connected = False
            self.audio_writer = None
            writer.close()

    async def _send_audio_batch(self, data: bytearray, timestamp_us: int = 0):
        """Convert int16 samples to float32 and send to browsers efficiently"""
        if HAS_NUMPY:
            # Fast path with NumPy
            samples_i16 = np.frombuffer(data, dtype=np.int16)
            samples_f32 = (samples_i16.astype(np.float32) / 32768.0)
            count = len(samples_f32)
            # Binary format: [0x01][pad][count:u16][timestamp:u64][samples:f32[]]
            # 12-byte header + sample data
            header = struct.pack('<BBH', 0x01, 0x00, count)
            timestamp_bytes = struct.pack('<Q', timestamp_us)
            msg = header + timestamp_bytes + samples_f32.tobytes()
        else:
            # Fallback without NumPy
            samples = array.array('h')
            samples.frombytes(data)
            count = len(samples)
            float_samples = array.array('f', (s / 32768.0 for s in samples))
            header = struct.pack('<BBH', 0x01, 0x00, count)
            timestamp_bytes = struct.pack('<Q', timestamp_us)
            msg = header + timestamp_bytes + float_samples.tobytes()

        await self.broadcast_binary(msg)

    async def handle_midi_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Handle TCP connection from Renode MIDI peripheral"""
        addr = writer.get_extra_info('peername')
        print(f"[MIDI] Renode connected: {addr}")
        self.state.midi_connected = True
        self.midi_writer = writer

        try:
            while True:
                # Read header: 'MID\x00' + len (u16) + reserved (u16)
                header = await reader.readexactly(8)
                if header[:4] != b'MID\x00':
                    continue

                length = struct.unpack('<H', header[4:6])[0]
                if length == 0:
                    continue

                # Read MIDI bytes
                midi_data = await reader.readexactly(length)

                # Forward to browser
                self.state.midi_tx_count += 1
                await self.broadcast_json({
                    'type': 'midi',
                    'data': list(midi_data),
                    'direction': 'tx'
                })

        except asyncio.IncompleteReadError:
            print("[MIDI] Renode disconnected")
        except Exception as e:
            print(f"[MIDI] Error: {e}")
        finally:
            self.state.midi_connected = False
            self.midi_writer = None
            writer.close()

    async def handle_control_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Handle TCP connection from Renode control peripheral"""
        addr = writer.get_extra_info('peername')
        print(f"[Control] Renode connected: {addr}")
        self.state.control_connected = True
        self.control_writer = writer

        try:
            while True:
                # Read header: 'STA\x00' + json_len (u16) + reserved (u16)
                header = await reader.readexactly(8)
                if header[:4] != b'STA\x00':
                    continue

                length = struct.unpack('<H', header[4:6])[0]
                if length == 0:
                    continue

                # Read JSON
                json_data = await reader.readexactly(length)
                status = json.loads(json_data.decode('utf-8'))

                # Update state
                self.state.uptime = status.get('uptime', 0)
                self.state.cpu_load = status.get('cpuLoad', 0)
                self.state.task_count = status.get('taskCount', 0)
                self.state.heap_used = status.get('heapUsed', 0)
                self.state.heap_total = status.get('heapTotal', 0)
                self.state.audio_running = status.get('audioRunning', False)

                # Forward to browsers
                status['type'] = 'kernel-state'
                await self.broadcast_json(status)

        except asyncio.IncompleteReadError:
            print("[Control] Renode disconnected")
        except Exception as e:
            print(f"[Control] Error: {e}")
        finally:
            self.state.control_connected = False
            self.control_writer = None
            writer.close()

    async def send_midi_to_renode(self, midi_bytes: bytes):
        """Send MIDI to Renode"""
        if self.midi_writer:
            try:
                # Protocol: 'MID\x00' + len (u16) + reserved (u16) + data
                header = b'MID\x00' + struct.pack('<HH', len(midi_bytes), 0)
                self.midi_writer.write(header + midi_bytes)
                await self.midi_writer.drain()
            except Exception as e:
                print(f"[MIDI] Send error: {e}")

    async def send_param_to_renode(self, param_id: int, value: float):
        """Send parameter change to Renode"""
        if self.control_writer:
            try:
                msg = json.dumps({'type': 'set-param', 'id': param_id, 'value': value})
                msg_bytes = msg.encode('utf-8')
                header = b'CMD\x00' + struct.pack('<HH', len(msg_bytes), 0)
                self.control_writer.write(header + msg_bytes)
                await self.control_writer.drain()
            except Exception as e:
                print(f"[Control] Send error: {e}")

    async def send_control_to_renode(self, cmd: str, value: int):
        """Send control command to Renode"""
        if self.control_writer:
            try:
                msg = json.dumps({'type': cmd, 'value': value})
                msg_bytes = msg.encode('utf-8')
                header = b'CMD\x00' + struct.pack('<HH', len(msg_bytes), 0)
                self.control_writer.write(header + msg_bytes)
                await self.control_writer.drain()
            except Exception as e:
                print(f"[Control] Send error: {e}")

    # ===== HTTP Server =====

    def create_http_app(self):
        """Create HTTP server for static files"""
        app = web.Application()

        async def status_handler(request):
            return web.json_response({
                'audioConnected': self.state.audio_connected,
                'midiConnected': self.state.midi_connected,
                'controlConnected': self.state.control_connected,
                'sampleCount': self.state.sample_count,
                'bufferCount': self.state.buffer_count,
                'wsClients': len(self.ws_clients),
            })

        app.router.add_get('/api/status', status_handler)
        app.router.add_static('/', self.config.static_dir, show_index=True, follow_symlinks=True)
        return app

    # ===== Main =====

    async def start(self):
        """Start all servers"""
        self.running = True

        print("=" * 60)
        print("  Renode Audio Web Bridge")
        print("=" * 60)

        # Start TCP servers for Renode peripherals
        audio_server = await asyncio.start_server(
            self.handle_audio_client, '127.0.0.1', self.config.audio_port)
        print(f"[Audio] TCP server on port {self.config.audio_port}")

        midi_server = await asyncio.start_server(
            self.handle_midi_client, '127.0.0.1', self.config.midi_port)
        print(f"[MIDI] TCP server on port {self.config.midi_port}")

        control_server = await asyncio.start_server(
            self.handle_control_client, '127.0.0.1', self.config.control_port)
        print(f"[Control] TCP server on port {self.config.control_port}")

        # Start WebSocket server
        ws_server = await websockets.serve(
            self.ws_handler, "127.0.0.1", self.config.ws_port)
        print(f"[WS] Server on ws://localhost:{self.config.ws_port}")

        # Start HTTP server
        app = self.create_http_app()
        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, '127.0.0.1', self.config.http_port)
        await site.start()
        print(f"[HTTP] Server on http://localhost:{self.config.http_port}")

        print()
        print("  Browser: http://localhost:8088/workbench/synth_sim.html?backend=renode")
        print()
        print("  Start Renode:")
        print("    renode --console --disable-xwt -e \"i $CWD/renode/synth_audio.resc\"")
        print()
        print("  Press Ctrl+C to stop.")
        print("=" * 60)

        # Keep running
        await asyncio.Event().wait()


async def main():
    config = Config()
    bridge = WebBridge(config)
    await bridge.start()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nShutting down...")
