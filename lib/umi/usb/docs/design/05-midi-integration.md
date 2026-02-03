# 05 — MIDI 統合の再設計と MIDI 2.0 対応

> 元ドキュメント: [SPEED_SUPPORT_DESIGN.md](../SPEED_SUPPORT_DESIGN.md) セクション 17

## 17. MIDI 統合の再設計 — EventRouter 連携と MIDI 2.0 対応

### 17-1. 問題: 現状の MIDI API は直接操作型

現在の umiusb の MIDI API は AudioInterface に直接的な送受信メソッドを持つ:

```
現状のデータフロー:

受信:
  USB EP → MidiProcessor::process_packet()
       → on_midi callback (生バイト: cable, data, len)
       → UmidiUsbMidiAdapter → umidi::Parser → UMP32 → EventQueue

送信:
  アプリ → AudioInterface::send_midi(hal, cable, status, d1, d2)
       → USB MIDI 1.0 パケット構築 → hal.ep_write()
```

**問題点:**

| 問題 | 詳細 |
|------|------|
| EventRouter と無関係 | USB MIDI がイベントシステムの外側で処理されている |
| umios アーキテクチャと不整合 | RawInputQueue への投入が OS 側でなく adapter 側の責務 |
| MIDI 1.0 固定 | MidiProcessor が CIN ベースの USB MIDI 1.0 パケットのみ対応 |
| 送信 API が USB 固有 | `send_midi(hal, cable, status, d1, d2)` は USB MIDI 1.0 の生パケット構築 |
| 二重パース | MidiProcessor が CIN→バイト、UmidiUsbMidiAdapter が再度バイト→UMP32 と2段変換 |
| SysEx が別系統 | SysEx は MidiProcessor 内で再組み立て、独自コールバック経由 |

### 17-2. あるべき姿: EventRouter 統合

umios-architecture で定義された設計に従い、USB MIDI は
**トランスポート層の一つとして RawInputQueue にイベントを投入するだけ** にすべき。

```
あるべきデータフロー:

受信 (Host → Device):
  USB EP_MIDI_OUT → [UsbMidiTransport] → RawInputQueue
                                              │
                                              ▼
                                         EventRouter
                                         ├→ AudioEventQueue (ROUTE_AUDIO)
                                         ├→ SharedParamState (ROUTE_PARAM)
                                         ├→ ControlEventQueue (ROUTE_CONTROL)
                                         └→ Stream (ROUTE_STREAM)

送信 (Device → Host):
  Processor::output_events → EventRouter
                                 │
                                 ▼
                            [UsbMidiTransport]
                            ├→ USB: ep_write()
                            ├→ UART: DMA 送信
                            └→ (BLE: 将来)
```

### 17-3. USB MIDI トランスポートの責務分離

#### umiusb 側の責務 (トランスポート層)

USB パケットの送受信 **のみ**。メッセージの意味を解釈しない。

```cpp
/// USB MIDI トランスポート — umiusb が提供
/// umidi::MidiInput / MidiOutput concept を満たす
template<UsbMidiVersion MaxVersion = UsbMidiVersion::MIDI_2_0>
class UsbMidiTransport {
public:
    // --- MidiInput concept ---
    void poll() {}  // コールバック駆動、ポーリング不要
    bool is_connected() const { return configured_; }

    // --- MidiOutput concept ---
    // UMP32 → USB MIDI 1.0 パケットに変換して送信
    // UMP64 → USB MIDI 2.0 パケットに変換して送信 (MIDI 2.0 mode)
    template<Hal HalT>
    bool send(HalT& hal, const umidi::UMP32& ump);

    template<Hal HalT>
    bool send(HalT& hal, const umidi::UMP64& ump);  // MIDI 2.0

    // --- USB コールバック (AudioInterface/Device から呼ばれる) ---
    // USB MIDI 1.0 パケット受信時
    void on_usb_midi_rx(const uint8_t* data, uint16_t len);

    // USB MIDI 2.0 パケット受信時 (将来)
    void on_usb_midi2_rx(const uint8_t* data, uint16_t len);

    // --- OS 接続 ---
    void set_raw_input_queue(RawInputQueue* q) { queue_ = q; }
    void set_source_id(uint8_t id) { source_id_ = id; }

private:
    RawInputQueue* queue_ = nullptr;
    uint8_t source_id_ = 0;
    bool configured_ = false;
    UsbMidiVersion active_version_ = UsbMidiVersion::MIDI_1_0;
};
```

