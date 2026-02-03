#!/usr/bin/env python3
"""
Debug QSPI registers via DFU SysEx command FW_DEBUG_QSPI
"""

import sys
import time
import rtmidi

# DFU Commands
FW_QUERY = 0x10
FW_ACK = 0x18
FW_DEBUG_QSPI = 0x1B

# UMI SysEx ID
UMI_SYSEX_ID = [0x7E, 0x7F, 0x00]

def find_daisy():
    """Find Daisy MIDI device"""
    midi_in = rtmidi.MidiIn()
    midi_out = rtmidi.MidiOut()
    
    in_ports = midi_in.get_ports()
    out_ports = midi_out.get_ports()
    
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
    
    if in_idx is None or out_idx is None:
        print("Error: Daisy not found")
        sys.exit(1)
    
    midi_in.open_port(in_idx)
    midi_out.open_port(out_idx)
    
    return midi_in, midi_out

def calculate_checksum(data):
    """Calculate SysEx checksum (XOR of all bytes)"""
    cs = 0
    for b in data:
        cs ^= b
    return cs & 0x7F

def encode_7bit(data):
    """Encode 8-bit data to 7-bit MIDI-safe format"""
    result = []
    pos = 0
    while pos < len(data):
        # Get up to 7 bytes
        chunk = data[pos:pos+7]
        msb_byte = 0
        for j, b in enumerate(chunk):
            if b & 0x80:
                msb_byte |= (1 << j)
        result.append(msb_byte)
        for b in chunk:
            result.append(b & 0x7F)
        pos += 7
    return result

def decode_7bit(data):
    """Decode 7-bit MIDI data to 8-bit"""
    result = []
    pos = 0
    while pos < len(data):
        msb_byte = data[pos]
        pos += 1
        chunk_len = min(7, len(data) - pos)
        for j in range(chunk_len):
            if pos >= len(data):
                break
            byte = data[pos]
            pos += 1
            if msb_byte & (1 << j):
                byte |= 0x80
            result.append(byte)
    return result

def build_sysex(cmd, seq, payload=None):
    """Build a SysEx message"""
    msg = [0xF0] + UMI_SYSEX_ID
    cmd_part = [cmd, seq & 0x7F]
    if payload:
        cmd_part.extend(encode_7bit(payload))
    checksum = calculate_checksum(cmd_part)
    msg.extend(cmd_part)
    msg.append(checksum)
    msg.append(0xF7)
    return msg

def send_and_receive(midi_out, midi_in, msg, timeout=2.0):
    """Send SysEx and wait for response"""
    # Clear any pending messages
    while midi_in.get_message():
        pass
    
    midi_out.send_message(msg)
    
    start = time.time()
    while time.time() - start < timeout:
        result = midi_in.get_message()
        if result:
            msg_data, _ = result
            if len(msg_data) >= 8 and msg_data[0] == 0xF0 and msg_data[-1] == 0xF7:
                return msg_data
        time.sleep(0.01)
    return None

def parse_registers(data):
    """Parse QUADSPI register dump"""
    if len(data) < 29:
        return None
    
    status = data[0]
    cr = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4]
    dcr = (data[5] << 24) | (data[6] << 16) | (data[7] << 8) | data[8]
    sr = (data[9] << 24) | (data[10] << 16) | (data[11] << 8) | data[12]
    ccr = (data[13] << 24) | (data[14] << 16) | (data[15] << 8) | data[16]
    dlr = (data[17] << 24) | (data[18] << 16) | (data[19] << 8) | data[20]
    ar = (data[21] << 24) | (data[22] << 16) | (data[23] << 8) | data[24]
    abr = (data[25] << 24) | (data[26] << 16) | (data[27] << 8) | data[28]
    
    return {
        'status': status,
        'CR': cr,
        'DCR': dcr,
        'SR': sr,
        'CCR': ccr,
        'DLR': dlr,
        'AR': ar,
        'ABR': abr,
    }

def decode_cr(cr):
    """Decode CR register fields"""
    en = (cr >> 0) & 1
    abort = (cr >> 1) & 1
    dmaen = (cr >> 2) & 1
    tcen = (cr >> 3) & 1
    sshift = (cr >> 4) & 1
    fthres = (cr >> 8) & 0x1F
    prescaler = (cr >> 24) & 0xFF
    
    print(f"  CR: 0x{cr:08X}")
    print(f"    EN={en} ABORT={abort} DMAEN={dmaen} TCEN={tcen} SSHIFT={sshift}")
    print(f"    FTHRES={fthres} PRESCALER={prescaler}")

def decode_dcr(dcr):
    """Decode DCR register fields"""
    ckmode = (dcr >> 0) & 1
    csht = (dcr >> 8) & 0x7
    fsize = (dcr >> 16) & 0x1F
    
    flash_size = (2 ** (fsize + 1)) if fsize else 0
    
    print(f"  DCR: 0x{dcr:08X}")
    print(f"    CKMODE={ckmode} CSHT={csht} FSIZE={fsize} (= {flash_size} bytes)")

