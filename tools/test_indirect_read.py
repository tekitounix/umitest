#!/usr/bin/env python3
"""
Test QSPI indirect read via FW_FLASH_STATUS command and JEDEC ID
"""

import sys
import time
import struct
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from umi_dfu import DfuClient, DfuCmd, build_sysex, parse_sysex

# Add new commands
FW_FLASH_STATUS = 0x1C
FW_JEDEC_ID = 0x1D
FW_WRITE_TEST = 0x1E

def main():
    print("=== QSPI Flash Communication Test ===")
    
    # Connect
    client = DfuClient()
    if not client.connect():
        print("Error: Cannot connect to device")
        return 1
    
    try:
        # First, run simple indirect read test
        print("\n--- Indirect Read Test (Pure, No SR Read) ---")
        response = client.send_command(FW_WRITE_TEST)
        if response['valid']:
            payload = response['payload']
            if len(payload) >= 10:
                status = payload[0]
                data = payload[1:9]
                fifo_level = payload[9]
                print(f"Status:     {'OK' if status == 0 else 'TIMEOUT'}")
                print(f"FIFO Level: {fifo_level}")
                print(f"Data:       {data.hex()}")
                
                if all(b == 0x00 for b in data):
                    print("RESULT: All zeros")
                elif all(b == 0xFF for b in data):
                    print("RESULT: All 0xFF (erased)")
                else:
                    print(f"RESULT: Mixed data")
            else:
                print(f"Unexpected payload length {len(payload)}: {payload.hex()}")
        else:
            print(f"Error: {response.get('error')}")
        
        # Check JEDEC ID to verify basic communication
        print("\n--- JEDEC ID ---")
        response = client.send_command(FW_JEDEC_ID)
        if response['valid']:
            payload = response['payload']
            if len(payload) >= 7:
                mfr_id = payload[1]
                device_id = (payload[2] << 8) | payload[3]
                sr = payload[4]
                timeout_flag = payload[5]
                fifo_level = payload[6]
                print(f"Manufacturer ID: {mfr_id:#04x}")
                print(f"Device ID: {device_id:#06x}")
                print(f"Status Register: {sr:#04x} (QE={(sr & 0x40) >> 6})")
                print(f"Timeout: {'Yes' if timeout_flag else 'No'}")
                print(f"FIFO Level: {fifo_level}")
                
                # IS25LP064A should be: Manufacturer=0x9D, Device=0x6017
                if mfr_id == 0x9D and device_id == 0x6017:
                    print("SUCCESS: IS25LP064A detected!")
                elif mfr_id == 0x00 and device_id == 0x0000:
                    print("FAIL: All zeros - flash not responding")
                elif mfr_id == 0xFF and device_id == 0xFFFF:
                    print("FAIL: All 0xFF - flash not connected?")
                else:
                    print(f"WARNING: Unknown flash device")
            else:
                print(f"Unexpected payload length {len(payload)}: {payload.hex()}")
        else:
            print(f"Error: {response.get('error')}")
        
        # Check current state
        print("\n--- Flash Status & Indirect Read ---")
        response = client.send_command(FW_FLASH_STATUS)
        if response['valid']:
            payload = response['payload']
            if len(payload) >= 10:
                flash_sr = payload[1]
                data = payload[2:10]
                print(f"Flash SR: {flash_sr:#04x}")
                print(f"  WIP={flash_sr & 0x01}, WEL={(flash_sr & 0x02) >> 1}, QE={(flash_sr & 0x40) >> 6}")
                print(f"Data at 0x00 (indirect read): {data.hex()}")
            else:
                print(f"Unexpected payload: {payload.hex()}")
        else:
            print(f"Error: {response.get('error')}")
        
        # Upload test pattern
        print("\n--- Upload 0xAA Pattern ---")
        test_data = bytes([0xAA] * 4096)
        test_file = Path('/tmp/test_aa.bin')
        test_file.write_bytes(test_data)
        
        if not client.upload(test_file):
            print("Upload failed!")
            return 1
        
        # Wait a moment
        time.sleep(0.5)
        
        # Check state after upload (indirect read)
        print("\n--- After Upload (Indirect Read) ---")
        response = client.send_command(FW_FLASH_STATUS)
        if response['valid']:
            payload = response['payload']
            if len(payload) >= 10:
                flash_sr = payload[1]
                data = payload[2:10]
                print(f"Flash SR: {flash_sr:#04x}")
                print(f"  WIP={flash_sr & 0x01}, WEL={(flash_sr & 0x02) >> 1}, QE={(flash_sr & 0x40) >> 6}")
                print(f"Data at 0x00 (indirect read): {data.hex()}")
                
                if all(b == 0xAA for b in data):
                    print("SUCCESS: Indirect read returns correct data!")
                elif all(b == 0 for b in data):
                    print("FAIL: Indirect read also returns zeros")
                elif all(b == 0xFF for b in data):
                    print("FAIL: Flash seems to be erased (all 0xFF)")
                else:
                    print(f"PARTIAL: Some data is different")
            else:
                print(f"Unexpected payload: {payload.hex()}")
        else:
            print(f"Error: {response.get('error')}")
        
        # Check memory-mapped read
        print("\n--- Memory-Mapped Read ---")
        result = client.read_data(0, 16)
        if result['valid']:
            print(f"Data at 0x90000000: {result['data'].hex()}")
            if all(b == 0xAA for b in result['data']):
                print("SUCCESS: Memory-mapped read also works!")
            elif all(b == 0 for b in result['data']):
                print("FAIL: Memory-mapped still returns zeros")
        
        return 0
    finally:
        client.disconnect()

if __name__ == '__main__':
    sys.exit(main())