#### OS 側の責務 (EventRouter)

ルーティング、タイムスタンプ確定、値変換。umiusb を知らない。

#### アプリ側の責務

`ROUTE_CONTROL_RAW` で受け取った UMP32 を `umidi::Decoder` で解釈。
または `output_events` に UMP32/UMP64 を投入して EventRouter 経由で送信。

### 17-4. AudioInterface からの MIDI 分離

現在 AudioInterface が持つ MIDI 関連コードを分離する:

| 現在の場所 | 分離先 | 理由 |
|-----------|--------|------|
| `MidiProcessor` (audio_types.hh:507-615) | `UsbMidiTransport` | USB パケット解析はトランスポートの責務 |
| `send_midi/send_note_on/send_cc/send_sysex` (audio_interface.hh:2589-2681) | `UsbMidiTransport::send()` | 送信もトランスポートの責務 |
| `set_midi_callback/set_sysex_callback` | 不要 (RawInputQueue 経由) | OS が EventRouter でルーティング |
| `UmidiUsbMidiAdapter` (umidi_adapter.hh) | 不要 (RawInputQueue 直接投入) | 二重パースの解消 |
| MIDI EP の configure/on_rx 分岐 | AudioInterface 内に残す (EP 管理) | USB EP 構成は Audio Class の一部 |

#### 分離後の AudioInterface

```cpp
template <..., typename MidiTransport_ = NullMidiTransport>
class AudioInterface {
    MidiTransport_ midi_transport_;

    // on_rx での MIDI 処理
    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if constexpr (HAS_MIDI_OUT) {
            if (ep == EP_MIDI_OUT) {
                // トランスポートに丸投げ — パース・ルーティングは OS の責務
                midi_transport_.on_usb_midi_rx(data.data(), data.size());
                return;
            }
        }
        // Audio OUT 処理...
    }

    // 送信 — EventRouter 経由で呼ばれる
    template<typename HalT>
    void send_midi(HalT& hal, const umidi::UMP32& ump) {
        midi_transport_.send(hal, ump);
    }
};
```

### 17-5. MIDI 2.0 対応設計

#### USB MIDI 2.0 の要件

| 項目 | USB MIDI 1.0 | USB MIDI 2.0 |
|------|-------------|-------------|
| パケット形式 | 4 bytes (CIN + 3 data) | 4/8/12/16 bytes (UMP 直接) |
| 速度 | Bulk | Bulk (同一) |
| ディスクリプタ | Audio Class 1.0 MS | Audio Class 2.0 MS + Group Terminal Block |
| Alt Setting | Alt 0 のみ | Alt 0 = MIDI 1.0, Alt 1 = MIDI 2.0 |
| 内部表現 | CIN ベース | UMP ネイティブ |
| 解像度 | velocity 7bit, CC 7bit | velocity 16bit, CC 32bit |

#### UMP サイズと Message Type

```
MT=0: Utility           (32bit)  — JR Timestamp, NoOp
MT=1: System Common     (32bit)  — Timing Clock, Start/Stop 等
MT=2: MIDI 1.0 CV       (32bit)  — 従来の NoteOn/Off, CC (7bit)
MT=3: SysEx7            (64bit)  — 7bit SysEx データ
MT=4: MIDI 2.0 CV       (64bit)  — NoteOn/Off (16bit vel), CC (32bit val)
MT=5: SysEx8/MDS        (128bit) — 8bit SysEx, Mixed Data Set
```

#### UsbMidiTransport の MIDI 2.0 処理

