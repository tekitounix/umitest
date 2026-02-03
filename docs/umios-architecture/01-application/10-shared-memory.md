# 10 — SharedMemory 仕様

## 概要

SharedMemory はカーネルとアプリ間で共有するメモリ領域の構造体である。
アプリは `umi::get_shared()` syscall でポインタを取得する。

本章は SharedMemory の完全な構造体定義を集約する。物理アドレス配置は [07-memory.md](../03-port/07-memory.md) を、各メンバーの使い方は [01-audio-context.md](../00-fundamentals/01-audio-context.md)・[04-param-system.md](04-param-system.md) を参照。

## SharedMemory 構造体（完全定義）

```cpp
struct SharedMemory {
    // === オーディオバッファ (Shared Audio 8KB 領域) ===
    static constexpr size_t AUDIO_BUFFER_FRAMES = 256;
    static constexpr size_t AUDIO_CHANNELS = 2;

    float audio_input[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];   // 2048B
    float audio_output[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];  // 2048B
    float mic_input[AUDIO_BUFFER_FRAMES];                      // 1024B

    // === AudioContext 情報 ===
    uint32_t sample_rate;       // 48000
    uint32_t buffer_size;       // AUDIO_BUFFER_FRAMES (256)
    float dt;                   // 1.0f / sample_rate
    uint64_t sample_position;   // 累積サンプル位置

    // === イベントキュー (Shared MIDI 2KB 領域) ===
    static constexpr size_t EVENT_QUEUE_SIZE = 64;
    umi::Event event_queue[EVENT_QUEUE_SIZE];                   // 1536B
    std::atomic<uint32_t> event_write_idx;                     // 4B
    std::atomic<uint32_t> event_read_idx;                      // 4B

    // === パラメータ状態 ===
    SharedParamState params;                                    // 136B

    // === MIDI チャンネル状態 ===
    SharedChannelState channel;                                 // 64B

    // === フラグ・I/O (Shared HwState 1KB 領域) ===
    SharedInputState input;                                     // 32B
    std::atomic<uint32_t> flags;                               // 4B
    std::atomic<uint8_t> led_state;                            // 1B
    std::atomic<uint8_t> button_pressed;                       // 1B
    std::atomic<uint8_t> button_current;                       // 1B

    // === ヒープ情報 ===
    void* heap_base = nullptr;                                 // 4B (Cortex-M)
    size_t heap_size = 0;                                      // 4B (Cortex-M)
};
// sizeof ≈ 5.9KB
```

> **旧ドキュメントとの差異**:
> - 03-port/07-memory.md にあったフラットな `std::atomic<float> params[MAX_PARAMS]` (128B) は `SharedParamState` (136B) に置換。`changed_flags` と `version` を含む正式な構造体を使用する
> - `button_pressed`, `button_current` 等のフラットフィールドは `SharedInputState` に集約すべきだが、レガシー互換のため当面維持する
> - 各内包構造体の正式定義は本章で行い、他章は抜粋のみ記載する

## 内包構造体

### SharedParamState（パラメータ値）

EventRouter のみが書き込み、Processor / Controller は読み取り専用。

```cpp
struct SharedParamState {
    float values[32];           // パラメータ値（実値、denormalize 済み）
    uint32_t changed_flags;     // 変更フラグ（ビット i = values[i] が変化）
    uint32_t version;           // 更新カウンタ（毎ブロック先頭でインクリメント）
};
// sizeof = 136B (128 + 4 + 4)
```

- `values[]` は ParamDescriptor の `denormalize()` 適用後の実値が格納される
- `changed_flags` はブロック境界でリセットされる
- 詳細な使い方は [04-param-system.md](04-param-system.md) を参照

### SharedChannelState（MIDI チャンネル状態）

MIDI チャンネルメッセージの最新値。EventRouter が更新する。

```cpp
struct SharedChannelState {
    struct Channel {
        uint8_t program;        // プログラム番号
        uint8_t pressure;       // チャンネルプレッシャー
        int16_t pitch_bend;     // ピッチベンド (-8192 ~ 8191)
    };
    Channel channels[16];
};
// sizeof = 64B
```

### SharedInputState（ハードウェア入力状態）

ADC/GPIO からの値を正規化して格納。EventRouter が更新する。

```cpp
struct SharedInputState {
    uint16_t raw[16];   // 0x0000〜0xFFFF
};
// sizeof = 32B
```

## 書き込み権限

| メンバー | カーネル (EventRouter) | アプリ (Processor) | アプリ (Controller) |
|---------|----------------------|-------------------|-------------------|
| audio_input | 書き込み | 読み取り | — |
| audio_output | 読み取り | 書き込み | — |
| mic_input | 書き込み | 読み取り | — |
| sample_rate, buffer_size, dt | 書き込み (初期化時) | 読み取り | 読み取り |
| sample_position | 書き込み | 読み取り | 読み取り |
| event_queue, write/read_idx | 書き込み/読み取り | 読み取り/書き込み | — |
| params (SharedParamState) | 書き込み | 読み取り | 読み取り |
| channel (SharedChannelState) | 書き込み | 読み取り | 読み取り |
| input (SharedInputState) | 書き込み | 読み取り | 読み取り |
| flags, led_state | 書き込み/読み取り | — | 読み取り/書き込み |
| button_pressed, button_current | 書き込み | — | 読み取り |
| heap_base, heap_size | 書き込み (初期化時) | 読み取り | 読み取り |

## メモリ領域との対応

SharedMemory の各セクションは [07-memory.md](../03-port/07-memory.md) のリンカシンボルで定義される物理アドレスに配置される。

| セクション | リンカシンボル | サイズ | 主要メンバー |
|-----------|-------------|--------|------------|
| Shared Audio | `_shared_audio_start` (0x20008000) | 8KB | audio_input, audio_output, mic_input, AudioContext 情報 |
| Shared MIDI | `_shared_midi_start` (0x2000A000) | 2KB | event_queue, write/read_idx, SharedParamState |
| Shared HwState | `_shared_hwstate_start` (0x2000A800) | 1KB | SharedChannelState, SharedInputState, flags, LED/ボタン |

> **注**: 構造体が連続メモリに配置される場合、上記のセクション境界は論理的な区分であり、コンパイラのレイアウトと完全には一致しない可能性がある。リンカスクリプトで各セクションを独立配置するか、構造体内のアライメント属性で境界を合わせる必要がある。

## アクセスパターン

### Audio Task (process()) からのアクセス

AudioContext 経由で間接的にアクセスする。直接 SharedMemory を参照しない。

```
SharedMemory.audio_input  → AudioContext.inputs
SharedMemory.audio_output → AudioContext.outputs
SharedMemory.event_queue  → AudioContext.input_events
SharedMemory.params       → AudioContext.params
SharedMemory.channel      → AudioContext.channel
SharedMemory.input        → AudioContext.input
```

詳細は [01-audio-context.md](../00-fundamentals/01-audio-context.md) を参照。

### Controller (main()) からのアクセス

- 読み取り: `umi::get_shared()` で SharedMemory ポインタを取得し、直接読む
- 書き込み: syscall 経由（`umi::send_param_request()` 等）。SharedParamState への直接書き込みは禁止

### EventRouter からの書き込み

System Task 内で動作する。同一タスク内のため排他制御は不要（SPSC 前提）。
Audio Task との共有は atomic / ダブルバッファで管理する。
