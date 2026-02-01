# USB Audio Class 仕様リファレンス

> umiusb 設計の前提知識として、UAC 1.0 / 2.0 の仕様を整理したもの。
> オーディオインターフェイスおよび電子楽器として必要な機能を網羅的にカバーする。

---

## 1. USB Audio Class の構造

### 1-1. クラス階層

```
bInterfaceClass = 0x01 (Audio)
├── bInterfaceSubClass = 0x01  Audio Control (AC)
├── bInterfaceSubClass = 0x02  Audio Streaming (AS)
└── bInterfaceSubClass = 0x03  MIDI Streaming (MS)
```

MIDI Streaming は Audio Class のサブクラスであり、独立したクラスではない。
ただし AC インターフェイスとの論理的な結合は緩く、MIDI 単体デバイスとしても成立する。

### 1-2. UAC バージョン比較

| 項目 | UAC 1.0 | UAC 2.0 | UAC 3.0 |
|------|---------|---------|---------|
| USB Speed | Full Speed | Full/High Speed | Full/High/Super Speed |
| 最大サンプルレート | 96kHz (帯域制限) | 384kHz+ | 384kHz+ |
| クロック管理 | なし (EP で制御) | Clock Source/Selector/Multiplier | 同左 |
| サンプルレート変更 | EP Sampling Freq Control | Clock Source SET CUR | 同左 |
| 割り込みEP | オプション | 推奨 | 必須 |
| Power Domain | なし | なし | あり |
| BADD | なし | なし | Basic Audio Device Definition |
| OS サポート | ◎ 全OS | ◎ 全OS | △ Windows のみ (限定的) |

**結論:** UAC 1.0 + UAC 2.0 を対象とする。UAC 3.0 は OS サポートが不十分で不要。

---

## 2. Audio Control (AC) インターフェイス

### 2-1. エンティティ (Unit / Terminal)

AC インターフェイスは以下のエンティティで構成されるトポロジーを定義する。

| エンティティ | Desc Type | UAC1 | UAC2 | 用途 |
|-------------|-----------|------|------|------|
| Input Terminal | 0x02 | ✓ | ✓ | 信号入力元 (USB、マイク等) |
| Output Terminal | 0x03 | ✓ | ✓ | 信号出力先 (USB、スピーカー等) |
| Mixer Unit | 0x04 | ✓ | ✓ | N入力→1出力のミキサー |
| Selector Unit | 0x05 | ✓ | ✓ | N入力から1つを選択 |
| Feature Unit | 0x06 | ✓ | ✓ | ボリューム、ミュート等の制御 |
| Processing Unit | 0x07 (UAC1) / 0x08 (UAC2) | ✓ | ✓ | DSP処理 (Up/Down Mix 等) |
| Extension Unit | 0x08 (UAC1) / 0x09 (UAC2) | ✓ | ✓ | ベンダー拡張 |
| Effect Unit | — | — | ✓ (0x07) | エフェクト (Reverb, Chorus 等) |
| Clock Source | — | — | ✓ (0x0A) | クロック生成元 |
| Clock Selector | — | — | ✓ (0x0B) | クロックソース選択 |
| Clock Multiplier | — | — | ✓ (0x0C) | クロック逓倍 |
| Sample Rate Converter | — | — | ✓ (0x0D) | SRC |

### 2-2. Terminal Types

USB Audio Terminal Types (audio10.pdf Table A-7, audio20.pdf Table A-7):