```cpp
enum class UsbMidiVersion : uint8_t {
    MIDI_1_0 = 0,  // Alt Setting 0: USB MIDI 1.0 (CIN パケット)
    MIDI_2_0 = 1,  // Alt Setting 1: USB MIDI 2.0 (UMP ネイティブ)
};

void on_usb_midi_rx(const uint8_t* data, uint16_t len) {
    if (active_version_ == UsbMidiVersion::MIDI_2_0) {
        // UMP ネイティブ — パース不要、直接 RawInputQueue へ
        process_ump_stream(data, len);
    } else {
        // USB MIDI 1.0 — CIN パケットをパースして UMP32 に変換
        process_midi1_packets(data, len);
    }
}

void process_ump_stream(const uint8_t* data, uint16_t len) {
    uint16_t pos = 0;
    while (pos < len) {
        uint32_t word0 = read_le32(data + pos);
        uint8_t mt = (word0 >> 28) & 0x0F;
        uint8_t ump_size = ump_word_count(mt);  // MT から UMP サイズを決定

        if (ump_size == 1) {
            // 32bit UMP (MT=0,1,2)
            push_ump32_to_queue({word0});
        } else if (ump_size == 2) {
            // 64bit UMP (MT=3,4)
            uint32_t word1 = read_le32(data + pos + 4);
            push_ump64_to_queue(word0, word1);
        }
        // MT=5 (128bit) は将来対応
        pos += ump_size * 4;
    }
}

// MT → UMP ワード数
static constexpr uint8_t ump_word_count(uint8_t mt) {
    constexpr uint8_t table[] = {1,1,1,2,2,4,1,1,2,2,2,3,3,4,4,4};
    return table[mt & 0x0F];
}
```

#### RawInput の拡張 (MIDI 2.0 対応)

```cpp
struct RawInput {
    uint32_t hw_timestamp;
    uint8_t source_id;
    uint8_t size;           // 4 (UMP32) or 8 (UMP64)
    uint8_t payload[8];     // UMP32 (4B) or UMP64 (8B)
};
// sizeof = 16B (UMP64 対応)
```

#### ディスクリプタ追加

descriptor.hh に MIDI 2.0 用のディスクリプタビルダーを追加:

```cpp
namespace midi2 {

/// Group Terminal Block Descriptor (USB MIDI 2.0 仕様)
constexpr auto GroupTerminalBlock(
    uint8_t block_id,
    uint8_t direction,        // 0=Input, 1=Output, 2=Bidirectional
    uint8_t first_group,
    uint8_t num_groups,
    uint8_t protocol,         // 0x01=MIDI1.0, 0x02=MIDI2.0
    uint16_t max_sysex_size = 256
);

/// Alternate Setting 1 の Interface Descriptor (MIDI 2.0)
/// Alt 0 = MIDI 1.0 (従来), Alt 1 = MIDI 2.0 (UMP ネイティブ)
constexpr auto MidiStreamingInterface2();

}  // namespace midi2
```

### 17-6. EventRouter との接続性

#### 受信パス (USB → EventRouter)

```
USB MIDI EP (Bulk OUT)
    │
    ▼
UsbMidiTransport::on_usb_midi_rx()
    │
    ├─ MIDI 1.0: CIN パケット → parse → UMP32
    │                                      │
    ├─ MIDI 2.0: UMP ストリーム → 直接       │
    │                                      ▼
    └──────────────────────→ RawInputQueue.push({
                                hw_timestamp,     // os::timer::now_us()
                                source_id,        // USB_MIDI = 0
                                size,             // 4 or 8
                                payload           // UMP32 or UMP64
                             })
                                      │
                                      ▼
                                 EventRouter
                                 (以降は 03-event-system.md の通り)
```

#### 送信パス (EventRouter → USB)

