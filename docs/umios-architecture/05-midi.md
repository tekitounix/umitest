# 05 — MIDI 統合

## 原則

1. **UMP32 が内部表現** — 全トランスポートの出力先は UMP32 ベース
2. **OS パーサは最小限** — トランスポート層の `StreamParser` はバイト→UMP32 変換のみ。メッセージの意味を解釈しない
3. **umidi はアプリ向け** — `umidi::Decoder` のテンプレート型システムはアプリ側（ROUTE_CONTROL_RAW 等）で使用
4. **アダプタは薄く** — 各トランスポート固有のグルーコードのみ
5. **リアルタイム安全** — アダプタ・パーサ・キュー全てヒープ不使用、ロックフリー

## UMP32

UMI 内部の MIDI メッセージ表現。MIDI 1.0 チャンネルメッセージを 32bit に格納する。

```cpp
namespace umidi {

struct UMP32 {  // sizeof = 4B
    uint32_t data;

    uint8_t message_type() const;   // MT (4bit)
    uint8_t group() const;          // Group (4bit)
    uint8_t status() const;         // Status byte
    uint8_t command() const;        // Command nibble (0x80-0xF0)
    uint8_t channel() const;        // Channel (0-15)
    uint8_t byte2() const;          // Data byte 1
    uint8_t byte3() const;          // Data byte 2

    bool is_midi1_cv() const;       // MIDI 1.0 Channel Voice
    bool is_system() const;         // System Common/Realtime

    // CC ヘルパー
    uint8_t cc_number() const;
    uint8_t cc_value() const;
};

} // namespace umidi
```

## トランスポート層

### 受信アーキテクチャ

```
USB MIDI          UART MIDI         (BLE MIDI)
AudioInterface    DMA Circular      (将来)
callback          1kHz poll
    │                 │                 │
    ▼                 ▼                 ▼
┌─────────────────────────────────────────────┐
│  StreamParser (バイトストリーム → UMP32)     │
│  ※ 各トランスポートがインスタンスを持つ      │
└─────────────────────────────────────────────┘
    │                 │                 │
    ▼                 ▼                 ▼
    RawInputQueue (hw_timestamp 付き)
```

### StreamParser — OS 内部パーサ

トランスポート層が使用する軽量パーサ。MIDI 1.0 バイトストリームを UMP32 に変換するだけで、メッセージの種類を解釈しない。

```cpp
namespace umidi {

class StreamParser {  // sizeof ≈ 5B
public:
    // 1バイト入力。完全なメッセージが揃ったら true を返し out に UMP32 を格納する。
    // Running Status 対応。SysEx はステータスバイトのみ通知（本体は別経路）。
    bool feed(uint8_t byte, UMP32& out) noexcept;

private:
    uint8_t status = 0;       // Running Status
    uint8_t data[2]{};
    uint8_t count = 0;
    uint8_t expected = 0;
};

} // namespace umidi
```

ステータスバイトの上位ニブルからデータバイト数（1 or 2）を判定し、揃ったら UMP32 にパックする。メッセージ型ごとの分岐がないため、コードサイズは数十行・100〜200B 程度。

### StreamParser と umidi::Decoder の棲み分け

| | StreamParser | umidi::Decoder |
|---|---|---|
| 用途 | OS トランスポート層（EventRouter 前段） | アプリ側の MIDI 処理 |
| 機能 | バイト→UMP32 変換のみ | メッセージ型の解釈、型安全な CC、コールバック |
| メッセージ選択 | 全メッセージをパース（RouteTable が実行時に変わるため） | テンプレートで対象メッセージをコンパイル時に選択 |
| コードサイズ | ~200B | テンプレート展開に依存（SynthDecoder: 小、FullDecoder: 大） |
| 配置 | OS（カーネル） | アプリ（ユーザスペース） |

