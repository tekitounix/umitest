#!/usr/bin/env python3

import argparse
import sys
import time
import struct
import zlib
from pathlib import Path

try:
    import mido
except ImportError:
    print("Error: mido not installed. Run: pip install mido python-rtmidi")
    sys.exit(1)

# UMI SysEx ID
UMI_SYSEX_ID = [0x7E, 0x7F, 0x00]

# DFU Commands
class DfuCmd:
    FW_QUERY      = 0x10
    FW_INFO       = 0x11
    FW_BEGIN      = 0x12
    FW_DATA       = 0x13
    FW_VERIFY     = 0x14
    FW_COMMIT     = 0x15
    FW_REBOOT     = 0x17
    FW_ACK        = 0x18
    FW_READ_CRC   = 0x19
    FW_READ_DATA  = 0x1A
    FW_DEBUG_QSPI = 0x1B
    FW_FLASH_STATUS = 0x1C

# Update targets
class UpdateTarget:
    KERNEL = 0x00
    APP    = 0x01


def encode_7bit(data: bytes) -> bytes:
    result = bytearray()
    for i in range(0, len(data), 7):
        chunk = data[i:i+7]
        msb_byte = 0
        for j, byte in enumerate(chunk):
            if byte & 0x80:
                msb_byte |= (1 << j)
        result.append(msb_byte)
        for byte in chunk:
            result.append(byte & 0x7F)
    return bytes(result)


def decode_7bit(data: bytes) -> bytes:
    result = bytearray()
    i = 0
    while i < len(data):
        msb_byte = data[i]
        i += 1
        for j in range(7):
            if i >= len(data):
                break
            byte = data[i]
            i += 1
            if msb_byte & (1 << j):
                byte |= 0x80
            result.append(byte)
    return bytes(result)


def calculate_checksum(data: bytes) -> int:
    checksum = 0
    for byte in data:
        checksum ^= byte
    return checksum & 0x7F


def build_sysex(cmd: int, seq: int, payload: bytes = b'') -> bytes:
    msg = bytearray([0xF0])
    msg.extend(UMI_SYSEX_ID)
    cmd_start = len(msg)
    msg.append(cmd)
    msg.append(seq & 0x7F)
    if payload:
        msg.extend(encode_7bit(payload))
    checksum = calculate_checksum(msg[cmd_start:])
    msg.append(checksum)
    msg.append(0xF7)
    return bytes(msg)


def parse_sysex(data: bytes) -> dict:
    if len(data) < 8:
        return {'valid': False}
    if data[0] != 0xF0 or data[-1] != 0xF7:
        return {'valid': False}
    
    # Check UMI SysEx ID
    for i, byte in enumerate(UMI_SYSEX_ID):
        if data[1 + i] != byte:
            return {'valid': False}
    
    cmd_pos = 1 + len(UMI_SYSEX_ID)
    checksum_pos = len(data) - 2
    
    # Verify checksum
    expected = data[checksum_pos]
    actual = calculate_checksum(data[cmd_pos:checksum_pos])
    if expected != actual:
        return {'valid': False, 'error': 'checksum mismatch'}
    
    cmd = data[cmd_pos]
    seq = data[cmd_pos + 1]
    payload_7bit = data[cmd_pos + 2:checksum_pos]
    payload = decode_7bit(payload_7bit) if payload_7bit else b''
    
    return {
        'valid': True,
        'cmd': cmd,
        'seq': seq,
        'payload': payload,
    }