| 値 | 名称 | 用途 |
|----|------|------|
| 0x0101 | USB Streaming | USB ホストとの I/O |
| 0x01FF | USB Vendor Specific | ベンダー固有 |
| 0x0201 | Microphone | マイク入力 |
| 0x0202 | Desktop Microphone | 卓上マイク |
| 0x0203 | Personal Microphone | ヘッドセットマイク等 |
| 0x0204 | Omni-directional Microphone | 全指向性マイク |
| 0x0205 | Microphone Array | マイクアレイ |
| 0x0206 | Processing Microphone Array | 処理付きマイクアレイ |
| 0x0301 | Speaker | スピーカー出力 |
| 0x0302 | Headphones | ヘッドフォン |
| 0x0303 | Head Mounted Display Audio | HMD |
| 0x0304 | Desktop Speaker | 卓上スピーカー |
| 0x0305 | Room Speaker | ルームスピーカー |
| 0x0306 | Communication Speaker | 通信用スピーカー |
| 0x0307 | Low Frequency Effects Speaker | サブウーファー |
| 0x0400 | Bi-directional Undefined | 双方向 |
| 0x0401 | Handset | ハンドセット |
| 0x0402 | Headset | ヘッドセット (マイク付きヘッドフォン) |
| 0x0501 | Analog Connector | アナログライン入力 |
| 0x0502 | Digital Audio Interface | デジタル入力 (S/PDIF 等) |
| 0x0503 | Line Connector | ライン入力/出力 |
| 0x0601 | Analog Connector (Output) | アナログライン出力 |
| 0x0602 | Digital Audio Interface (Output) | デジタル出力 |
| 0x0603 | Line Connector (Output) | ライン出力 |
| 0x0605 | S/PDIF Interface | S/PDIF |
| 0x0700 | Embedded Undefined | 組み込み |
| 0x0703 | Synthesizer | シンセサイザー |
| 0x0704 | Telephone Ringer | 着信音 |
| 0x0706 | Multi-track Recorder | MTR |
| 0x0710 | Instrument | 楽器 (MIDI 楽器等) |

**オーディオインターフェイス典型例:** USB Streaming (0x0101) + Speaker (0x0301) + Microphone (0x0201)
**電子楽器典型例:** USB Streaming (0x0101) + Synthesizer (0x0703) or Instrument (0x0710)

### 2-3. Feature Unit コントロール

| コントロール | CS | UAC1 | UAC2 | 用途 |
|-------------|-----|------|------|------|
| Mute | 0x01 | ✓ | ✓ | ミュート |
| Volume | 0x02 | ✓ | ✓ | ボリューム (dB 単位、1/256dB 分解能) |
| Bass | 0x03 | ✓ | ✓ | 低音イコライザー |
| Mid | 0x04 | ✓ | ✓ | 中音イコライザー |
| Treble | 0x05 | ✓ | ✓ | 高音イコライザー |
| Graphic Equalizer | 0x06 | ✓ | ✓ | グラフィック EQ |
| Automatic Gain Control | 0x07 | ✓ | ✓ | AGC |
| Delay | 0x08 | ✓ | ✓ | ディレイ (ms 単位) |
| Bass Boost | 0x09 | ✓ | ✓ | バスブースト |
| Loudness | 0x0A | ✓ | ✓ | ラウドネス補正 |
| Input Gain | 0x0B | — | ✓ | 入力ゲイン (UAC2 追加) |
| Input Gain Pad | 0x0C | — | ✓ | 入力ゲインパッド (UAC2 追加) |
| Phase Inverter | 0x0D | — | ✓ | 位相反転 (UAC2 追加) |
| Underflow | 0x0E | — | ✓ | アンダーフロー検出 (UAC2 追加) |
| Overflow | 0x0F | — | ✓ | オーバーフロー検出 (UAC2 追加) |

**実用的に必要なもの:** Mute, Volume, Input Gain (UAC2)
**オプション:** Bass/Mid/Treble (ヘッドフォンアンプ等), AGC (マイク入力)

### 2-4. ボリューム単位

- 単位: dB × 256 (1/256 dB 分解能)
- 範囲: -127.9961 dB (0x8001) 〜 +127.9961 dB (0x7FFF)
- 無音: 0x8000 (-∞ dB)
- GET MIN / GET MAX / GET RES で対応範囲と分解能を報告
- 典型的な実装: MIN = -80dB, MAX = 0dB, RES = 1dB (0x0100)

---

## 3. Audio Streaming (AS) インターフェイス

### 3-1. Alternate Setting

AS インターフェイスは Alternate Setting で動作を切り替える:

| Alt Setting | 内容 |
|-------------|------|
| 0 | Zero Bandwidth (帯域予約なし、エンドポイントなし) |
| 1〜N | 各フォーマット/サンプルレート設定 |

ホストは再生/録音開始時に Alt Setting 1 以上に切り替え、停止時に 0 に戻す。

### 3-2. 対応フォーマット

**Format Type I (PCM):**

