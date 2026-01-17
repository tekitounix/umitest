# Control/Status Peripheral for Renode
# Uses Renode's request object for read/write operations

import socket
import struct

# Simple JSON implementation (IronPython compatible)
def json_dumps(obj):
    if isinstance(obj, dict):
        parts = []
        for k, v in obj.items():
            if isinstance(v, bool):
                parts.append('"%s":%s' % (k, 'true' if v else 'false'))
            elif isinstance(v, (int, float)):
                parts.append('"%s":%s' % (k, v))
            else:
                parts.append('"%s":"%s"' % (k, v))
        return '{' + ','.join(parts) + '}'
    return str(obj)


def json_loads(s):
    result = {}
    s = s.strip('{}')
    for pair in s.split(','):
        if ':' in pair:
            k, v = pair.split(':', 1)
            k = k.strip().strip('"')
            v = v.strip()
            if v.startswith('"'):
                result[k] = v.strip('"')
            elif v == 'true':
                result[k] = True
            elif v == 'false':
                result[k] = False
            else:
                try:
                    result[k] = int(v)
                except:
                    try:
                        result[k] = float(v)
                    except:
                        result[k] = v
    return result


# Register offsets
CTRL_CMD = 0x00      # Command register
CTRL_STATUS = 0x04   # Status register
CTRL_DATA0 = 0x08    # Data register 0
CTRL_DATA1 = 0x0C    # Data register 1
CTRL_DATA2 = 0x10    # Data register 2
CTRL_DATA3 = 0x14    # Data register 3

# Commands
CMD_REPORT_STATE = 0x01
CMD_SET_PARAM = 0x02
CMD_GET_PARAM = 0x03

# Status bits
STATUS_CONNECTED = 0x01
STATUS_CMD_PENDING = 0x02
STATUS_PARAM_READY = 0x04

# Bridge connection
BRIDGE_HOST = "127.0.0.1"
BRIDGE_PORT = 9003

# Peripheral state
if not 'ctrl_state' in dir():
    ctrl_state = {
        'status': 0,
        'data': [0, 0, 0, 0],
        'kernel_state': {
            'uptime': 0,
            'cpuLoad': 0,
            'taskCount': 0,
            'taskReady': 0,
            'taskBlocked': 0,
            'heapUsed': 0,
            'heapTotal': 65536,
            'stackUsed': 0,
            'stackTotal': 4096,
            'audioRunning': False,
            'bufferCount': 0,
            'dropCount': 0,
            'dspLoad': 0,
            'midiRx': 0,
        },
        'pending_commands': [],
        'socket': None,
        'connected': False,
    }


def ctrl_log(msg):
    self.Log(LogLevel.Info, "[CTRL] " + msg)


def ctrl_connect():
    s = ctrl_state
    if s['connected']:
        return True
    try:
        s['socket'] = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s['socket'].settimeout(0.01)
        s['socket'].connect((BRIDGE_HOST, BRIDGE_PORT))
        s['socket'].setblocking(False)
        s['connected'] = True
        s['status'] = s['status'] | STATUS_CONNECTED
        ctrl_log("Connected to bridge at %s:%d" % (BRIDGE_HOST, BRIDGE_PORT))
        return True
    except:
        s['socket'] = None
        s['connected'] = False
        s['status'] = s['status'] & ~STATUS_CONNECTED
        return False


def ctrl_send_state():
    s = ctrl_state
    if not s['connected']:
        ctrl_connect()

    if s['connected']:
        try:
            json_bytes = json_dumps(s['kernel_state']).encode('utf-8')
            header = b'STA\x00' + struct.pack('<HH', len(json_bytes), 0)
            s['socket'].sendall(header + json_bytes)
        except:
            s['connected'] = False
            s['socket'] = None
            s['status'] = s['status'] & ~STATUS_CONNECTED


def ctrl_try_receive():
    s = ctrl_state
    if not s['connected']:
        return

    try:
        header = s['socket'].recv(8)
        if header and len(header) >= 8:
            if header[:4] == b'CMD\x00':
                length = struct.unpack('<H', header[4:6])[0]
                if length > 0:
                    json_data = s['socket'].recv(length)
                    if json_data:
                        cmd = json_loads(json_data.decode('utf-8'))
                        s['pending_commands'].append(cmd)
                        s['status'] = s['status'] | STATUS_CMD_PENDING
    except:
        pass


def ctrl_handle_command(cmd):
    s = ctrl_state

    if cmd == CMD_REPORT_STATE:
        s['kernel_state']['uptime'] = s['data'][0]
        s['kernel_state']['cpuLoad'] = s['data'][1] / 100.0
        s['kernel_state']['taskCount'] = (s['data'][2] >> 16) & 0xFFFF
        s['kernel_state']['taskReady'] = (s['data'][2] >> 8) & 0xFF
        s['kernel_state']['taskBlocked'] = s['data'][2] & 0xFF
        s['kernel_state']['heapUsed'] = s['data'][3]
        ctrl_send_state()

    elif cmd == CMD_GET_PARAM:
        if s['pending_commands']:
            cmd_data = s['pending_commands'].pop(0)
            cmd_type = cmd_data.get('type', '')

            if cmd_type == 'set-param':
                s['data'][0] = cmd_data.get('id', 0)
                value = cmd_data.get('value', 0)
                s['data'][1] = int(value * 65536)
                s['status'] = s['status'] | STATUS_PARAM_READY
            elif cmd_type == 'start':
                s['data'][0] = 1
                s['status'] = s['status'] | STATUS_PARAM_READY
            elif cmd_type == 'stop':
                s['data'][0] = 2
                s['status'] = s['status'] | STATUS_PARAM_READY

            if not s['pending_commands']:
                s['status'] = s['status'] & ~STATUS_CMD_PENDING
        else:
            s['status'] = s['status'] & ~STATUS_CMD_PENDING
            s['status'] = s['status'] & ~STATUS_PARAM_READY


# Main logic
if request.isInit:
    ctrl_log("Peripheral initialized")

elif request.isRead:
    s = ctrl_state
    offset = request.offset

    if offset == CTRL_STATUS:
        ctrl_try_receive()
        request.value = s['status']
    elif offset == CTRL_DATA0:
        request.value = s['data'][0]
    elif offset == CTRL_DATA1:
        request.value = s['data'][1]
    elif offset == CTRL_DATA2:
        request.value = s['data'][2]
    elif offset == CTRL_DATA3:
        request.value = s['data'][3]
    else:
        request.value = 0

else:  # Write
    s = ctrl_state
    offset = request.offset
    value = request.value

    if offset == CTRL_CMD:
        ctrl_handle_command(value)
    elif offset == CTRL_DATA0:
        s['data'][0] = value
    elif offset == CTRL_DATA1:
        s['data'][1] = value
    elif offset == CTRL_DATA2:
        s['data'][2] = value
    elif offset == CTRL_DATA3:
        s['data'][3] = value