class DfuClient:
    
    # CHUNK_SIZE must be small enough that 7-bit encoded data + SysEx header
    # fits in MidiProcessor's sysex_buf_ (256 bytes).
    # 175 bytes raw -> ~200 bytes encoded + ~7 bytes header = ~207 bytes < 256
    CHUNK_SIZE = 175
    TIMEOUT = 5.0
    
    def __init__(self, device_name: str = None):
        self.outport = None
        self.inport = None
        self.device_name = device_name
        self.seq = 0
    
    def find_device(self) -> tuple:
        out_ports = mido.get_output_names()
        in_ports = mido.get_input_names()
        
        # Filter by device name if specified
        if self.device_name:
            out_ports = [p for p in out_ports if self.device_name.lower() in p.lower()]
            in_ports = [p for p in in_ports if self.device_name.lower() in p.lower()]
        
        # Look for "Daisy" or "UMI" in port names
        for name in ['Daisy', 'UMI', 'Pod']:
            for out in out_ports:
                if name.lower() in out.lower():
                    for inp in in_ports:
                        if name.lower() in inp.lower():
                            return out, inp
        
        # Fallback: use first matching ports
        if out_ports and in_ports:
            return out_ports[0], in_ports[0]
        
        return None, None
    
    def connect(self) -> bool:
        out_name, in_name = self.find_device()
        if not out_name or not in_name:
            print("Error: No UMI device found")
            print("Available output ports:", mido.get_output_names())
            print("Available input ports:", mido.get_input_names())
            return False
        
        print(f"Connecting to: {out_name}")
        self.outport = mido.open_output(out_name)
        self.inport = mido.open_input(in_name)
        return True
    
    def disconnect(self):
        if self.outport:
            self.outport.close()
        if self.inport:
            self.inport.close()
    
    def send_command(self, cmd: int, payload: bytes = b'') -> dict:
        msg_data = build_sysex(cmd, self.seq, payload)
        self.seq = (self.seq + 1) & 0x7F
        
        # Convert to mido SysEx message (strip F0 and F7)
        sysex_data = list(msg_data[1:-1])
        msg = mido.Message('sysex', data=sysex_data)
        
        # Clear input buffer
        for _ in self.inport.iter_pending():
            pass
        
        # Send
        self.outport.send(msg)
        
        # Wait for response
        start = time.time()
        while time.time() - start < self.TIMEOUT:
            for msg in self.inport.iter_pending():
                if msg.type == 'sysex':
                    # Reconstruct full SysEx with F0/F7
                    full_data = bytes([0xF0] + list(msg.data) + [0xF7])
                    response = parse_sysex(full_data)
                    if response['valid']:
                        return response
            time.sleep(0.001)
        
        return {'valid': False, 'error': 'timeout'}
    
    def query(self) -> dict:
        response = self.send_command(DfuCmd.FW_QUERY)
        if not response['valid']:
            return response
        
        if response['cmd'] != DfuCmd.FW_INFO:
            return {'valid': False, 'error': f"unexpected response: {response['cmd']}"}
        
        payload = response['payload']
        if len(payload) >= 12:
            version = struct.unpack('>I', payload[0:4])[0]
            max_size = struct.unpack('>I', payload[4:8])[0]
            base_addr = struct.unpack('>I', payload[8:12])[0]
            return {
                'valid': True,
                'version': f"{(version >> 24) & 0xFF}.{(version >> 16) & 0xFF}.{(version >> 8) & 0xFF}",
                'max_size': max_size,
                'base_addr': hex(base_addr),
            }
        
        return response
    
    def debug_qspi(self) -> dict:
        response = self.send_command(DfuCmd.FW_DEBUG_QSPI)
        if not response['valid']:
            return response
        
        if response['cmd'] != DfuCmd.FW_ACK:
            return {'valid': False, 'error': f"unexpected response: {response['cmd']}"}
        
        payload = response['payload']
        if len(payload) < 29:
            return {'valid': False, 'error': f"payload too short: {len(payload)} bytes"}
        
        status = payload[0]
        cr = struct.unpack('>I', payload[1:5])[0]
        dcr = struct.unpack('>I', payload[5:9])[0]
        sr = struct.unpack('>I', payload[9:13])[0]
        ccr = struct.unpack('>I', payload[13:17])[0]
        dlr = struct.unpack('>I', payload[17:21])[0]
        ar = struct.unpack('>I', payload[21:25])[0]
        abr = struct.unpack('>I', payload[25:29])[0]
        
        result = {
            'valid': True,
            'status': status,
            'CR': cr,
            'DCR': dcr,
            'SR': sr,
            'CCR': ccr,
            'DLR': dlr,
            'AR': ar,
            'ABR': abr,
        }

        if len(payload) >= 38:
            fail_code = struct.unpack('>I', payload[29:33])[0]
            fail_sr = struct.unpack('>I', payload[33:37])[0]
            fail_flash_sr = payload[37]
            result['FAIL_CODE'] = fail_code
            result['FAIL_SR'] = fail_sr
            result['FAIL_FLASH_SR'] = fail_flash_sr

        return result
    
    def read_data(self, offset: int, size: int) -> dict:
        if size > 32:
            size = 32
        payload = struct.pack('>IB', offset, size)
        response = self.send_command(DfuCmd.FW_READ_DATA, payload)
        if not response['valid']:
            return response
        
        if response['cmd'] != DfuCmd.FW_ACK:
            return {'valid': False, 'error': f"unexpected response: {response['cmd']}"}
        
        data = response['payload']
        status = data[0] if data else 0xFF
        raw_data = data[1:] if len(data) > 1 else b''
        
        return {
            'valid': True,
            'status': status,
            'data': raw_data,
        }

    def flash_status(self) -> dict:
        response = self.send_command(DfuCmd.FW_FLASH_STATUS)
        if not response['valid']:
            return response

        if response['cmd'] != DfuCmd.FW_ACK:
            return {'valid': False, 'error': f"unexpected response: {response['cmd']}"}

        payload = response['payload']
        if len(payload) < 10:
            return {'valid': False, 'error': f"payload too short: {len(payload)} bytes"}

        data_end = 10
        if len(payload) >= 18:
            data_end = 18

        return {
            'valid': True,
            'status': payload[0],
            'flash_sr': payload[1],
            'data': payload[2:data_end],
        }
    
    def upload(self, filepath: Path, progress_callback=None) -> bool:
        if not filepath.exists():
            print(f"Error: File not found: {filepath}")
            return False
        
        data = filepath.read_bytes()
        file_size = len(data)
        crc32 = zlib.crc32(data) & 0xFFFFFFFF
        
        print(f"File: {filepath.name}")
        print(f"Size: {file_size} bytes")
        print(f"CRC32: {crc32:08X}")
        
        # FW_BEGIN
        print("Starting update...")
        begin_payload = struct.pack('>BI', UpdateTarget.APP, file_size)
        response = self.send_command(DfuCmd.FW_BEGIN, begin_payload)
        
        if not response['valid']:
            print(f"Error: FW_BEGIN failed: {response.get('error', 'unknown')}")
            return False
        
        if response['cmd'] != DfuCmd.FW_ACK or response['payload'][0] != 0:
            print(f"Error: FW_BEGIN rejected: {response['payload'][0]:02X}")
            return False
        
        # Calculate expected erase time (4KB sectors, max 400ms each)
        num_sectors = (file_size + 4095) // 4096
        estimated_erase_time = max(1.0, num_sectors * 0.4)
        print(f"Erasing flash ({num_sectors} sectors, ~{estimated_erase_time:.1f}s)...", end='', flush=True)
        time.sleep(estimated_erase_time)
        print(" done")
        
        # FW_DATA chunks
        offset = 0
        chunk_count = (file_size + self.CHUNK_SIZE - 1) // self.CHUNK_SIZE
        
        print(f"Uploading {chunk_count} chunks...")
        
        ERASE_IN_PROGRESS = 0x0C
        MAX_ERASE_RETRIES = 500
        
        for i in range(chunk_count):
            chunk = data[offset:offset + self.CHUNK_SIZE]
            
            # Retry loop for ERASE_IN_PROGRESS
            retry_count = 0
            while True:
                response = self.send_command(DfuCmd.FW_DATA, chunk)
                
                if not response['valid']:
                    print(f"\nError: FW_DATA failed at offset {offset}: {response.get('error', 'unknown')}")
                    return False
                
                if response['cmd'] == DfuCmd.FW_ACK and response['payload'][0] == ERASE_IN_PROGRESS:
                    # Still erasing, wait and retry
                    retry_count += 1
                    if retry_count > MAX_ERASE_RETRIES:
                        print(f"\nError: Erase timeout at offset {offset}")
                        return False
                    if retry_count == 1:
                        print(f"\nWaiting for erase to complete...", end='', flush=True)
                    time.sleep(0.1)
                    continue
                
                break  # Exit retry loop
            
            if response['cmd'] != DfuCmd.FW_ACK or response['payload'][0] != 0:
                print(f"\nError: FW_DATA rejected at offset {offset}: {response['payload'][0]:02X}")
                return False
            
            offset += len(chunk)
            
            # Progress
            percent = (i + 1) * 100 // chunk_count
            bar_len = 50
            filled = bar_len * (i + 1) // chunk_count
            bar = '=' * filled + '-' * (bar_len - filled)
            print(f"\r[{bar}] {percent}% ({offset}/{file_size})", end='', flush=True)
            
            if progress_callback:
                progress_callback(i + 1, chunk_count)

        print()  # Newline after progress bar
        
        # FW_VERIFY
        print("Verifying...")
        verify_payload = struct.pack('>I', crc32)
        response = self.send_command(DfuCmd.FW_VERIFY, verify_payload)
        
        if not response['valid']:
            print(f"Error: FW_VERIFY failed: {response.get('error', 'unknown')}")
            return False
        
        if response['cmd'] != DfuCmd.FW_ACK or response['payload'][0] != 0:
            print(f"Error: FW_VERIFY failed - CRC mismatch: {response['payload'][0]:02X}")
            return False
        
        print("Verification passed!")
        
        # FW_COMMIT
        print("Committing...")
        response = self.send_command(DfuCmd.FW_COMMIT)
        
        if not response['valid']:
            print(f"Error: FW_COMMIT failed: {response.get('error', 'unknown')}")
            return False
        
        if response['cmd'] != DfuCmd.FW_ACK or response['payload'][0] != 0:
            print(f"Error: FW_COMMIT rejected: {response['payload'][0]:02X}")
            return False
        
        print("Upload complete!")
        return True
    
    def reboot(self) -> bool:
        print("Rebooting device...")
        response = self.send_command(DfuCmd.FW_REBOOT)
        # Device may reset before responding
        if response['valid'] and response['cmd'] == DfuCmd.FW_ACK:
            print("Reboot command sent")
            return True
        print("Reboot command sent (no ack received - device may have reset)")
        return True