def decode_sr(sr):
    """Decode SR register fields"""
    tef = (sr >> 0) & 1
    tcf = (sr >> 1) & 1
    ftf = (sr >> 2) & 1
    smf = (sr >> 3) & 1
    tof = (sr >> 4) & 1
    busy = (sr >> 5) & 1
    flevel = (sr >> 8) & 0x7F
    
    print(f"  SR: 0x{sr:08X}")
    print(f"    TEF={tef} TCF={tcf} FTF={ftf} SMF={smf} TOF={tof} BUSY={busy}")
    print(f"    FLEVEL={flevel}")

def decode_ccr(ccr):
    """Decode CCR register fields"""
    instruction = (ccr >> 0) & 0xFF
    imode = (ccr >> 8) & 0x3
    admode = (ccr >> 10) & 0x3
    adsize = (ccr >> 12) & 0x3
    abmode = (ccr >> 14) & 0x3
    absize = (ccr >> 16) & 0x3
    dcyc = (ccr >> 18) & 0x1F
    dmode = (ccr >> 24) & 0x3
    fmode = (ccr >> 26) & 0x3
    sioo = (ccr >> 28) & 1
    ddrm = (ccr >> 31) & 1
    
    mode_names = ['None', 'Single', 'Dual', 'Quad']
    fmode_names = ['Indirect Write', 'Indirect Read', 'Auto Polling', 'Memory Mapped']
    adsize_names = ['8-bit', '16-bit', '24-bit', '32-bit']
    
    print(f"  CCR: 0x{ccr:08X}")
    print(f"    INSTRUCTION=0x{instruction:02X} IMODE={mode_names[imode]}")
    print(f"    ADMODE={mode_names[admode]} ADSIZE={adsize_names[adsize]}")
    print(f"    ABMODE={mode_names[abmode]} ABSIZE={absize}")
    print(f"    DCYC={dcyc} DMODE={mode_names[dmode]}")
    print(f"    FMODE={fmode_names[fmode]} SIOO={sioo} DDRM={ddrm}")

def main():
    print("QSPI Register Debug Tool")
    print("=" * 50)
    
    midi_in, midi_out = find_daisy()
    print("Connected to Daisy")
    
    # Query device first
    msg = build_sysex(FW_QUERY, 0)
    response = send_and_receive(midi_out, midi_in, msg)
    if response:
        print("Device responded to QUERY")
    else:
        print("Warning: No response to QUERY")
    
    # Request QSPI debug info
    print("\nRequesting QSPI register dump...")
    msg = build_sysex(FW_DEBUG_QSPI, 1)
    response = send_and_receive(midi_out, midi_in, msg)
    
    if not response:
        print("ERROR: No response from device")
        return
    
    # Parse response
    # Skip F0, ID, ACK cmd, seq
    if len(response) < 10:
        print(f"ERROR: Response too short: {len(response)} bytes")
        return
    
    # Find payload start
    payload_start = 1 + len(UMI_SYSEX_ID) + 2  # F0 + ID + cmd + seq
    payload_end = len(response) - 2  # -checksum -F7
    
    if payload_end <= payload_start:
        print("ERROR: No payload in response")
        return
    
    encoded_payload = response[payload_start:payload_end]
    decoded = decode_7bit(encoded_payload)
    
    regs = parse_registers(decoded)
    if not regs:
        print(f"ERROR: Failed to parse registers (got {len(decoded)} bytes)")
        return
    
    print(f"\nStatus: 0x{regs['status']:02X}")
    print("\nQUADSPI Registers:")
    decode_cr(regs['CR'])
    decode_dcr(regs['DCR'])
    decode_sr(regs['SR'])
    decode_ccr(regs['CCR'])
    print(f"  DLR: 0x{regs['DLR']:08X} (Data Length)")
    print(f"  AR: 0x{regs['AR']:08X} (Address)")
    print(f"  ABR: 0x{regs['ABR']:08X} (Alternate Bytes)")
    
    # Check if memory-mapped mode is properly configured
    print("\n" + "=" * 50)
    print("Analysis:")
    
    fmode = (regs['CCR'] >> 26) & 0x3
    if fmode != 3:
        print("  WARNING: Not in Memory Mapped mode!")
    else:
        print("  OK: In Memory Mapped mode")
    
    en = (regs['CR'] >> 0) & 1
    if not en:
        print("  WARNING: QUADSPI not enabled!")
    else:
        print("  OK: QUADSPI enabled")
    
    busy = (regs['SR'] >> 5) & 1
    if busy:
        print("  WARNING: QUADSPI BUSY flag set!")
    else:
        print("  OK: QUADSPI not busy")
    
    abr = regs['ABR']
    if abr != 0xA0:
        print(f"  WARNING: ABR={abr:#x}, expected 0xA0 for continuous read")
    else:
        print("  OK: ABR set to 0xA0 (continuous read mode)")

if __name__ == "__main__":
    main()
