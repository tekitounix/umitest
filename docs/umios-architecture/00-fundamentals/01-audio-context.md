# 01 — AudioContext 統一仕様

## 概要

`AudioContext` は Processor の `process()` に渡される唯一の引数である。
オーディオバッファ、入力イベント、出力イベント、パラメータ状態、タイミング情報を統合する。

## struct 定義

```cpp
namespace umi {

struct AudioContext {
    // --- オーディオバッファ ---
    std::span<const sample_t* const> inputs;   // 入力バッファ配列（チャンネル数分）
    std::span<sample_t* const> outputs;        // 出力バッファ配列（チャンネル数分）

    // --- イベント ---
    std::span<const Event> input_events;       // 入力イベント（読み取り専用 span）
    EventQueue<>& output_events;               // 出力イベントキュー（push で書き込み）

    // --- タイミング ---
    uint32_t sample_rate;                   // サンプルレート (例: 48000)
    uint32_t buffer_size;                   // バッファサイズ (サンプル数)
    float dt;                               // 1.0f / sample_rate（事前計算）
    uint64_t sample_position;               // 累積サンプル位置

    // --- パラメータ状態（読み取り専用、初期化前は nullptr） ---
    const SharedParamState* params;         // パラメータ値（float × 32）
    const SharedChannelState* channel;      // MIDI チャンネル状態（16ch）
    const SharedInputState* input_state;    // ハードウェア入力状態（16入力）

    uint32_t output_event_count;            // 書き込まれた出力イベント数

    // --- バッファアクセス（span ベース） ---
    // 未接続チャンネルは空の span を返す（安全・長さ情報付き）
    std::span<const sample_t> input(size_t ch) const noexcept {
        return ch < inputs.size() && inputs[ch]
            ? std::span<const sample_t>{inputs[ch], buffer_size}
            : std::span<const sample_t>{};
    }
    std::span<sample_t> output(size_t ch) const noexcept {
        return ch < outputs.size() && outputs[ch]
            ? std::span<sample_t>{outputs[ch], buffer_size}
            : std::span<sample_t>{};
    }
};

} // namespace umi
```

> **設計上の注意**:
> - `input_events` は `umi::Event` の span（読み取り専用イテレーション）
> - `output_events` は `EventQueue<>&`。push 操作が必要なため span では不十分
> - `params` / `channel` / `input_state` はポインタ型。SharedMemory 未設定時に nullptr となりうるため
> - バッファアクセスは `input(ch)` / `output(ch)` メソッドを提供（`std::span` 返却、未接続時は空 span）
>
> **span ベース API の利点**:
> - 長さ情報が付随（`out.size()` でバッファサイズ取得可能）
> - 未接続チャンネルは `empty()` で判定（nullptr チェック不要）
> - range-based for 対応
> - ホットループでは生ポインタと同等の性能（[ベンチマーク実証済み](../99-proposals/bench-span-vs-ptr.md)）

## オーディオバッファ

### アクセス方法

```cpp
void process(umi::AudioContext& ctx) {
    // span 経由（推奨 — 長さ情報付き、未接続は empty()）
    auto out_l = ctx.output(0);
    auto out_r = ctx.output(1);
    if (out_l.empty()) return;

    for (uint32_t i = 0; i < out_l.size(); ++i) {
        out_l[i] = /* ... */;
        if (!out_r.empty()) out_r[i] = out_l[i];
    }

    // range-based for も使用可能
    // for (auto& sample : out_l) { sample = ...; }
}
```

### 寿命ルール

- `inputs` / `outputs` の span は**当該 `process()` 呼び出し中のみ有効**
- span やその `data()` ポインタを保持してはならない（次の呼び出しで無効になる）
- バッファサイズはバックエンドにより異なる（組み込み: 固定、Plugin: ホスト依存）

### チャンネル数

チャンネル数はアプリケーション形式とバックエンドで決まる。