```
Processor::output_events
    │ (sample_pos 付き UMP32/UMP64)
    ▼
EventRouter (逆方向処理)
    │ sample_pos → hw_timestamp 変換
    ▼
UsbMidiTransport::send(hal, ump)
    │
    ├─ MIDI 1.0 mode: UMP32 → CIN パケット → ep_write
    │
    └─ MIDI 2.0 mode: UMP → USB パケット直接 → ep_write
```

#### SysEx の統合

SysEx も RawInputQueue 経由に統一する:

```
USB SysEx 受信:
  MIDI 1.0: CIN 0x04-0x07 → UsbMidiTransport 内で
            UMP SysEx7 (MT=3, 64bit) に変換 → RawInputQueue
  MIDI 2.0: MT=3/5 UMP → そのまま RawInputQueue

EventRouter:
  SysExAssembler で再組み立て → 完了後にルーティング
  (05-midi.md の設計通り)
```

### 17-7. umidi ライブラリとの関係整理

```
┌─────────────────────────────────────────────────────────┐
│ umidi (lib/umidi/)                                        │
│                                                           │
│  core/ump.hh      — UMP32, UMP64 型定義                   │
│  core/parser.hh   — バイトストリーム → UMP32 パーサ         │
│  core/sysex.hh    — SysEx バッファ                         │
│  event.hh         — Event (sample_pos + UMP), EventQueue  │
│  codec/decoder.hh — テンプレート型デコーダ                   │
│  messages/        — メッセージ型定義                        │
│                                                           │
│  ※ トランスポート非依存。USB/UART/BLE を知らない            │
└──────────────────┬──────────────────────────────────────┘
                   │ uses
┌──────────────────▼──────────────────────────────────────┐
│ umiusb (lib/umiusb/)                                      │
│                                                           │
│  midi/usb_midi_transport.hh  — USB MIDI トランスポート     │
│    - USB MIDI 1.0 パケット ↔ UMP32 変換                    │
│    - USB MIDI 2.0 UMP ネイティブ転送                        │
│    - RawInputQueue への投入                                │
│    - MidiOutput concept 実装                               │
│                                                           │
│  ※ umidi::Parser, umidi::UMP32 を使用                     │
│  ※ EventRouter は知らない (RawInputQueue のみ)             │
└──────────────────┬──────────────────────────────────────┘
                   │ pushes to
┌──────────────────▼──────────────────────────────────────┐
│ umios EventRouter                                         │
│                                                           │
│  RawInputQueue → ルーティング → AudioEventQueue 等         │
│  output_events → 逆ルーティング → UsbMidiTransport::send() │
│                                                           │
│  ※ umidi::UMP32 型を使用するが、umidi のデコーダは使わない  │
│  ※ RouteTable でメッセージを分類するだけ                    │
└─────────────────────────────────────────────────────────┘
```

### 17-8. MidiProcessor の廃止と移行

現在の `MidiProcessor` (audio_types.hh:507-615) は以下の理由で廃止する:

| MidiProcessor の機能 | 移行先 | 理由 |
|---------------------|--------|------|
| CIN パース (process_packet) | UsbMidiTransport | USB 固有のパケット解析 |
| SysEx 再組み立て (sysex_buf_) | UsbMidiTransport → EventRouter の SysExAssembler | OS 側で統一処理 |
| on_midi コールバック | RawInputQueue.push() | EventRouter 経由に変更 |
| on_sysex コールバック | EventRouter → umi::read_sysex() | OS 側で統一処理 |
| status_to_cin() | UsbMidiTransport (送信用) | USB MIDI 1.0 送信に必要 |

`UmidiUsbMidiAdapter` (umidi_adapter.hh) も同時に廃止。
これにより二重パース (CIN→バイト→UMP32) が解消され、
CIN→UMP32 の1段変換になる。

### 17-9. MIDI 2.0 で必要な RouteTable の拡張

05-midi.md に記載のある方針に沿って、RouteTable に MT=4 用のテーブルを追加:

```cpp
struct RouteTable {
    // --- 既存: MIDI 1.0 (MT=2) ---
    RouteFlags channel_voice[8][16];   // command × channel
    RouteFlags control_change[128];    // CC 番号ごとのオーバーライド
    RouteFlags system[16];             // System メッセージ

    // --- 追加: MIDI 2.0 (MT=4) ---
    RouteFlags channel_voice_2[8][16]; // MIDI 2.0 Channel Voice
    RouteFlags control_change_2[128];  // MIDI 2.0 CC (7bit index、値は 32bit)
};
```

MIDI 2.0 の CC は cc_index が 7bit のままなので、テーブルサイズは同一。
値の解像度 (32bit) は SharedParamState への書き込み時に反映される。

### 17-10. ダウンコンバート方針

MIDI 2.0 メッセージ (UMP64 MT=4) が入力された場合の処理:

```
UMP64 (MT=4, MIDI 2.0 CV)
    │
    ├─ RouteTable に MT=4 用エントリがある
    │   → MT=4 のまま処理 (高解像度値を保持)
    │
    └─ RouteTable に MT=4 用エントリがない (フォールバック)
        → umidi::downconvert(UMP64) → UMP32 (MT=2)
        → MT=2 用テーブルで処理
```

ダウンコンバートは umidi ライブラリが提供する:

```cpp
namespace umidi {
/// MIDI 2.0 Channel Voice (UMP64 MT=4) → MIDI 1.0 (UMP32 MT=2) 変換
/// velocity: 16bit → 7bit, CC value: 32bit → 7bit
constexpr UMP32 downconvert(const UMP64& ump64);
}
```

### 17-11. 改訂ファイル構成 (MIDI 関連)

```
lib/umiusb/include/
├── midi/
│   ├── usb_midi_transport.hh     # UsbMidiTransport<MaxVersion>  [新規]
│   │                             #   - USB MIDI 1.0/2.0 パケット処理
│   │                             #   - RawInputQueue 投入
│   │                             #   - MidiInput/MidiOutput concept 実装
│   ├── usb_midi_descriptors.hh   # MIDI 2.0 ディスクリプタビルダー [新規]
│   │                             #   - GroupTerminalBlock
│   │                             #   - Alt Setting 1 (MIDI 2.0)
│   └── umidi_adapter.hh          # [廃止予定 → 移行期間のみ残す]

lib/umidi/include/
├── core/
│   ├── ump.hh                    # UMP32, UMP64 (既存)
│   ├── parser.hh                 # Parser, Serializer (既存)
│   └── convert.hh                # downconvert(UMP64→UMP32) [新規/拡張]
├── event.hh                      # Event, EventQueue (既存)
└── codec/
    └── decoder.hh                # テンプレートデコーダ (既存)
```

### 17-12. 実装計画への追加

セクション 10 の Phase 2 に MIDI 関連を統合:

#### Phase 2 (改訂): WinUSB + WebUSB + MIDI 再設計

1. `UsbMidiTransport` を `midi/usb_midi_transport.hh` に新規作成
   - USB MIDI 1.0 パケット → UMP32 変換 (MidiProcessor の CIN パースを移植)
   - UMP32 → USB MIDI 1.0 パケット変換 (send_midi の逆変換を移植)
   - RawInputQueue への投入 API
2. AudioInterface から MIDI 直接操作 API を段階的に除去
   - `MidiTransport_` テンプレートパラメータ追加 (デフォルト: NullMidiTransport)
   - on_rx の MIDI 分岐を MidiTransport_ に委譲
3. `umidi_adapter.hh` を非推奨化 (deprecated 警告)
4. WinUSB / WebUSB ディスクリプタ対応 (セクション 8-9)

#### Phase 4 に MIDI 2.0 を追加:

1. `UsbMidiTransport` に MIDI 2.0 (UMP ネイティブ) 受信を追加
2. `midi/usb_midi_descriptors.hh` に Group Terminal Block 等を追加
3. Alt Setting 0/1 の切り替え対応 (set_interface)
4. `umidi::downconvert()` の実装/拡張
5. RouteTable の MT=4 対応 (OS 側)