| フォーマット | Tag | 用途 |
|-------------|-----|------|
| PCM | 0x0001 | 整数 PCM (16/24/32bit) |
| PCM8 | 0x0002 | 8bit PCM |
| IEEE Float | 0x0003 | 32bit 浮動小数点 |
| ALaw | 0x0004 | A-law 圧縮 |
| MuLaw | 0x0005 | μ-law 圧縮 |
| DSD (UAC2) | — | Direct Stream Digital |

**UAC2 Format Type I Descriptor:**
```
bBitResolution: 実際のデータビット数 (16, 24, 32)
bSubslotSize: 1サンプルのバイト数 (2, 3, 4)
```

**実用:** PCM 16/24bit, IEEE Float 32bit で十分。DSD は特殊用途。

### 3-3. サンプルレート設定

**UAC 1.0:**
- Format Type I ディスクリプタに対応サンプルレート一覧を記述 (3バイト × N)
- 変更: SET CUR Endpoint Sampling Frequency Control (EP Control Selector 0x01)
- 連続範囲も指定可能 (min/max)

**UAC 2.0:**
- Clock Source の GET RANGE で対応範囲をホストに通知
- 変更: SET CUR Clock Source Frequency Control
- 12バイト per range (min 4B, max 4B, res 4B)

### 3-4. 同期モード

| モード | bmAttributes | 方向 | 仕組み |
|--------|-------------|------|--------|
| Synchronous | 0x0D | IN/OUT | デバイスとホストが同じクロック (SOF) |
| Adaptive | 0x09 | IN/OUT | デバイスがホストに追従 |
| Asynchronous | 0x05 | IN/OUT | デバイスが独自クロック、フィードバックで通知 |

**フィードバック:**

| 種類 | 説明 | Windows | macOS | Linux |
|------|------|---------|-------|-------|
| Explicit (専用 EP) | 別 EP でサンプルレート通知 | ✓ | ✓ | ✓ |
| Implicit (Data EP) | IN データレートで暗黙通知 | ✗ | ✓ | ✓ |

**重要:** Windows は Implicit Feedback に非対応。Async OUT は Explicit Feedback EP が必須。

**フィードバック値のフォーマット:**

| Speed | Format | 単位 |
|-------|--------|------|
| Full Speed | 10.14 固定小数点 (UAC1) / 16.16 (UAC2) | samples/frame (1ms) |
| High Speed | 12.13 固定小数点 (UAC1) / 16.16 (UAC2) | samples/microframe (125μs) |

### 3-5. パケットサイズ

**Full Speed (1ms フレーム):**

| サンプルレート | Ch | Bit | Bytes/Frame | MaxPacketSize |
|---------------|----|-----|------------|---------------|
| 44.1kHz | 2 | 16 | 44×2×2 = 176 (±4) | 180 |
| 48kHz | 2 | 16 | 48×2×2 = 192 | 196 |
| 48kHz | 2 | 24 | 48×2×3 = 288 | 294 |
| 96kHz | 2 | 24 | 96×2×3 = 576 | 582 |
| 96kHz | 2 | 16 | 96×2×2 = 384 | 388 |

FS Isochronous の最大パケットサイズは 1023 バイト。

**High Speed (125μs マイクロフレーム):**

| サンプルレート | Ch | Bit | Bytes/μFrame | MaxPacketSize |
|---------------|----|-----|-------------|---------------|
| 48kHz | 2 | 24 | 6×2×3 = 36 | 42 |
| 96kHz | 2 | 24 | 12×2×3 = 72 | 78 |
| 192kHz | 2 | 24 | 24×2×3 = 144 | 150 |
| 384kHz | 2 | 24 | 48×2×3 = 288 | 294 |

HS Isochronous は最大 1024 バイト × 3 トランザクション/μFrame。

---

## 4. クロック管理 (UAC 2.0)

### 4-1. Clock Source

| 属性 | 説明 |
|------|------|
| Clock Type | Internal Fixed / Internal Variable / Internal Programmable / External |
| Sync to SOF | SOF に同期するか否か |

**リクエスト:**

| Request | CS | 応答 |
|---------|-----|------|
| GET CUR Frequency | 0x01 | 4B (Hz) |
| SET CUR Frequency | 0x01 | — |
| GET RANGE Frequency | 0x01 | wNumSubRanges + (min, max, res)×N |
| GET CUR Validity | 0x02 | 1B (0=invalid, 1=valid) |

### 4-2. Clock Selector