| ターゲット | 入力 | 出力 | 備考 |
|-----------|------|------|------|
| シンセ (.umia) | 0 | 1-2 | MIDI 入力のみ |
| エフェクト (.umia) | 1-2 | 1-2 | オーディオスルー |
| Plugin (VST3 等) | ホスト依存 | ホスト依存 | バスレイアウトによる |
| モジュラー | N | M | CV / オーディオ混在 |

存在しないチャンネルへのアクセスは `ctx.input(ch)` / `ctx.output(ch)` が**空の span** を返す（`empty() == true`）。

## 入力イベント

`input_events` は当該バッファ区間に発生したイベントの配列。

### umi::Event 構造

```cpp
namespace umi {

enum class EventType : uint8_t {
    MIDI,          // MIDI メッセージ
    PARAM,         // パラメータ変更
    RAW,           // 生データ
    BUTTON_DOWN,   // ボタン押下
    BUTTON_UP,     // ボタン解放
};

struct Event {  // sizeof = 24B
    port_id_t port_id = 0;          // ポート ID
    uint32_t sample_pos = 0;        // バッファ内サンプル位置 (0 ~ buffer_size-1)
    EventType type = EventType::MIDI;

    union {
        MidiData midi;              // MIDI バイト列 (3B + size)
        ParamData param;            // パラメータ ID + float 値
        RawData raw;                // 生データ (最大 8B)
        ButtonData button;          // ボタン ID
    };

    // ファクトリメソッド
    static Event make_midi(port_id_t port, uint32_t pos,
                           uint8_t status, uint8_t d1, uint8_t d2 = 0);
    static Event make_param(param_id_t id, uint32_t pos, float value);
    static Event note_on(port_id_t port, uint32_t pos,
                         uint8_t channel, uint8_t note, uint8_t velocity);
    static Event note_off(port_id_t port, uint32_t pos,
                          uint8_t channel, uint8_t note, uint8_t velocity = 0);
    static Event cc(port_id_t port, uint32_t pos,
                    uint8_t channel, uint8_t cc_num, uint8_t value);
    static Event button_down(uint32_t pos, uint8_t button_id);
    static Event button_up(uint32_t pos, uint8_t button_id);
};

} // namespace umi
```

> **MidiData**: `bytes[3]` + `size` で MIDI 1.0 メッセージを格納。`is_note_on()`, `cc_number()` 等のヘルパーメソッドあり。
> **ParamData**: `param_id_t id` + `float value`。
> Event は MIDI 以外（パラメータ変更、ボタン、生データ）も統一的に扱う汎用イベントである。

### サンプル精度処理

イベントは `sample_pos` でバッファ内の発生位置を示す。サンプル精度で処理するには:

```cpp
void process(umi::AudioContext& ctx) {
    auto out = ctx.output(0);
    if (out.empty()) return;

    uint32_t event_idx = 0;

    for (uint32_t i = 0; i < out.size(); ++i) {
        // このサンプル位置のイベントを処理
        while (event_idx < ctx.input_events.size() &&
               ctx.input_events[event_idx].sample_pos == i) {
            const auto& e = ctx.input_events[event_idx];
            if (e.is_note_on()) {
                freq = midi_to_freq(e.note());
                env.trigger();
            } else if (e.is_note_off()) {
                env.release();
            }
            ++event_idx;
        }

        out[i] = osc.tick(freq * ctx.dt) * env.tick(ctx.dt);
    }
}
```

### イベントの順序保証

- `input_events` は `sample_pos` の昇順で並ぶ
- 同一 `sample_pos` 内の順序は到着順（実装依存）
- イベントは当該バッファ区間のもののみ含まれる

## 出力イベント

`output_events` は process() 内で生成する MIDI OUT 等のイベントを書き込むキュー。

```cpp
void process(umi::AudioContext& ctx) {
    // アルペジエーターの例: ノート生成
    if (should_trigger(ctx.sample_position)) {
        ctx.output_events.push(
            Event::note_on(0, current_sample_pos, 0, next_note(), 100)
        );
        ctx.output_event_count++;
    }
}
```

