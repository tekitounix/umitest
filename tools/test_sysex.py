#!/usr/bin/env python3
"""Quick SysEx test for debugging"""

import mido
import time

def main():
    out_ports = mido.get_output_names()
    in_ports = mido.get_input_names()

    out_name = in_name = None
    for p in out_ports:
        if 'Daisy' in p:
            out_name = p
            break

    for p in in_ports:
        if 'Daisy' in p:
            in_name = p
            break

    print(f'Output: {out_name}')
    print(f'Input: {in_name}')

    if not out_name or not in_name:
        print("ERROR: Daisy not found")
        return

    outport = mido.open_output(out_name)
    inport = mido.open_input(in_name)

    # Send FW_QUERY
    sysex_data = [0x7E, 0x7F, 0x00, 0x10, 0x00, 0x10]  # ID + CMD + SEQ + CHECKSUM
    print(f'Sending FW_QUERY: {[hex(b) for b in sysex_data]}')
    msg = mido.Message('sysex', data=sysex_data)
    outport.send(msg)

    time.sleep(0.5)
    for m in inport.iter_pending():
        if m.type == 'sysex':
            print(f'Response: {[hex(b) for b in m.data]}')

    # Now send FW_BEGIN (simplified - no 7-bit encoding for now)
    # Target: APP (0x01), Size: 4096 (0x00001000)
    # Encode: [0x01, 0x00, 0x00, 0x10, 0x00] -> 7-bit: [msb=0x08] [0x01, 0x00, 0x00, 0x10, 0x00]
    # MSB byte: bit 3 set for 0x10 -> 0x08
    payload = [0x08, 0x01, 0x00, 0x00, 0x10, 0x00]
    sysex_data = [0x7E, 0x7F, 0x00, 0x12, 0x01]  # ID + FW_BEGIN + seq=1
    sysex_data.extend(payload)
    # Checksum
    checksum = 0
    for b in sysex_data[3:]:
        checksum ^= b
    sysex_data.append(checksum & 0x7F)
    
    print(f'\nSending FW_BEGIN: {[hex(b) for b in sysex_data]}')
    msg = mido.Message('sysex', data=sysex_data)
    outport.send(msg)

    time.sleep(2)  # Wait for erase
    for m in inport.iter_pending():
        if m.type == 'sysex':
            print(f'Response: {[hex(b) for b in m.data]}')

    # Send FW_DATA with small chunk (1 byte encoded)
    raw_data = bytes(10)  # 10 bytes
    
    # Encode 7-bit
    encoded = []
    for i in range(0, len(raw_data), 7):
        chunk = raw_data[i:i+7]
        msb_byte = 0
        for j, byte in enumerate(chunk):
            if byte & 0x80:
                msb_byte |= (1 << j)
        encoded.append(msb_byte)
        for byte in chunk:
            encoded.append(byte & 0x7F)
    
    sysex_data = [0x7E, 0x7F, 0x00, 0x13, 0x02]  # ID + FW_DATA + seq=2
    sysex_data.extend(encoded)
    checksum = 0
    for b in sysex_data[3:]:
        checksum ^= b
    sysex_data.append(checksum & 0x7F)
    
    print(f'\nSending FW_DATA with 10 bytes (encoded to {len(encoded)} bytes)')
    print(f'Total SysEx size: {len(sysex_data)} bytes')
    msg = mido.Message('sysex', data=sysex_data)
    outport.send(msg)

    time.sleep(0.5)
    for m in inport.iter_pending():
        if m.type == 'sysex':
            print(f'Response: {[hex(b) for b in m.data]}')

    outport.close()
    inport.close()

if __name__ == "__main__":
    main()