複数の Clock Source から1つを選択する。

| Request | CS | 応答 |
|---------|-----|------|
| GET CUR | 0x01 | 1B (選択中の Clock Source ID) |
| SET CUR | 0x01 | — |

用途: 内部クロック ↔ 外部クロック (S/PDIF, Word Clock) の切り替え。

### 4-3. Clock Multiplier

入力クロックを逓倍/分周する。分母/分子を報告。
特殊用途のため、一般的なオーディオデバイスでは不要。

---

## 5. 割り込みエンドポイント (Status Interrupt)

### 5-1. 用途

AC インターフェイスのオプション (UAC2 では推奨) の割り込みエンドポイント。
デバイス側の状態変化をホストに非同期通知する。

### 5-2. 通知内容

| 通知 | 内容 |
|------|------|
| StatusChanged | エンティティの状態変化 (ボリューム変更、クロック変更等) |
| Memory Content Changed | メモリ内容変更 (Extension Unit) |

通知パケット:
```
bStatusType: 0=AudioControl, 1=AudioStreaming, 2=MemoryContent
bOriginator: 変化したエンティティの ID
```

ホストはこの通知を受けて該当エンティティに GET CUR を送る。

### 5-3. 実用性

- 物理ノブ等でボリュームが変化した場合にホストに通知するために必要
- クロックソースの切り替えをホストに通知
- ファームウェアが能動的に状態変更を報告できる唯一の手段
- **電子楽器/オーディオインターフェイスでは実装推奨**

---

## 6. MIDI Streaming

### 6-1. UAC における MIDI の位置づけ

MIDI Streaming (MS) は Audio Class のサブクラス 0x03 だが、AC インターフェイスとの機能的結合は薄い。
AC Header に MS インターフェイスを含める必要があるのみ。

コンポジットデバイスでは IAD (Interface Association Descriptor) で Audio + MIDI をグループ化する。

### 6-2. USB MIDI 1.0

| 項目 | 仕様 |
|------|------|
| パケットサイズ | 4バイト (CIN 1B + MIDI Data 3B) |
| エンドポイント | Bulk IN/OUT |
| Cable Number | 0-15 (最大16ポート) |
| Jack タイプ | Embedded (USB 側) / External (物理端子側) |

### 6-3. USB MIDI 2.0 (UMP)

| 項目 | 仕様 |
|------|------|
| パケットサイズ | 4/8/12/16 バイト (UMP) |
| エンドポイント | Bulk IN/OUT |
| Alt Setting 0 | MIDI 1.0 互換 (CIN パケット) |
| Alt Setting 1 | MIDI 2.0 (UMP ネイティブ) |
| Group Terminal Block | グループ→物理端子のマッピング |
| 発見プロトコル | MIDI-CI (UMP 上) |

**Alt Setting の切り替え:**
- ホストが MIDI 2.0 対応なら Alt Setting 1 に切り替え
- 非対応なら Alt Setting 0 のまま (後方互換)

---

## 7. OS 対応状況

### 7-1. OS 標準ドライバ対応

| 機能 | Windows (usbaudio2.sys) | macOS (AppleUSBAudio) | Linux (snd-usb-audio) | iOS |
|------|------------------------|----------------------|----------------------|-----|
| UAC 1.0 | ✓ | ✓ | ✓ | ✓ |
| UAC 2.0 | ✓ (Win10+) | ✓ | ✓ | ✓ |
| UAC 3.0 (BADD) | △ (限定) | ✗ | △ | ✗ |
| Async Explicit FB | ✓ | ✓ | ✓ | ✓ |
| Async Implicit FB | ✗ | ✓ | ✓ | ✓ |
| Adaptive IN | ✓ | ✓ | ✓ | ✓ |
| Synchronous | ✓ | ✓ | ✓ | ✓ |
| Sample Rate Change | ✓ | ✓ | ✓ | ✓ |
| Volume/Mute | ✓ | ✓ | ✓ | ✓ |
| Interrupt EP | ✓ | ✓ | ✓ | ? |
| USB MIDI 1.0 | ✓ | ✓ | ✓ | ✓ |
| USB MIDI 2.0 | △ (Win11 24H2+) | △ (macOS 15+) | ✓ | ✗ |

### 7-2. WinUSB / WebUSB (ドライバレス)

ドライバレス動作のために以下が必要:

| 項目 | 説明 |
|------|------|
| bcdUSB = 0x0201 | BOS ディスクリプタ対応を示す |
| BOS Descriptor | MS OS 2.0 Platform Capability + WebUSB Platform Capability |
| MS OS 2.0 Descriptor Set | Compatible ID "WINUSB" + Registry Property (DeviceInterfaceGUID) |
| WebUSB URL Descriptor | Landing Page URL (vendor request で返却) |
| Vendor Request Handling | Device が vendor request を Class に委譲 |

---

## 8. 典型的なトポロジー

### 8-1. ステレオ DAC (再生専用)

```
USB Streaming IT (0x0101) ─→ Feature Unit ─→ Speaker OT (0x0301)
                               Mute/Volume
```

### 8-2. ステレオ ADC (録音専用)

```
Microphone IT (0x0201) ─→ Feature Unit ─→ USB Streaming OT (0x0101)
                            Mute/Volume
```

### 8-3. デュプレックス (再生 + 録音)

```
USB Streaming IT ─→ FU (OUT) ─→ Speaker OT
Microphone IT    ─→ FU (IN)  ─→ USB Streaming OT
```

### 8-4. シンセサイザー

```
[内部音源]
Synthesizer IT (0x0703) ─→ Feature Unit ─→ USB Streaming OT (0x0101)
                            Mute/Volume

[USB MIDI]
MIDI IN Jack (Embedded) ←── USB Host
MIDI OUT Jack (Embedded) ──→ USB Host
```

### 8-5. マルチ入力オーディオインターフェイス

```
USB Streaming IT ─→ FU (OUT) ─→ Speaker OT
Mic IT (0x0201)  ─┐
Line IT (0x0501) ─┤→ Selector Unit ─→ FU (IN) ─→ USB Streaming OT
S/PDIF IT (0x0502)┘
```

---

## 9. 現在の umiusb 実装状況

### 9-1. 実装済み

| 機能 | UAC1 | UAC2 | ファイル |
|------|------|------|---------|
| AC Header | ✓ | ✓ | audio_interface.hh |
| Input Terminal | ✓ | ✓ | audio_interface.hh |
| Output Terminal | ✓ | ✓ | audio_interface.hh |
| Feature Unit (Mute/Volume) | ✓ | — | audio_interface.hh |
| Clock Source | — | ✓ | audio_interface.hh |
| Format Type I (PCM) | ✓ | ✓ | audio_interface.hh |
| AS General + CS Endpoint | ✓ | ✓ | audio_interface.hh |
| Alt Setting 0/1 | ✓ | ✓ | audio_interface.hh |
| Sampling Freq Control (EP) | ✓ | — | audio_interface.hh |
| Clock Freq Control | — | ✓ | audio_interface.hh |
| Async Explicit Feedback | ✓ | ✓ | audio_interface.hh |
| MIDI Streaming (USB MIDI 1.0) | ✓ | ✓ | audio_interface.hh (結合) |
| WinUSB descriptors | ✓ | ✓ | descriptor.hh |
| BOS descriptors | ✓ | ✓ | descriptor.hh |

### 9-2. 未実装

| 機能 | 優先度 | 備考 |
|------|--------|------|
| Feature Unit (UAC2) | 高 | UAC2 で FU なし — ボリューム/ミュート不可 |
| Selector Unit | 中 | 入力ソース選択 |
| Mixer Unit | 低 | N→1 ミキサー |
| Clock Selector | 中 | 内部/外部クロック切り替え |
| Clock Multiplier | 低 | 特殊用途 |
| Interrupt Endpoint | 中 | 状態変化通知 |
| Processing Unit | 低 | Up/Down Mix 等 |
| Effect Unit (UAC2) | 低 | Reverb/Chorus 等 |
| Extension Unit | 低 | ベンダー拡張 |
| USB MIDI 2.0 (UMP) | 中 | Alt Setting 1, Group Terminal Block |
| MIDI 完全分離 | 高 | AudioInterface から独立した MidiClass |
| Device の Vendor Request 委譲 | 高 | 現在全て STALL |
| Device の BOS 応答 | 高 | handle_get_descriptor に未実装 |
| Implicit Feedback | 低 | Windows 非対応のため優先度低 |
| DSD | 低 | ニッチ用途 |
