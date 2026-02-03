#!/usr/bin/env python3
"""
Test QSPI read-back verification after upload
"""

import sys
import time
import struct
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from umi_dfu import DfuClient, DfuCmd

def main():
    print("=== QSPI Readback Test ===")
    
    # Create test pattern
    test_size = 4096
    test_pattern = bytes([0x55] * test_size)  # 0x55 pattern
    test_file = Path('/tmp/test_55.bin')
    test_file.write_bytes(test_pattern)
    expected_crc = zlib.crc32(test_pattern) & 0xFFFFFFFF
    
    print(f"Test pattern: 0x55 x {test_size}")
    print(f"Expected CRC32: {expected_crc:08X}")
    
    # Connect
    client = DfuClient()
    if not client.connect():
        print("Error: Cannot connect to device")
        return 1
    
    try:
        # Upload
        print("\n--- Upload ---")
        if not client.upload(test_file):
            print("Upload failed!")
            return 1
        
        # Wait a moment
        time.sleep(0.5)
        
        # Read data from QSPI (first 32 bytes)
        print("\n--- Read Data ---")
        result = client.read_data(0, 32)
        if not result['valid']:
            print(f"Read error: {result.get('error')}")
            return 1
        
        print(f"Status: {result['status']:#04x}")
        data = result['data']
        print(f"First 32 bytes: {data.hex()}")
        
        if all(b == 0x55 for b in data):
            print("SUCCESS: Data matches!")
        elif all(b == 0 for b in data):
            print("FAIL: All zeros - read not working")
        else:
            print(f"PARTIAL: Some data differs. First byte: {data[0]:#04x}")
        
        # Also check with FW_READ_CRC (need to add this to DfuClient)
        print("\n--- Read CRC from QSPI ---")
        # FW_READ_CRC payload: [offset(4)][size(4)] big-endian
        payload = struct.pack('>II', 0, test_size)
        response = client.send_command(DfuCmd.FW_READ_CRC, payload)
        
        if not response['valid']:
            print(f"Read CRC error: {response.get('error')}")
        else:
            resp_payload = response['payload']
            status = resp_payload[0] if resp_payload else 0xFF
            if len(resp_payload) >= 5:
                actual_crc = struct.unpack('>I', resp_payload[1:5])[0]
                print(f"Status: {status:#04x}")
                print(f"CRC from QSPI: {actual_crc:08X}")
                print(f"Expected CRC:  {expected_crc:08X}")
                
                if actual_crc == expected_crc:
                    print("CRC MATCH!")
                else:
                    print("CRC MISMATCH!")
                    # Calculate CRC of all zeros for comparison
                    zero_crc = zlib.crc32(bytes(test_size)) & 0xFFFFFFFF
                    print(f"(CRC of all zeros: {zero_crc:08X})")
        
        return 0
    finally:
        client.disconnect()

if __name__ == '__main__':
    sys.exit(main())