EventRouter は RouteTable を実行時に書き換え可能（MIDI Learn 等）なため、パース時点では何が必要か分からない。よって OS 側パーサは全メッセージ対応が必須だが、メッセージの意味を知る必要がないので StreamParser で十分である。

umidi::Decoder のテンプレート型システム（`SynthDecoder<NoteOn, NoteOff>` 等）は、アプリが `ROUTE_CONTROL_RAW` で受け取った UMP32 を解釈する場合や、OS を使わないスタンドアロン用途で活きる。

### MidiInput concept

```cpp
namespace umidi {

template<typename T>
concept MidiInput = requires(T& t) {
    { t.poll() } -> std::same_as<void>;
    { t.is_connected() } -> std::convertible_to<bool>;
};

} // namespace umidi
```

### USB MIDI 入力

```cpp
class UsbMidiInput {
public:
    bool is_connected() const { return connected; }
    void poll() {} // コールバック駆動、ポーリング不要

    void on_midi_rx(uint8_t cable, const uint8_t* data, uint8_t len) {
        auto ts = os::timer::now_us();  // OS 内部タイマー直接参照
        for (uint8_t i = 0; i < len; ++i) {
            UMP32 ump;
            if (parser.feed(data[i], ump)) {
                raw_input_queue->push({ts, source_id, ump});
            }
        }
    }

private:
    StreamParser parser{};
    RawInputQueue* raw_input_queue = nullptr;
    uint8_t source_id = 0;
    bool connected = false;
};
```

### UART MIDI 入力

```cpp
class UartMidiInput {
public:
    bool is_connected() const { return true; }

    void poll() {
        if (!dma_buf) return;
        auto ts = os::timer::now_us();  // OS 内部タイマー直接参照
        uint16_t write_pos = buf_size - *dma_ndtr;
        while (read_pos != write_pos) {
            UMP32 ump;
            if (parser.feed(dma_buf[read_pos], ump)) {
                raw_input_queue->push({ts, source_id, ump});
            }
            read_pos = (read_pos + 1) % buf_size;
        }
    }

    void bind(const uint8_t* buf, uint16_t size, volatile uint16_t* ndtr) {
        dma_buf = buf; buf_size = size; dma_ndtr = ndtr; read_pos = 0;
    }

private:
    StreamParser parser{};
    RawInputQueue* raw_input_queue = nullptr;
    const uint8_t* dma_buf = nullptr;
    volatile uint16_t* dma_ndtr = nullptr;
    uint16_t buf_size = 0;
    uint16_t read_pos = 0;
    uint8_t source_id = 0;
};
```

### MidiOutput concept

```cpp
template<typename T>
concept MidiOutput = requires(T& t, const UMP32& ump) {
    { t.send(ump) } -> std::convertible_to<bool>;
    { t.is_connected() } -> std::convertible_to<bool>;
};
```

送信は process() の `output_events` → EventRouter → USB/UART の順で処理される。

## 送信フロー

```
Processor
    │ output_events (sample_pos 付き)
    ▼
EventRouter
    ├── USB: AudioInterface::send_midi()
    ├── UART: Serializer → DMA 送信キュー
    └── (BLE: 将来)
```

## ジッター補正

トランスポート層は hw_timestamp のみ記録する。sample_pos の算出は EventRouter が行う。

### タイムスタンプソース

hw_timestamp は OS 内部のタイマー（`os::timer::now_us()`）を直接参照して取得する。トランスポート層は全て OS 側コード（System Task / ISR）で動作するため、syscall を経由する必要がなく、オーバーヘッドなしでμs 精度のタイムスタンプを取得できる。

アプリ側から `std::chrono::steady_clock::now()` を使う場合は `get_time` syscall 経由となるが、トランスポート層には関係しない。

μs オーダーの精度があればサンプル位置の算出に十分である（1サンプル @48kHz = ~20.8μs）。

### サンプル位置の配置

