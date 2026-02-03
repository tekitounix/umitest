#!/usr/bin/env python3
"""
DFU Error Case Test Script

Tests error handling scenarios:
1. CRC mismatch (verify failure)
2. Device state recovery after error
"""

import sys
import struct
import zlib
from pathlib import Path

# Add umi_dfu module (tools/)
sys.path.insert(0, str(Path(__file__).parents[5] / 'lib' / 'umi' / 'boot' / 'tools'))
from umi_dfu import DfuClient, DfuCmd, build_sysex, parse_sysex, encode_7bit

# Error codes expected from device
ERR_SUCCESS = 0x00
ERR_CRC_MISMATCH = 0x01
ERASE_IN_PROGRESS = 0xFE


def test_crc_mismatch(client: DfuClient, test_size: int = 8192) -> bool:
    """Test CRC mismatch error handling.
    
    Uploads valid data but sends wrong CRC in FW_VERIFY.
    Device should return error and remain in valid state.
    """
    print(f"\n=== Test: CRC Mismatch (size={test_size}) ===")
    
    # Create test data
    test_data = bytes(range(256)) * (test_size // 256)
    test_data = test_data[:test_size]
    
    # Calculate real CRC (we'll send a wrong one later)
    real_crc = zlib.crc32(test_data) & 0xFFFFFFFF
    wrong_crc = (real_crc ^ 0xDEADBEEF) & 0xFFFFFFFF
    
    file_size = len(test_data)
    
    # FW_BEGIN
    print("Sending FW_BEGIN...")
    begin_payload = struct.pack('>BI', 0x01, file_size)  # target=APP, size
    response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
    
    if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
        print(f"ERROR: FW_BEGIN failed")
        return False
    
    status = response['payload'][0]
    if status == ERASE_IN_PROGRESS:
        print("Waiting for erase...")
        import time
        while status == ERASE_IN_PROGRESS:
            time.sleep(0.1)
            response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
            if not response['valid']:
                print("ERROR: Communication lost during erase")
                return False
            status = response['payload'][0]
    
    if status != ERR_SUCCESS:
        print(f"ERROR: FW_BEGIN rejected with status {status:02X}")
        return False
    
    print("FW_BEGIN accepted")
    
    # FW_DATA - send all chunks (data only, no offset)
    print("Sending FW_DATA...")
    offset = 0
    chunk_count = (file_size + client.CHUNK_SIZE - 1) // client.CHUNK_SIZE
    
    for i in range(chunk_count):
        chunk = test_data[offset:offset + client.CHUNK_SIZE]
        # Note: umi_dfu.py sends chunk only (no offset prefix)
        
        response = client.send_command(DfuCmd.FW_DATA, chunk)
        
        if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
            print(f"ERROR: FW_DATA failed at offset {offset}")
            return False
        
        if response['payload'][0] != ERR_SUCCESS:
            print(f"ERROR: FW_DATA rejected at offset {offset}")
            return False
        
        offset += len(chunk)
        
        # Progress
        if (i + 1) % 10 == 0 or i == chunk_count - 1:
            percent = (i + 1) * 100 // chunk_count
            print(f"  Progress: {percent}%")
    
    # FW_VERIFY with WRONG CRC
    print(f"Sending FW_VERIFY with WRONG CRC...")
    print(f"  Real CRC:  0x{real_crc:08X}")
    print(f"  Wrong CRC: 0x{wrong_crc:08X}")
    
    verify_payload = struct.pack('>I', wrong_crc)
    response = client.send_command(DfuCmd.FW_VERIFY, verify_payload)
    
    if not response['valid']:
        print("ERROR: FW_VERIFY communication failed")
        return False
    
    if response['cmd'] != DfuCmd.FW_ACK:
        print(f"ERROR: Unexpected response cmd: 0x{response['cmd']:02X}")
        return False
    
    status = response['payload'][0]
    
    if status == ERR_SUCCESS:
        print("FAIL: Device accepted wrong CRC!")
        return False
    
    if status == ERR_CRC_MISMATCH:
        print(f"PASS: Device correctly rejected wrong CRC (status=0x{status:02X})")
    else:
        print(f"PASS: Device rejected with status=0x{status:02X}")
    
    return True


def test_recovery_after_error(client: DfuClient) -> bool:
    """Test that device can accept new upload after error.
    
    After a CRC mismatch error, verify device accepts a new FW_BEGIN.
    """
    print(f"\n=== Test: Recovery After Error ===")
    
    # Try to start a new upload (small size)
    test_size = 1024
    begin_payload = struct.pack('>BI', 0x01, test_size)
    response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
    
    if not response['valid']:
        print("ERROR: FW_BEGIN communication failed")
        return False
    
    if response['cmd'] != DfuCmd.FW_ACK:
        print(f"ERROR: Unexpected response: 0x{response['cmd']:02X}")
        return False
    
    status = response['payload'][0]
    
    # Wait for erase if needed
    if status == ERASE_IN_PROGRESS:
        print("Waiting for erase...")
        import time
        for _ in range(100):
            time.sleep(0.1)
            response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
            if not response['valid']:
                print("ERROR: Communication lost")
                return False
            status = response['payload'][0]
            if status != ERASE_IN_PROGRESS:
                break
    
    if status == ERR_SUCCESS:
        print("PASS: Device accepts new upload after error")
        # Note: device is now in RECEIVING state (or ERASING/IDLE)
        # Send reboot to reset state for next test
        import time
        print("  (Rebooting to reset state...)")
        client.send_command(DfuCmd.FW_REBOOT)
        time.sleep(2)
        return True
    else:
        print(f"FAIL: Device rejected new upload with status=0x{status:02X}")
        return False


def test_normal_upload(client: DfuClient, test_size: int = 4096) -> bool:
    """Test normal upload works correctly (sanity check after error tests)."""
    print(f"\n=== Test: Normal Upload ({test_size} bytes) ===")
    
    # Create test data
    test_data = bytes(range(256)) * (test_size // 256)
    test_data = test_data[:test_size]
    
    real_crc = zlib.crc32(test_data) & 0xFFFFFFFF
    file_size = len(test_data)
    
    # FW_BEGIN
    print("Sending FW_BEGIN...")
    begin_payload = struct.pack('>BI', 0x01, file_size)
    response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
    
    if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
        print("ERROR: FW_BEGIN failed")
        return False
    
    status = response['payload'][0]
    if status == ERASE_IN_PROGRESS:
        import time
        print("Waiting for erase...")
        while status == ERASE_IN_PROGRESS:
            time.sleep(0.1)
            response = client.send_command(DfuCmd.FW_BEGIN, begin_payload)
            if not response['valid']:
                return False
            status = response['payload'][0]
    
    if status != ERR_SUCCESS:
        print(f"ERROR: FW_BEGIN rejected: {status:02X}")
        return False
    
    # FW_DATA (data only, no offset)
    print("Sending FW_DATA...")
    offset = 0
    while offset < file_size:
        chunk = test_data[offset:offset + client.CHUNK_SIZE]
        # Note: umi_dfu.py sends chunk only (no offset prefix)
        
        response = client.send_command(DfuCmd.FW_DATA, chunk)
        if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
            print(f"ERROR: FW_DATA failed at {offset}")
            return False
        if response['payload'][0] != ERR_SUCCESS:
            print(f"ERROR: FW_DATA rejected at {offset}")
            return False
        
        offset += len(chunk)
    
    print("FW_DATA complete")
    
    # FW_VERIFY with correct CRC
    print(f"Sending FW_VERIFY with correct CRC (0x{real_crc:08X})...")
    verify_payload = struct.pack('>I', real_crc)
    response = client.send_command(DfuCmd.FW_VERIFY, verify_payload)
    
    if not response['valid'] or response['cmd'] != DfuCmd.FW_ACK:
        print("ERROR: FW_VERIFY failed")
        return False
    
    if response['payload'][0] != ERR_SUCCESS:
        print(f"ERROR: FW_VERIFY rejected: {response['payload'][0]:02X}")
        return False
    
    print("PASS: Normal upload verified successfully")
    return True


def main():
    print("=== UMI DFU Error Case Tests ===\n")
    
    client = DfuClient()
    if not client.connect():
        print("Failed to connect to device")
        return 1
    
    results = []
    
    try:
        # Test 1: CRC Mismatch
        results.append(("CRC Mismatch", test_crc_mismatch(client)))
        
        # Test 2: Recovery After Error
        results.append(("Recovery After Error", test_recovery_after_error(client)))
        
        # Test 3: Normal Upload (sanity check)
        results.append(("Normal Upload", test_normal_upload(client)))
        
    finally:
        client.disconnect()
    
    # Print summary
    print("\n" + "=" * 40)
    print("Test Results Summary")
    print("=" * 40)
    
    all_passed = True
    for name, passed in results:
        status = "PASS" if passed else "FAIL"
        print(f"  {name}: {status}")
        if not passed:
            all_passed = False
    
    print("=" * 40)
    
    if all_passed:
        print("All tests PASSED!")
        return 0
    else:
        print("Some tests FAILED!")
        return 1


if __name__ == '__main__':
    sys.exit(main())
