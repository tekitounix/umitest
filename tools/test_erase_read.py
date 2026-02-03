#!/usr/bin/env python3
"""Test erase and indirect read"""
import struct
import time
import sys
sys.path.insert(0, '.')
from umi_dfu import DfuClient

c = DfuClient()
c.connect()

# Trigger erase by starting FW_BEGIN
begin_payload = struct.pack('>BI', 0x01, 256)
response = c.send_command(0x12, begin_payload)  # FW_BEGIN
print('FW_BEGIN response:', response)

time.sleep(2)

# Now test indirect read
response = c.send_command(0x1E, b'')  # FW_WRITE_TEST
print('After erase, FW_WRITE_TEST response:', response)
if response['valid'] and response['cmd'] == 0x18:
    data = response['payload']
    print(f'  Status: 0x{data[0]:02X}')
    print(f'  Data: {data[1:9].hex()}')
    if all(b == 0xFF for b in data[1:9]):
        print('  => All 0xFF (erased OK)')