def cmd_query(args):
    client = DfuClient(args.device)
    if not client.connect():
        return 1
    
    try:
        info = client.query()
        if info['valid']:
            print(f"Device Version: {info.get('version', 'unknown')}")
            print(f"Max App Size: {info.get('max_size', 0)} bytes")
            print(f"Base Address: {info.get('base_addr', 'unknown')}")
            return 0
        else:
            print(f"Query failed: {info.get('error', 'unknown')}")
            return 1
    finally:
        client.disconnect()


def cmd_upload(args):
    client = DfuClient(args.device)
    if not client.connect():
        return 1
    
    try:
        filepath = Path(args.file)
        if client.upload(filepath):
            if args.reboot:
                time.sleep(0.5)
                client.reboot()
            return 0
        return 1
    finally:
        client.disconnect()


def cmd_reboot(args):
    client = DfuClient(args.device)
    if not client.connect():
        return 1
    
    try:
        client.reboot()
        return 0
    finally:
        client.disconnect()


def cmd_debug_qspi(args):
    client = DfuClient(args.device)
    if not client.connect():
        return 1

    try:
        result = client.debug_qspi()
        if not result['valid']:
            print(f"Error: {result.get('error', 'unknown')}")
            return 1

        print("QUADSPI Registers:")
        print(f"  Status: {result['status']:#04x}")
        print(f"  CR:  {result['CR']:#010x}")
        print(f"  DCR: {result['DCR']:#010x}")
        print(f"  SR:  {result['SR']:#010x}")
        print(f"  CCR: {result['CCR']:#010x}")
        print(f"  DLR: {result['DLR']:#010x}")
        print(f"  AR:  {result['AR']:#010x}")
        print(f"  ABR: {result['ABR']:#010x}")

        if 'FAIL_CODE' in result:
            print(f"  FAIL_CODE: {result['FAIL_CODE']:#010x}")
            print(f"  FAIL_SR:   {result['FAIL_SR']:#010x}")
            print(f"  FAIL_FLASH_SR: {result['FAIL_FLASH_SR']:#04x}")

        cr = result['CR']
        sr = result['SR']
        ccr = result['CCR']

        en = (cr >> 0) & 1
        busy = (sr >> 5) & 1
        fmode = (ccr >> 26) & 0x3
        fmode_names = ['Indirect Write', 'Indirect Read', 'Auto Polling', 'Memory Mapped']

        print(f"\nAnalysis:")
        print(f"  QUADSPI EN: {en}")
        print(f"  BUSY: {busy}")
        print(f"  FMODE: {fmode_names[fmode]}")

        return 0
    finally:
        client.disconnect()


