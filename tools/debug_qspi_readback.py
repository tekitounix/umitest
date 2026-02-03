#!/usr/bin/env python3
"""Debug QSPI write and readback"""

import sys
import struct
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from umi_dfu import DfuClient, DfuCmd

FW_READ_DATA = 0x1A

def main():
    client = DfuClient()
    if not client.connect():
        print('Failed to connect')
        return 1

    # Write a known pattern
    print('Writing 0xAA pattern (256 bytes)...')
    test_data = bytes([0xAA] * 256)

    # FW_BEGIN
    client.seq = 0  # Reset sequence number
    begin_payload = struct.pack('>BI', 0x01, 256)
    response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
    print(f'FW_BEGIN: {response["payload"][0]:02X}')

    # Wait for erase
    print('Waiting for erase...')
    time.sleep(2.0)

    # FW_DATA with retry (save seq for retry)
    ERASE_IN_PROGRESS = 0x0C
    saved_seq = client.seq
    for retry in range(100):
        client.seq = saved_seq  # Use same seq for retry
        response = client.send_command(DfuCmd.FW_DATA, test_data)
        if not response['valid']:
            if retry == 0:
                print(f'FW_DATA retry (timeout?)...')
            time.sleep(0.1)
            continue
        status = response["payload"][0]
        if status == ERASE_IN_PROGRESS:
            if retry == 0:
                print(f'FW_DATA: ERASE_IN_PROGRESS, retrying...')
            time.sleep(0.1)
            continue
        print(f'FW_DATA: {status:02X}')
        if status != 0:
            print(f'FW_DATA error!')
            client.disconnect()
            return 1
        break
    else:
        print(f'FW_DATA failed after retries')
        client.disconnect()
        return 1

    # FW_VERIFY
    crc = zlib.crc32(test_data) & 0xFFFFFFFF
    verify_payload = struct.pack('>I', crc)
    response = client.send_command(DfuCmd.FW_VERIFY, verify_payload)
    print(f'FW_VERIFY: {response["payload"][0]:02X}')

    # FW_COMMIT
    response = client.send_command(DfuCmd.FW_COMMIT)
    print(f'FW_COMMIT: {response["payload"][0]:02X}')

    # Now read back
    print()
    print('Reading first 16 bytes from QSPI...')
    payload = struct.pack('>IB', 0, 16)
    response = client.send_command(FW_READ_DATA, payload)

    if response['valid'] and len(response['payload']) > 1:
        status = response['payload'][0]
        data = response['payload'][1:]
        print(f'Status: 0x{status:02X}')
        print(f'Data ({len(data)} bytes): {data.hex()}')
        print(f'As bytes: {list(data)}')
        if data == bytes([0xAA] * 16):
            print('SUCCESS: Data matches!')
        else:
            print('FAIL: Data mismatch!')

    client.disconnect()
    return 0

if __name__ == '__main__':
    sys.exit(main())
