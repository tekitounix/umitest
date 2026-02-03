#!/usr/bin/env python3
"""Test simple upload without indirect read test"""
import struct
import time
import zlib
import tempfile
import os
import sys
sys.path.insert(0, '.')
from pathlib import Path
from umi_dfu import DfuClient

c = DfuClient()
c.connect()

# Test pattern
test_data = bytes([0xAA] * 256)
crc = zlib.crc32(test_data) & 0xFFFFFFFF
print(f"Test pattern: 0xAA x 256 bytes, CRC32: 0x{crc:08X}")

# Write to temp file
with tempfile.NamedTemporaryFile(suffix='.umia', delete=False) as f:
    f.write(test_data)
    temp_path = f.name

try:
    # Upload
    print("Uploading...")
    result = c.upload(Path(temp_path))
    print(f"Upload result: {result}")
finally:
    os.unlink(temp_path)

if not result:
    print("Upload failed, cannot continue")
    sys.exit(1)

# Wait for flash operations
time.sleep(1)

# Test memory-mapped read via FW_READ_DATA
print("\nTesting memory-mapped read (FW_READ_DATA)...")
read_result = c.read_data(0, 32)
print(f"  read_data result: {read_result}")
if read_result['valid']:
    if read_result['status'] == 0:
        got = read_result['data'].hex()
        print(f"  Data: {got[:32]}...")
        if got.startswith('aa' * 16):
            print("  => SUCCESS: Upload and read works!")
        else:
            print("  => FAIL: Read returns wrong data")