hw_timestamp から sample_pos への変換は `ROUTE_AUDIO` 経路のイベントにのみ適用される。process() の `input_events` に sample_pos 付きで渡すことで、バッファ内の正確な位置にイベントを配置できる。

追加レイテンシは発生しない。バッファサイズ分の遅延は process() の呼び出し構造から本質的に存在するものであり、その範囲内でイベントの位置を精密に決定するだけである。

他の経路では sample_pos は意味を持たない:

| 経路 | sample_pos | 理由 |
|------|-----------|------|
| `ROUTE_AUDIO` | 適用 | process() の input_events でサンプル単位の配置が可能 |
| `ROUTE_PARAM` | 不要 | SharedParamState はブロック単位の最新値上書き |
| `ROUTE_CONTROL` | 不要 | Controller の wait_event() にサンプル精度の概念がない |

配置精度はタイムスタンプの精度に依存する。OS 内部タイマーがμs 精度であれば、サンプル精度（@48kHz で ~20.8μs）の配置が可能である。

sample_pos の計算は OS のケイパビリティに依存する。hw_timestamp を取得可能なビルドでは正確な sample_pos が計算され、軽量ビルドでは sample_pos = 0（バッファ先頭）が渡される。アプリはこの違いを意識する必要がない。

### 変換式

```cpp
uint16_t hw_timestamp_to_sample_pos(
    uint64_t event_time_us, uint64_t block_start_us,
    uint32_t sample_rate, uint32_t buffer_size)
{
    uint64_t elapsed_us = event_time_us - block_start_us;
    uint32_t sample_offset = uint32_t(elapsed_us * sample_rate / 1'000'000);
    return std::min(sample_offset, buffer_size - 1);
}
```

## SysEx

SysEx の再組み立てとルーティングは EventRouter が一括処理する。
トランスポート層は UMP SysEx7 パケットをそのまま RawInputQueue に渡すだけ。

### SysExAssembler

```cpp
struct SysExAssembler {
    uint8_t buffer[256];
    uint16_t length = 0;
    bool complete = false;

    void feed(const UMP32& ump);  // SysEx7 パケットを1つずつ入力
    bool is_complete() const { return complete; }
    std::span<const uint8_t> data() const { return {buffer, length}; }
    void reset() { length = 0; complete = false; }
};
```

USB / UART 各1インスタンス。1 インスタンスあたり 256B (buffer) + 2B (length) + 1B (complete) + パディング ≈ 260B、2 インスタンスで約 520B。

### SysEx の経路

- UMI プロトコル SysEx → System Task（シェル、DFU）
- アプリ宛 SysEx → `umi::read_sysex()` で Controller が読み取り
- 送信: `umi::send_sysex()` で USB / UART に送信

## MIDI 2.0 対応方針

MIDI 2.0 (UMP MT=4, 64bit) への対応は段階的に行う:

| 項目 | 変更 | 影響範囲 |
|------|------|---------|
| USB ディスクリプタ | Group Terminal Block 追加 | umiusb |
| パケット受信 | バイトパース不要、UMP 直接キャスト | umidi transport |
| UMP64 MT=4 | velocity 16bit, CC 値 32bit | umidi::Event サイズ変更 |
| ダウンコンバート | UMP64 → UMP32 互換変換 | umidi |
| RouteTable | MT=4 用の追加テーブル | OS + syscall |
| JR Timestamp | EventRouter で hw_timestamp を置換 | EventRouter |

AudioContext と SharedParamState の API は変わらない。
Processor から見れば MIDI 2.0 でも同じ `input_events` / `params` / `channel` を読むだけ。

### JR Timestamp によるサンプル位置補正

MIDI 2.0 の Jitter Reduction (JR) Timestamp はホスト側が付与する「本来の発生時刻」である。EventRouter は RawInput に JR Timestamp が含まれていればそれを hw_timestamp の代わりに使用し、含まれていなければ hw_timestamp にフォールバックする。下流の sample_pos 算出ロジックは変わらない。
