# MIDI Peripheral for Renode
# Uses Renode's request object for read/write operations

import socket
import struct

# Register offsets
MIDI_SR = 0x00    # Status register
MIDI_DR = 0x04    # Data register
MIDI_CR = 0x08    # Control register

# Status bits
SR_RXNE = 0x01    # RX not empty
SR_TXE = 0x02     # TX empty
SR_TC = 0x04      # Transmission complete

# Bridge connection
BRIDGE_HOST = "127.0.0.1"
BRIDGE_PORT = 9002

# Peripheral state
if not 'midi_state' in dir():
    midi_state = {
        'sr': SR_TXE | SR_TC,  # TX ready
        'cr': 0,
        'rx_buffer': [],
        'tx_buffer': [],
        'socket': None,
        'connected': False,
    }


def midi_log(msg):
    self.Log(LogLevel.Info, "[MIDI] " + msg)


def midi_connect():
    s = midi_state
    if s['connected']:
        return True
    try:
        s['socket'] = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s['socket'].settimeout(0.01)
        s['socket'].connect((BRIDGE_HOST, BRIDGE_PORT))
        s['socket'].setblocking(False)
        s['connected'] = True
        midi_log("Connected to bridge at %s:%d" % (BRIDGE_HOST, BRIDGE_PORT))
        return True
    except:
        s['socket'] = None
        s['connected'] = False
        return False


def midi_try_receive():
    s = midi_state
    if not s['connected']:
        return

    try:
        header = s['socket'].recv(8)
        if header and len(header) >= 8:
            if header[:4] == b'MID\x00':
                length = struct.unpack('<H', header[4:6])[0]
                if length > 0:
                    midi_data = s['socket'].recv(length)
                    if midi_data:
                        for byte in midi_data:
                            if isinstance(byte, int):
                                s['rx_buffer'].append(byte)
                            else:
                                s['rx_buffer'].append(ord(byte))
                        s['sr'] = s['sr'] | SR_RXNE
    except:
        pass


def midi_send(data):
    s = midi_state
    if not s['connected']:
        midi_connect()

    if s['connected']:
        try:
            header = b'MID\x00' + struct.pack('<HH', len(data), 0)
            s['socket'].sendall(header + bytes(data))
            return True
        except:
            s['connected'] = False
            s['socket'] = None
            return False
    return False


# Main logic
if request.isInit:
    midi_log("Peripheral initialized")

elif request.isRead:
    s = midi_state
    offset = request.offset

    if offset == MIDI_SR:
        midi_try_receive()
        request.value = s['sr']
    elif offset == MIDI_DR:
        if s['rx_buffer']:
            request.value = s['rx_buffer'].pop(0)
            if not s['rx_buffer']:
                s['sr'] = s['sr'] & ~SR_RXNE
        else:
            s['sr'] = s['sr'] & ~SR_RXNE
            request.value = 0
    elif offset == MIDI_CR:
        request.value = s['cr']
    else:
        request.value = 0

else:  # Write
    s = midi_state
    offset = request.offset
    value = request.value

    if offset == MIDI_CR:
        s['cr'] = value
        if value & 0x01:  # Enable
            midi_connect()
    elif offset == MIDI_DR:
        byte = value & 0xFF
        s['tx_buffer'].append(byte)

        # Send when we have a complete MIDI message
        if len(s['tx_buffer']) >= 3 or (byte & 0x80 and len(s['tx_buffer']) > 1):
            midi_send(s['tx_buffer'])
            s['tx_buffer'] = []

        s['sr'] = s['sr'] | SR_TXE | SR_TC
