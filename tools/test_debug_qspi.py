#!/usr/bin/env python3
"""Simple test for DFU QUERY and DEBUG_QSPI commands"""

import sys
import time
import rtmidi

print("Starting...")

midi_in = rtmidi.MidiIn()
midi_out = rtmidi.MidiOut()
print("Created MIDI objects")

in_ports = midi_in.get_ports()
out_ports = midi_out.get_ports()
print(f"In ports: {in_ports}")
print(f"Out ports: {out_ports}")

in_idx = None
out_idx = None
for i, p in enumerate(in_ports):
    if 'Daisy' in p:
        in_idx = i
        break
for i, p in enumerate(out_ports):
    if 'Daisy' in p:
        out_idx = i
        break

print(f"Found: in={in_idx}, out={out_idx}")

if in_idx is None or out_idx is None:
    print("Daisy not found")
    sys.exit(1)

midi_in.open_port(in_idx)
midi_out.open_port(out_idx)
print("Ports opened")

# Clear pending messages
while midi_in.get_message():
    pass

# Simple QUERY test
msg = [0xF0, 0x7E, 0x7F, 0x00, 0x10, 0x00, 0x10, 0xF7]
midi_out.send_message(msg)
print("Sent QUERY")

start = time.time()
while time.time() - start < 2.0:
    result = midi_in.get_message()
    if result:
        data, _ = result
        print(f"Got QUERY response: {[hex(b) for b in data]}")
        break
    time.sleep(0.01)
else:
    print("No response to QUERY")
    sys.exit(1)

# DEBUG_QSPI test
time.sleep(0.1)
while midi_in.get_message():  # Clear
    pass

# FW_DEBUG_QSPI = 0x1B
msg = [0xF0, 0x7E, 0x7F, 0x00, 0x1B, 0x01, 0x1A, 0xF7]  # checksum = 0x1B ^ 0x01 = 0x1A
midi_out.send_message(msg)
print("Sent DEBUG_QSPI")

start = time.time()
while time.time() - start < 2.0:
    result = midi_in.get_message()
    if result:
        data, _ = result
        print(f"Got DEBUG_QSPI response ({len(data)} bytes):")
        print(f"  Raw: {[hex(b) for b in data]}")
        
        # Parse if it's an ACK response
        if len(data) > 8 and data[4] == 0x18:  # FW_ACK
            # Decode payload
            payload_start = 6
            payload_end = len(data) - 2
            encoded = data[payload_start:payload_end]
            
            # 7-bit decode
            decoded = []
            pos = 0
            while pos < len(encoded):
                msb = encoded[pos]
                pos += 1
                for j in range(min(7, len(encoded) - pos)):
                    b = encoded[pos]
                    pos += 1
                    if msb & (1 << j):
                        b |= 0x80
                    decoded.append(b)
            
            print(f"  Decoded ({len(decoded)} bytes): {[hex(b) for b in decoded]}")
            
            if len(decoded) >= 29:
                status = decoded[0]
                cr = (decoded[1] << 24) | (decoded[2] << 16) | (decoded[3] << 8) | decoded[4]
                dcr = (decoded[5] << 24) | (decoded[6] << 16) | (decoded[7] << 8) | decoded[8]
                sr = (decoded[9] << 24) | (decoded[10] << 16) | (decoded[11] << 8) | decoded[12]
                ccr = (decoded[13] << 24) | (decoded[14] << 16) | (decoded[15] << 8) | decoded[16]
                dlr = (decoded[17] << 24) | (decoded[18] << 16) | (decoded[19] << 8) | decoded[20]
                ar = (decoded[21] << 24) | (decoded[22] << 16) | (decoded[23] << 8) | decoded[24]
                abr = (decoded[25] << 24) | (decoded[26] << 16) | (decoded[27] << 8) | decoded[28]
                
                print(f"\nQUADSPI Registers:")
                print(f"  Status: {status:#x}")
                print(f"  CR:  {cr:#010x}")
                print(f"  DCR: {dcr:#010x}")
                print(f"  SR:  {sr:#010x}")
                print(f"  CCR: {ccr:#010x}")
                print(f"  DLR: {dlr:#010x}")
                print(f"  AR:  {ar:#010x}")
                print(f"  ABR: {abr:#010x}")
                
                # Decode CCR
                fmode = (ccr >> 26) & 0x3
                fmode_names = ['Indirect Write', 'Indirect Read', 'Auto Polling', 'Memory Mapped']
                print(f"\n  FMODE: {fmode_names[fmode]}")
                
                en = (cr >> 0) & 1
                print(f"  QUADSPI EN: {en}")
                
                busy = (sr >> 5) & 1
                print(f"  BUSY: {busy}")
        break
    time.sleep(0.01)
else:
    print("No response to DEBUG_QSPI")

print("\nDone")
