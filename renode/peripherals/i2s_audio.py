# I2S Audio Peripheral for Renode
# Captures audio samples and forwards to WebSocket bridge via TCP socket
#
# Uses Renode's request object for read/write operations
# request.isInit, request.isRead, request.offset, request.value

import socket
import struct

# I2S Register offsets (STM32-like)
I2S_CR1 = 0x00      # Control register 1
I2S_CR2 = 0x04      # Control register 2
I2S_SR  = 0x08      # Status register
I2S_DR  = 0x0C      # Data register
I2S_CRCPR = 0x10    # CRC polynomial register
I2S_RXCRCR = 0x14   # RX CRC register
I2S_TXCRCR = 0x18   # TX CRC register
I2S_I2SCFGR = 0x1C  # I2S configuration register
I2S_I2SPR = 0x20    # I2S prescaler register
# DMA-like bulk transfer registers
I2S_DMA_ADDR = 0x24     # Buffer address in memory
I2S_DMA_COUNT = 0x28    # Sample count
I2S_DMA_TRIGGER = 0x2C  # Write to trigger send

# Status register bits
SR_RXNE = 0x01      # RX buffer not empty
SR_TXE  = 0x02      # TX buffer empty
SR_CHSIDE = 0x04    # Channel side
SR_UDR  = 0x08      # Underrun flag
SR_OVR  = 0x40      # Overrun flag
SR_BSY  = 0x80      # Busy flag

# Audio configuration
BUFFER_SIZE = 512  # samples per transfer (larger = more efficient)

# Bridge connection
BRIDGE_HOST = "127.0.0.1"
BRIDGE_PORT = 9001  # TCP port for audio data

# Peripheral state (global, persists across accesses)
if not 'i2s_state' in dir():
    i2s_state = {
        'cr1': 0,
        'cr2': 0,
        'sr': SR_TXE,  # TX empty initially
        'i2scfgr': 0,
        'i2spr': 0,
        'tx_buffer': [],
        'channel': 0,
        'sample_count': 0,
        'socket': None,
        'connected': False,
        # DMA-like transfer
        'dma_addr': 0,
        'dma_count': 0,
    }


def i2s_log(msg):
    self.Log(LogLevel.Info, "[I2S] " + msg)


def i2s_connect():
    s = i2s_state
    if s['connected']:
        return True
    try:
        s['socket'] = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s['socket'].settimeout(0.01)
        s['socket'].connect((BRIDGE_HOST, BRIDGE_PORT))
        s['socket'].setblocking(False)
        s['connected'] = True
        i2s_log("Connected to bridge at %s:%d" % (BRIDGE_HOST, BRIDGE_PORT))
        return True
    except:
        s['socket'] = None
        s['connected'] = False
        return False


def i2s_send_samples(samples):
    s = i2s_state
    if not s['connected']:
        i2s_connect()

    if s['connected']:
        try:
            header = b'AUD\x00' + struct.pack('<HH', len(samples), 0)
            data = header + struct.pack('<%dh' % len(samples), *samples)
            s['socket'].sendall(data)
            return True
        except:
            s['connected'] = False
            s['socket'] = None
            return False
    return False


# Main logic - executed on every memory access
if request.isInit:
    i2s_log("Peripheral initialized")

elif request.isRead:
    s = i2s_state
    offset = request.offset

    if offset == I2S_CR1:
        request.value = s['cr1']
    elif offset == I2S_CR2:
        request.value = s['cr2']
    elif offset == I2S_SR:
        request.value = s['sr']
        # Debug: log first few SR reads
        if s.get('sr_read_count', 0) < 5:
            s['sr_read_count'] = s.get('sr_read_count', 0) + 1
            i2s_log("SR read: 0x%02X" % s['sr'])
    elif offset == I2S_DR:
        request.value = 0  # No RX support
    elif offset == I2S_I2SCFGR:
        request.value = s['i2scfgr']
    elif offset == I2S_I2SPR:
        request.value = s['i2spr']
    else:
        request.value = 0

else:  # Write
    s = i2s_state
    offset = request.offset
    value = request.value

    if offset == I2S_CR1:
        s['cr1'] = value
    elif offset == I2S_CR2:
        s['cr2'] = value
    elif offset == I2S_DR:
        # Write to TX buffer (audio output)
        sample = value & 0xFFFF
        # Sign extend 16-bit to signed
        if sample >= 0x8000:
            sample = sample - 0x10000

        s['tx_buffer'].append(sample)
        s['sample_count'] += 1

        # Debug: log first few DR writes
        if s.get('dr_write_count', 0) < 5:
            s['dr_write_count'] = s.get('dr_write_count', 0) + 1
            i2s_log("DR write: sample=%d, buffer_len=%d" % (sample, len(s['tx_buffer'])))

        # Toggle channel (L/R)
        s['channel'] = 1 - s['channel']
        if s['channel'] == 0:
            s['sr'] = s['sr'] ^ SR_CHSIDE

        # When buffer is full, send to audio bridge
        if len(s['tx_buffer']) >= BUFFER_SIZE * 2:  # stereo
            # Debug: log first few sends
            if s.get('send_count', 0) < 5:
                s['send_count'] = s.get('send_count', 0) + 1
                i2s_log("Sending %d samples to bridge" % len(s['tx_buffer']))
            i2s_send_samples(s['tx_buffer'])
            s['tx_buffer'] = []

        # TX buffer always ready
        s['sr'] = s['sr'] | SR_TXE

    elif offset == I2S_I2SCFGR:
        s['i2scfgr'] = value
        if value & 0x0400:  # I2SE bit - I2S enable
            i2s_log("I2S enabled, config=0x%04X" % value)
            i2s_connect()
        else:
            i2s_log("I2S disabled")
    elif offset == I2S_I2SPR:
        s['i2spr'] = value
        div = value & 0xFF
        odd = (value >> 8) & 1
        if div > 0:
            i2sclk = 84000000
            actual_rate = i2sclk // ((16 * 2) * ((2 * div) + odd))
            i2s_log("Sample rate: ~%d Hz (div=%d, odd=%d)" % (actual_rate, div, odd))

    elif offset == I2S_DMA_ADDR:
        s['dma_addr'] = value

    elif offset == I2S_DMA_COUNT:
        s['dma_count'] = value

    elif offset == I2S_DMA_TRIGGER:
        # DMA trigger: read samples from memory and send in one batch
        if s['dma_addr'] != 0 and s['dma_count'] > 0:
            # Get system bus through the machine reference
            machine = self.GetMachine()
            bus = machine.SystemBus

            addr = s['dma_addr']
            count = s['dma_count']

            # Read all bytes at once using ReadBytes (much faster than loop)
            raw_bytes = bus.ReadBytes(addr, count * 2, context=None)

            # Convert bytes to signed 16-bit samples using struct
            samples = list(struct.unpack('<%dh' % count, bytes(raw_bytes)))

            # Send all samples at once
            s['dma_send_count'] = s.get('dma_send_count', 0) + 1
            if s['dma_send_count'] <= 10 or s['dma_send_count'] % 50 == 0:
                i2s_log("DMA[%d]: %d samples" % (s['dma_send_count'], len(samples)))
            i2s_send_samples(samples)

            s['sample_count'] += count
