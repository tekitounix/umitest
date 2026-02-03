#!/usr/bin/env python3
"""
QSPI Pattern Write/Read/Verify Test
Tests multiple patterns to verify QSPI flash write and read functionality.
"""

import sys
import zlib
import tempfile
import os
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[5] / 'lib' / 'umi' / 'boot' / 'tools'))
from umi_dfu import DfuClient

def main():
    print("=== QSPI Pattern Write/Read/Verify Test ===")
    
    client = DfuClient()
    if not client.connect():
        print("Error: Cannot connect to device")
        return 1
    
    def upload_and_verify(name, data):
        """Upload data pattern and verify via read-back."""
        print(f"\n--- {name} ({len(data)} bytes) ---")
        
        # Calculate expected CRC
        crc = zlib.crc32(data) & 0xFFFFFFFF
        print(f"CRC32: 0x{crc:08X}")
        
        # Write to temp file
        with tempfile.NamedTemporaryFile(suffix='.umia', delete=False) as f:
            f.write(data)
            temp_path = f.name
        
        try:
            # Upload
            print("Uploading...", end=' ', flush=True)
            result = client.upload(Path(temp_path))
            if not result:
                print("UPLOAD FAILED")
                return False
            print("OK")
        finally:
            os.unlink(temp_path)
        
        # Read back first 32 bytes
        print("Reading back...", end=' ', flush=True)
        read_result = client.read_data(0, 32)
        
        if not read_result['valid']:
            print(f"READ FAILED: {read_result.get('error', 'unknown')}")
            return False
        
        if read_result['status'] != 0:
            print(f"READ ERROR: status=0x{read_result['status']:02X}")
            return False
        
        read_data = read_result['data']
        expected = data[:32]
        
        print()
        print(f"  Expected: {expected[:16].hex()} ...")
        print(f"  Got:      {read_data[:16].hex()} ...")
        
        if read_data == expected:
            print("  ✅ VERIFY PASSED!")
            return True
        else:
            # Find first mismatch
            for i in range(min(len(expected), len(read_data))):
                if expected[i] != read_data[i]:
                    print(f"  ❌ VERIFY FAILED at byte {i}: expected 0x{expected[i]:02x}, got 0x{read_data[i]:02x}")
                    break
            return False
    
    # Test patterns
    tests = [
        ("All 0xAA", bytes([0xAA] * 4096)),
        ("All 0x55", bytes([0x55] * 4096)),
        ("All 0x00", bytes([0x00] * 4096)),
        ("Sequential 0-255", bytes(range(256)) * 16),
        ("UIMA Header", b'UIMA\x01\x00\x00\x00' + bytes([0x00] * 120) + bytes([0xDE, 0xAD, 0xBE, 0xEF] * 992)),
    ]
    
    results = []
    for name, data in tests:
        result = upload_and_verify(name, data)
        results.append((name, result))
    
    # Summary
    print("\n" + "=" * 50)
    print("SUMMARY")
    print("=" * 50)
    passed = 0
    failed = 0
    for name, success in results:
        status = "✅ PASS" if success else "❌ FAIL"
        print(f"  {name}: {status}")
        if success:
            passed += 1
        else:
            failed += 1
    
    print(f"\nTotal: {passed} passed, {failed} failed")
    
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
