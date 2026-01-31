# UMI-SysEx Transport 仕様（ドラフト）

バージョン: 0.1.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-29

---

## 1. 目的と範囲

本書は UMI-SysEx プロトコル群の**共通トランスポート**として、MIDI SysEx 上のメッセージ形式、7-bitエンコード、チェックサム、フロー制御、タイミング要件を定義する。
UMI-DATA のような**内容仕様**とは分離し、搬送レイヤの共通規約のみを扱う。

---

## 2. メッセージフレーム

```
F0 <Manufacturer ID> <Protocol ID> <Command> <Sequence> [Payload...] [Checksum] F7
```

| フィールド | サイズ | 説明 |
|---|---:|---|
| F0 | 1 | SysEx開始 |
| Manufacturer ID | 1 or 3 | メーカーID（2.2参照） |
| Protocol ID | 1 | UMI-SysExプロトコル識別子（2.1参照） |
| Command | 1 | プロトコル内コマンド |
| Sequence | 1 | シーケンス番号 (0-127) |
| Payload | 0-N | コマンド固有データ |
| Checksum | 0-1 | チェックサム（2.5参照） |
| F7 | 1 | SysEx終了 |

### 2.1 Protocol ID

| ID | プロトコル | 用途 |
|----|-----------|------|
| 0x01 | UMI-STDIO | Standard I/O |
| 0x02 | UMI-DFU | Firmware Update |
| 0x03 | UMI-SHELL | Interactive Shell |
| 0x04 | UMI-TEST | Automated Testing |
| 0x05 | UMI-STATUS | Status & Logging |
| 0x06 | UMI-DATA | User Data Exchange |
| 0x10-0x1F | Reserved | 将来拡張 |
| 0x20-0x7F | Vendor | ベンダー拡張 |

### 2.2 入力バリデーション（必須）

WebMIDI/USB など**外部入力は不正データを含み得る**ため、受信側は必ず以下を検証する。

- フレームの先頭が `0xF0`、末尾が `0xF7`
- SysEx 全体サイズが実装上限以下（上限は実装側で定義）
- `Manufacturer ID` / `Protocol ID` / `Command` / `Sequence` / `Payload` の各バイトが **7-bit（MSB=0）**
- 不正データは**即破棄**（応答しない）

---

---

## 3. Manufacturer ID の扱い

### 3.1 Universal Non-Real Time（推奨）

```
F0 7E <Device ID> <Sub-ID1> <Sub-ID2> ...
```

- Sub-ID1 = 0x7D (教育/開発用) または新規割り当て申請
- 標準化を目指す場合は MMA/AMEI への申請が必要

### 3.2 開発用ID

```
F0 7D ... (Educational/Development Use)
```

- 商用製品には使用不可
- プロトタイプ・開発用

### 3.3 UMI用3バイトID（暫定）

```
F0 00 7F 00 ...
```

- UMI プロジェクト固有（暫定）
- 製品化時は正式IDへ移行

---

## 4. 7-bit エンコーディング

MIDI SysExでは MSB=0 が必須。8-bit データは次の形式でエンコードする。

```
入力: 7バイト (56ビット)
出力: 8バイト (1バイト目にMSB集約)

Byte 0: 0 | b7_6 | b7_5 | b7_4 | b7_3 | b7_2 | b7_1 | b7_0
Byte 1: 0 | b6..b0 of input byte 0
Byte 2: 0 | b6..b0 of input byte 1
...
Byte 7: 0 | b6..b0 of input byte 6
```

- オーバーヘッド: 約14.3%
- 7-bit安全なバイト列のみで構成される Payload には適用不要

---

## 5. チェックサム

### 5.1 方式A: Roland互換（推奨）

```
uint8_t checksum_roland(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (0x80 - (sum & 0x7F)) & 0x7F;
}
```

- 検証: (data + checksum) の下位7ビットが0

### 5.2 方式B: XOR（互換/軽量）

```
uint8_t checksum_xor(const uint8_t* data, size_t len) {
    uint8_t xor_val = 0;
    for (size_t i = 0; i < len; i++) {
        xor_val ^= data[i];
    }
    return xor_val & 0x7F;
}
```

- 既存実装との互換用途に限定
- 新規実装は方式Aを推奨

---

## 6. フロー制御

### 6.1 Push/Poll モデル

- **Poll (デフォルト)**: 要求がある場合のみ応答
- **Push (サブスクリプション)**: 明示的な購読後に通知

### 6.2 XON/XOFF

| コマンド | 値 | 説明 |
|---------|-----|------|
| XOFF | 0x00 | 送信一時停止要求 |
| XON | 0x01 | 送信再開 |

---

## 7. 搬送経路の推奨

UMI-SysEx の搬送経路としては **USB-MIDI 1.0 / Bulk 転送** を標準推奨とする。

- 互換性が高く、主要OS/DAWでドライバレス動作
- パケット構造が単純で実装コストが低い

---

## 8. タイミング要件（推奨）

| 条件 | 推奨値 | 説明 |
|---|---:|---|
| メッセージ間遅延 | ≥20ms | 連続SysEx間の最小間隔 |
| 応答タイムアウト | 2000ms | ACK/NAK待ち最大時間 |
| バースト制限 | 4KB/100ms | 短時間の最大転送量 |

---

## 9. UMI-DATA との関係

- UMI-DATA の**内容仕様**は UMI-DATA 仕様書に定義する。
- 本トランスポートは UMI-DATA の**搬送経路**のみを提供する。
- UMI-DATA のバイナリデータは 7-bit エンコードして Payload に格納する。

---

## 10. 互換・移行メモ

- 旧 UMI SysEx 仕様では Protocol ID を省略する形式が存在する。
- UMI-SysEx では **Protocol ID を必須**とする方針で整理する。
- 旧形式は互換目的でのみ受理し、将来的に廃止予定。