### 出力イベントの処理

Runtime が `process()` 完了後に `output_events[0..output_event_count)` を読み取り、バックエンドに応じて送信する:

| バックエンド | 処理 |
|-------------|------|
| 組み込み | EventRouter が sample_pos → hw_timestamp 変換し USB/UART へ送信 |
| WASM | ホスト JS が Web MIDI API で送信 |
| VST3 | processData.outputEvents に変換 |
| CLAP | output_events に変換 |

### 寿命ルール

- `output_events` キューは当該 `process()` 呼び出し中のみ書き込み可能
- `output_event_count` は process() 開始時に 0 にリセットされる
- キューの容量を超えた push は失敗する（戻り値で判定可能）

### イベントバッファの容量

`input_events` / `output_events` の容量は OS 側が初期化時（`process()` ループ開始前）に決定する。リアルタイムパスでの確保ではない。

- アプリは `RegisterProc` 時に希望容量を伝達できる。OS の上限を超える要求は clamp される
- 実際の容量は `ctx.input_events.size()` / `ctx.output_events.size()` で確認する
- 容量不足の場合は優先度の低いイベントを間引くか、複数ブロックに分散して送信する

OS 側の実装では、テンプレートパラメータで最大上限を設定し、`RegisterProc` 時にアプリの希望容量と OS 上限の min を実容量として使用する。

## パラメータ状態

`params`, `channel`, `input_state` は共有メモリ上の読み取り専用ポインタ（初期化前は nullptr）。
完全な構造体定義は [10-shared-memory.md](../10-shared-memory.md) を参照。以下は process() で使用する主要メンバーの抜粋。

### SharedParamState (136B)

```cpp
struct SharedParamState {
    float values[32];           // パラメータ値
    uint32_t changed_flags;     // 変更フラグ（ビットフィールド）
    uint32_t version;           // 更新カウンタ
};
```

process() 内での読み取り:

```cpp
if (ctx.params && ctx.params->changed_flags != 0) {
    float cutoff = ctx.params->values[PARAM_CUTOFF];
    float reso = ctx.params->values[PARAM_RESONANCE];
}
```

変換フローの詳細は [04-param-system.md](../04-param-system.md) を参照。

### SharedChannelState (64B)

```cpp
struct SharedChannelState {
    struct Channel {
        uint8_t program;        // プログラム番号
        uint8_t pressure;       // チャンネルプレッシャー
        int16_t pitch_bend;     // ピッチベンド (-8192 ~ 8191)
    };
    Channel channels[16];
};
```

### SharedInputState (32B)

```cpp
struct SharedInputState {
    uint16_t raw[16];           // ハードウェア入力の生値 (0-65535)
};
```

## タイミング情報

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `sample_rate` | uint32_t | サンプルレート（Hz） |
| `buffer_size` | uint32_t | バッファサイズ（サンプル数） |
| `dt` | float | `1.0f / sample_rate`（事前計算） |
| `sample_position` | uint64_t | 累積サンプル位置（開始から単調増加） |

- `dt` はオシレーターの位相増分計算等に使用: `phase += freq * ctx.dt`
- `sample_position` は絶対時間の計算に使用: `time_sec = sample_position * ctx.dt`
- `buffer_size` はバックエンドにより異なる場合がある。process() 内では `ctx.buffer_size` を使うこと

## process() 内の制約

| 禁止事項 | 理由 |
|---------|------|
| ヒープ割当 (`new`, `malloc`, `vector::push_back`) | リアルタイム安全性 |
| ブロッキング (`mutex`, `semaphore`, `sleep`) | デッドライン違反 |
| 例外 (`throw`) | スタック巻き戻しのコスト |
| stdio (`printf`, `cout`) | ブロッキング I/O |
| syscall | SVC 例外によるスケジューラ介入がデッドライン違反を引き起こすリスクがある。MIDI 送信は output_events 経由で Runtime に委譲すること |
| バッファ span / ポインタの保持 | 次の呼び出しで無効 |
