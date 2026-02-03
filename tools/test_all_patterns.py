#!/usr/bin/env python3
"""Test QSPI write/read with multiple byte patterns"""
import struct
import time
import zlib
import tempfile
import os
import sys
sys.path.insert(0, '.')
from pathlib import Path
from umi_dfu import DfuClient

def test_pattern(c, pattern_byte, name):
    """Test a specific byte pattern"""
    print(f"\n=== Testing {name} (0x{pattern_byte:02X}) ===")
    
    # Create test data (256 bytes)
    test_data = bytes([pattern_byte] * 256)
    
    # Create temp file
    with tempfile.NamedTemporaryFile(suffix='.umia', delete=False) as f:
        f.write(test_data)
        tmp_path = f.name
    
    try:
        # Upload using DfuClient
        result = c.upload(Path(tmp_path))
        if not result:
            print(f"  Upload failed!")
            return False
        print(f"  Upload: OK")
        
        # Test indirect read (FW_WRITE_TEST = 0x18)
        # Address: 0x90000000 (QSPI base), Size: 8 bytes
        resp = c.send_command(0x18, bytes([0x00, 0x00, 0x00, 0x90, 0x08, 0x00, 0x00, 0x00]))
        if resp['valid']:
            data = resp['payload'][1:9]
            expected = bytes([pattern_byte] * 8)
            if data == expected:
                print(f"  Indirect read: OK - {data.hex()}")
            else:
                print(f"  Indirect read: MISMATCH - expected {expected.hex()}, got {data.hex()}")
                return False
        else:
            print(f"  Indirect read failed: {resp}")
            return False
        
        # Test memory-mapped read (FW_READ_DATA)
        read_result = c.read_data(0, 32)
        if read_result['valid'] and read_result['status'] == 0:
            data = read_result['data'][:8]
            expected = bytes([pattern_byte] * 8)
            if data == expected:
                print(f"  Memory-mapped read: OK - {data.hex()}")
            else:
                print(f"  Memory-mapped read: MISMATCH - expected {expected.hex()}, got {data.hex()}")
                return False
        else:
            print(f"  Memory-mapped read failed: {read_result}")
            return False
        
        print(f"  => {name} PASSED!")
        return True
    finally:
        os.unlink(tmp_path)

# Run tests
c = DfuClient()
c.connect()
print(f"Connected")

results = []
for pattern, name in [(0x00, "Zero"), (0x55, "0x55"), (0xAA, "0xAA"), (0xFF, "0xFF")]:
    results.append((name, test_pattern(c, pattern, name)))
    time.sleep(0.5)

print("\n" + "="*50)
print("SUMMARY:")
all_passed = True
for name, passed in results:
    status = "PASS" if passed else "FAIL"
    print(f"  {name}: {status}")
    if not passed:
        all_passed = False

if all_passed:
    print("\nAll patterns PASSED!")
else:
    print("\nSome patterns FAILED!")