def cmd_read_data(args):
    client = DfuClient(args.device)
    if not client.connect():
        return 1
    
    try:
        result = client.read_data(args.offset, args.size)
        if not result['valid']:
            print(f"Error: {result.get('error', 'unknown')}")
            return 1
        
        print(f"Status: {result['status']:#04x}")
        data = result['data']
        print(f"Data ({len(data)} bytes): {data.hex()}")
        
        # Check if all zeros
        if all(b == 0 for b in data):
            print("WARNING: All zeros!")
        
        return 0
    finally:
        client.disconnect()

def cmd_flash_status(args):
    client = DfuClient(args.device)
    if not client.connect():
        return 1

    try:
        result = client.flash_status()
        if not result['valid']:
            print(f"Error: {result.get('error', 'unknown')}")
            return 1

        data = result['data']
        print(f"Status: {result['status']:#04x}")
        print(f"Flash SR: {result['flash_sr']:#04x}")
        print(f"Data (8 bytes): {data.hex()}")
        return 0
    finally:
        client.disconnect()


def cmd_list(args):
    print("Output ports:")
    for name in mido.get_output_names():
        print(f"  {name}")
    print("\nInput ports:")
    for name in mido.get_input_names():
        print(f"  {name}")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description='UMI DFU Tool - Upload apps via USB MIDI SysEx'
    )
    subparsers = parser.add_subparsers(dest='command', help='Commands')
    
    # query
    p_query = subparsers.add_parser('query', help='Query device info')
    p_query.add_argument('-d', '--device', help='Device name filter')
    p_query.set_defaults(func=cmd_query)
    
    # upload
    p_upload = subparsers.add_parser('upload', help='Upload .umia app')
    p_upload.add_argument('file', help='Path to .umia file')
    p_upload.add_argument('-d', '--device', help='Device name filter')
    p_upload.add_argument('-r', '--reboot', action='store_true', help='Reboot after upload')
    p_upload.set_defaults(func=cmd_upload)
    
    # reboot
    p_reboot = subparsers.add_parser('reboot', help='Reboot device')
    p_reboot.add_argument('-d', '--device', help='Device name filter')
    p_reboot.set_defaults(func=cmd_reboot)
    
    # debug-qspi
    p_debug = subparsers.add_parser('debug-qspi', help='Debug QUADSPI registers')
    p_debug.add_argument('-d', '--device', help='Device name filter')
    p_debug.set_defaults(func=cmd_debug_qspi)
    
    # read-data
    p_read = subparsers.add_parser('read-data', help='Read raw data from QSPI')
    p_read.add_argument('-d', '--device', help='Device name filter')
    p_read.add_argument('--offset', type=lambda x: int(x, 0), default=0, help='Offset (hex or dec)')
    p_read.add_argument('--size', type=int, default=16, help='Size (max 32)')
    p_read.set_defaults(func=cmd_read_data)

    # flash-status
    p_status = subparsers.add_parser('flash-status', help='Read flash status and first 8 bytes')
    p_status.add_argument('-d', '--device', help='Device name filter')
    p_status.set_defaults(func=cmd_flash_status)
    
    # list
    p_list = subparsers.add_parser('list', help='List MIDI devices')
    p_list.set_defaults(func=cmd_list)
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
