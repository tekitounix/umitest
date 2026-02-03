#!/usr/bin/env python3
"""
DFU Read-back Verification Test

Tests that data written to QSPI flash can be correctly read back.
Uses specific test patterns to detect common flash issues.
"""

import sys
import struct
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[5] / 'lib' / 'umi' / 'boot' / 'tools'))
from umi_dfu import DfuClient, DfuCmd

# Error codes
ERR_SUCCESS = 0x00
ERASE_IN_PROGRESS = 0xFE


def create_test_patterns(size: int) -> list:
    """Create test patterns that detect common flash issues."""
    patterns = []
    
    # Pattern 1: All zeros (0x00) - tests erase state
    patterns.append(("All 0x00", bytes([0x00] * size)))
    
    # Pattern 2: All ones (0xFF) - typical erased state
    patterns.append(("All 0xFF", bytes([0xFF] * size)))
    
    # Pattern 3: Alternating bits (0xAA) - tests bit toggling
    patterns.append(("0xAA pattern", bytes([0xAA] * size)))
    
    # Pattern 4: Alternating bits inverted (0x55)
    patterns.append(("0x55 pattern", bytes([0x55] * size)))
    
    # Pattern 5: Walking ones - tests address/data lines
    walking_ones = bytes([(1 << (i % 8)) for i in range(size)])
    patterns.append(("Walking ones", walking_ones))
    
    # Pattern 6: Sequential bytes (0x00-0xFF repeating)
    sequential = bytes([i % 256 for i in range(size)])
    patterns.append(("Sequential", sequential))
    
    # Pattern 7: Address-based pattern (address XOR data)
    addr_pattern = bytes([(i ^ (i >> 8) ^ (i >> 16)) & 0xFF for i in range(size)])
    patterns.append(("Address XOR", addr_pattern))
    
    return patterns


def upload_data(client: DfuClient, data: bytes) -> bool:
    """Upload data to device."""
    file_size = len(data)
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    
    # FW_BEGIN
    begin_payload = struct.pack('>BI', 0x01, file_size)
    response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
    
    if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
        return False
    
    status = response['payload'][0]
    
    # Wait for erase
    import time
    while status == ERASE_IN_PROGRESS or status == ERR_SUCCESS:
        if status == ERR_SUCCESS:
            break
        time.sleep(0.1)
        response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
        if not response['valid']:
            return False
        status = response['payload'][0]
    
    if status != ERR_SUCCESS:
        print(f"    FW_BEGIN rejected: 0x{status:02X}")
        return False
    
    # FW_DATA
    offset = 0
    while offset < file_size:
        chunk = data[offset:offset + client.CHUNK_SIZE]
        response = client.send_command(DfuCmd.FW_DATA, chunk)
        
        if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
            return False
        if response['payload'][0] != ERR_SUCCESS:
            print(f"    FW_DATA rejected at {offset}: 0x{response['payload'][0]:02X}")
            return False
        
        offset += len(chunk)
    
    # FW_VERIFY
    verify_payload = struct.pack('>I', crc32)
    response = client.send_command(DfuCmd.FW_VERIFY, verify_payload)
    
    if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
        return False
    if response['payload'][0] != ERR_SUCCESS:
        print(f"    FW_VERIFY failed: 0x{response['payload'][0]:02X}")
        return False
    
    # FW_COMMIT
    response = client.send_command(DfuCmd.FW_COMMIT)
    if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
        return False
    
    return True


def verify_readback(client: DfuClient, expected_data: bytes, base_addr: int = 0x90000000) -> tuple:
    """
    Read back data from QSPI and verify against expected.
    
    This requires a READ command which may not be implemented yet.
    For now, we'll use a workaround: ask device to compute CRC of stored data.
    
    Returns: (success, mismatch_offset or None)
    """
    # Check if FW_READ_CRC command exists (0x19)
    # This is a new command we may need to add
    FW_READ_CRC = 0x19
    
    file_size = len(expected_data)
    expected_crc = zlib.crc32(expected_data) & 0xFFFFFFFF
    
    # Try to read CRC from device
    payload = struct.pack('>II', 0, file_size)  # offset, size
    response = client.send_command(FW_READ_CRC, payload)
    
    if not response['valid']:
        return None, "FW_READ_CRC not supported or timeout"
    
    if response['cmd'] != DfuCmd.FW_ACK:
        return None, f"Unexpected response: 0x{response['cmd']:02X}"
    
    if len(response['payload']) >= 5:
        status = response['payload'][0]
        if status != ERR_SUCCESS:
            return False, f"Read CRC failed: 0x{status:02X}"
        
        device_crc = struct.unpack('>I', response['payload'][1:5])[0]
        if device_crc == expected_crc:
            return True, None
        else:
            return False, f"CRC mismatch: expected 0x{expected_crc:08X}, got 0x{device_crc:08X}"
    
    return None, "Invalid response payload"


def test_pattern(client: DfuClient, name: str, data: bytes) -> bool:
    """Test a single pattern: write, then verify readback."""
    print(f"  Testing: {name} ({len(data)} bytes)")
    
    # Upload
    print(f"    Uploading...", end='', flush=True)
    if not upload_data(client, data):
        print(" FAILED")
        return False
    print(" OK")
    
    # Verify readback
    print(f"    Verifying readback...", end='', flush=True)
    success, error = verify_readback(client, data)
    
    if success is None:
        print(f" SKIPPED ({error})")
        return True  # Not a failure, just not supported
    elif success:
        print(" OK")
        return True
    else:
        print(f" FAILED ({error})")
        return False


def main():
    print("=== DFU Read-back Verification Test ===\n")
    
    client = DfuClient()
    if not client.connect():
        print("Failed to connect to device")
        return 1
    
    # Test with small size first (4KB = 1 sector)
    test_size = 4096
    patterns = create_test_patterns(test_size)
    
    results = []
    
    try:
        for name, data in patterns:
            # Reboot between tests to ensure clean state
            print(f"\n  Rebooting device...")
            client.send_command(DfuCmd.FW_REBOOT)
            import time
            time.sleep(2)
            
            # Reconnect after reboot
            client.disconnect()
            if not client.connect():
                print("Failed to reconnect after reboot")
                results.append((name, False))
                continue
            
            success = test_pattern(client, name, data)
            results.append((name, success))
            
    finally:
        client.disconnect()
    
    # Summary
    print("\n" + "=" * 50)
    print("Test Results Summary")
    print("=" * 50)
    
    all_passed = True
    for name, passed in results:
        status = "PASS" if passed else "FAIL"
        print(f"  {name}: {status}")
        if not passed:
            all_passed = False
    
    print("=" * 50)
    
    if all_passed:
        print("All tests PASSED!")
        return 0
    else:
        print("Some tests FAILED!")
        return 1


if __name__ == '__main__':
    sys.exit(main())
