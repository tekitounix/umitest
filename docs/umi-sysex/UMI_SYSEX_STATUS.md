# UMI-STATUS 仕様（ドラフト）

バージョン: 0.2.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-29

---

## 1. 目的と範囲

UMI-STATUS は UMI デバイスの状態取得・設定・ログ/メーター通知を行うプロトコルである。
搬送は [UMI_SYSEX_TRANSPORT.md](UMI_SYSEX_TRANSPORT.md) の共通規約に従う。

---

## 2. コマンド体系

```
// System (0x20-0x2F)
PING            = 0x20
PONG            = 0x21
RESET           = 0x22
VERSION         = 0x23
STATUS_REQUEST  = 0x24
STATUS_RESPONSE = 0x25
IDENTITY_REQ    = 0x26
IDENTITY_RES    = 0x27

// Audio Status (0x30-0x3F)
AUDIO_STATUS_REQ = 0x30
AUDIO_STATUS_RES = 0x31
METER_REQ        = 0x32
METER_RES        = 0x33

// Parameter (0x40-0x4F)
PARAM_LIST_REQ  = 0x40
PARAM_LIST_RES  = 0x41
PARAM_GET       = 0x42
PARAM_VALUE     = 0x43
PARAM_SET       = 0x44
PARAM_ACK       = 0x45
PARAM_SUBSCRIBE = 0x46
PARAM_NOTIFY    = 0x47
```

---

## 3. メッセージ詳細

### 3.1 IDENTITY_RES (0x27)

```
Byte 0-2:   Manufacturer ID (3 bytes, MIDI規格)
Byte 3-4:   Device Family (uint16_t, BE)
Byte 5-6:   Device Model (uint16_t, BE)
Byte 7:     Firmware Version Major
Byte 8:     Firmware Version Minor
Byte 9-10:  Firmware Version Patch (uint16_t, BE)
Byte 11:    Protocol Version Major
Byte 12:    Protocol Version Minor
Byte 13+:   Device Name (null-terminated UTF-8)
```

### 3.2 AUDIO_STATUS_RES (0x31)

```
Byte 0-1:   DSP Load (uint16_t, ×100)
Byte 2-4:   Sample Rate (uint24_t)
Byte 5-6:   Buffer Size (uint16_t, samples)
Byte 7-8:   Latency Input (uint16_t, samples)
Byte 9-10:  Latency Output (uint16_t, samples)
Byte 11:    Polyphony Current
Byte 12:    Polyphony Max
Byte 13:    Flags
            bit 0: Audio Running
            bit 1: Clipping Detected
            bit 2: Buffer Underrun Detected
            bit 3: Buffer Overrun Detected
Byte 14-17: Underrun Count (uint32_t)
Byte 18-21: Overrun Count (uint32_t)
Byte 22-25: Uptime (uint32_t, seconds)
```

### 3.3 METER_RES (0x33)

```
Byte 0:     Channel Count (N)
For each channel (N × 4 bytes):
  Byte 0-1: Peak Level (int16_t, -32768=silence, 0=0dBFS)
  Byte 2-3: RMS Level (int16_t)
```

サブスクリプション後のみ送信する。

### 3.4 PARAM_LIST_RES (0x41)

大きなリストは複数メッセージに分割。

```
Byte 0:     Total Param Count
Byte 1:     Offset
Byte 2:     Count in this message
For each param:
  Byte 0:     Param ID
  Byte 1:     CC Number (0xFF = no CC mapping)
  Byte 2:     Type (0=int, 1=float, 2=bool, 3=enum)
  Byte 3-4:   Min Value (int16_t)
  Byte 5-6:   Max Value (int16_t)
  Byte 7-8:   Default Value (int16_t)
  Byte 9:     Flags
              bit 0: Read-only
              bit 1: Hidden
              bit 2: Automatable
  Byte 10:    Group ID
  Byte 11:    Name Length
  Byte 12+:   Name (UTF-8)
```

### 3.5 PARAM_VALUE (0x43)

```
Byte 0:     Param ID
Byte 1-2:   Value (int16_t)
```

### 3.6 PARAM_SUBSCRIBE (0x46)

```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Param ID (0xFF = all)
Byte 2:     Rate Limit (ms, 0=every change)
```

---

## 4. サブスクリプション

### 4.1 LOG_SUBSCRIBE

```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Log Level (0=ALL, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR)
Byte 2-3:   Rate Limit (ms, 0=every message)
```

### 4.2 METER_SUBSCRIBE

```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Channel Mask (bit field)
Byte 2-3:   Update Interval (ms, minimum 50ms)
```

---

## 5. フロー制御（推奨）

- 受信側バッファ逼迫時は FLOW_CTRL(XOFF) を送信
- 回復時は FLOW_CTRL(XON) を送信
- PARAM_SUBSCRIBE の Rate Limit で通知頻度を制限
