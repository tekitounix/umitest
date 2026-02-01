# 01 — AudioContext 統一仕様

## 概要

`AudioContext` は Processor の `process()` に渡される唯一の引数である。
オーディオバッファ、入力イベント、出力イベント、パラメータ状態、タイミング情報を統合する。

## struct 定義

```cpp
namespace umi {

struct AudioContext {  // sizeof ≈ 80B (ポインタ/スパンサイズはターゲット依存)
    // --- オーディオバッファ ---
    std::span<const float* const> inputs;   // 入力バッファ配列（チャンネル数分）
    std::span<float* const> outputs;        // 出力バッファ配列（チャンネル数分）

    // --- イベント ---
    std::span<const umidi::Event> input_events;    // 入力イベント（MIDI, パラメータ変更等）
    std::span<umidi::Event> output_events;          // 出力イベント（MIDI OUT 等）
    uint32_t output_event_count;                    // 書き込まれた出力イベント数

    // --- パラメータ状態（読み取り専用） ---
    const SharedParamState& params;         // パラメータ値（float × 32）
    const SharedChannelState& channel;      // MIDI チャンネル状態（16ch）
    const SharedInputState& input;          // ハードウェア入力状態（16入力）

    // --- タイミング ---
    uint32_t sample_rate;                   // サンプルレート (例: 48000)
    uint32_t buffer_size;                   // バッファサイズ (サンプル数)
    float dt;                               // 1.0f / sample_rate（事前計算）
    uint64_t sample_position;               // 累積サンプル位置

    // --- ヘルパーメソッド ---
    const float* input_buffer(uint32_t ch) const {
        return ch < inputs.size() ? inputs[ch] : nullptr;
    }
    float* output_buffer(uint32_t ch) const {
        return ch < outputs.size() ? outputs[ch] : nullptr;
    }
};

} // namespace umi
```

> **旧ドキュメントとの差異**:
> - `input_events` に統一（旧 API_CONTEXT.md / APPLICATION.md と一致。旧 EVENT_SYSTEM_DESIGN.md の `events` から変更）
> - `output_events` を追加（旧 API_CONTEXT.md に記載があったが EVENT_SYSTEM_DESIGN.md にはなかった。新設計で正式採用）
> - バッファアクセスはメソッド `input_buffer(ch)` / `output_buffer(ch)` を提供。フィールド直接アクセスも可

## オーディオバッファ

### アクセス方法

```cpp
void process(umi::AudioContext& ctx) {
    // メソッド経由（推奨 — nullptr チェック込み）
    float* out_l = ctx.output_buffer(0);
    float* out_r = ctx.output_buffer(1);
    if (!out_l) return;

    // フィールド直接アクセス（チャンネル数が既知の場合）
    // float* out_l = ctx.outputs[0];

    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        out_l[i] = /* ... */;
        if (out_r) out_r[i] = out_l[i];
    }
}
```

### 寿命ルール

- `inputs` / `outputs` のバッファポインタは**当該 `process()` 呼び出し中のみ有効**
- ポインタを保持してはならない（次の呼び出しで無効になる）
- バッファサイズはバックエンドにより異なる（組み込み: 固定、Plugin: ホスト依存）

### チャンネル数

チャンネル数はアプリケーション形式とバックエンドで決まる。

| ターゲット | 入力 | 出力 | 備考 |
|-----------|------|------|------|
| シンセ (.umia) | 0 | 1-2 | MIDI 入力のみ |
| エフェクト (.umia) | 1-2 | 1-2 | オーディオスルー |
| Plugin (VST3 等) | ホスト依存 | ホスト依存 | バスレイアウトによる |
| モジュラー | N | M | CV / オーディオ混在 |

存在しないチャンネルへのアクセスは `input_buffer()` / `output_buffer()` が `nullptr` を返す。

## 入力イベント

`input_events` は当該バッファ区間に発生したイベントの配列。

### umidi::Event 構造

```cpp
namespace umidi {

struct Event {  // sizeof = 8B
    uint16_t sample_pos;    // バッファ内サンプル位置 (0 ~ buffer_size-1)
    uint8_t type;           // EventType
    uint8_t status;         // MIDI ステータスバイト or イベント固有
    uint32_t data;          // イベントデータ（型により解釈が異なる）

    // MIDI ヘルパー
    bool is_note_on() const;
    bool is_note_off() const;
    bool is_cc() const;
    uint8_t note() const;
    uint8_t velocity() const;
    uint8_t cc_number() const;
    uint8_t cc_value() const;
    uint8_t channel() const;
};

} // namespace umidi
```

### サンプル精度処理

イベントは `sample_pos` でバッファ内の発生位置を示す。サンプル精度で処理するには:

```cpp
void process(umi::AudioContext& ctx) {
    float* out = ctx.output_buffer(0);
    if (!out) return;

    uint32_t event_idx = 0;

    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
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

`output_events` は process() 内で生成する MIDI OUT 等のイベントを書き込む領域。

```cpp
void process(umi::AudioContext& ctx) {
    // アルペジエーターの例: ノート生成
    if (should_trigger(ctx.sample_position)) {
        uint32_t idx = ctx.output_event_count;
        if (idx < ctx.output_events.size()) {
            ctx.output_events[idx] = umidi::Event::note_on(
                current_sample_pos,  // バッファ内位置
                0,                   // channel
                next_note(),         // note
                100                  // velocity
            );
            ctx.output_event_count++;
        }
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

- `output_events` スパンは当該 `process()` 呼び出し中のみ書き込み可能
- `output_event_count` は process() 開始時に 0 にリセットされる
- スパンの容量を超えて書き込んではならない（超過分は無視される）

### イベントバッファの容量

`input_events` / `output_events` の容量は OS 側が初期化時（`process()` ループ開始前）に決定する。リアルタイムパスでの確保ではない。

- アプリは `RegisterProc` 時に希望容量を伝達できる。OS の上限を超える要求は clamp される
- 実際の容量は `ctx.input_events.size()` / `ctx.output_events.size()` で確認する
- 容量不足の場合は優先度の低いイベントを間引くか、複数ブロックに分散して送信する

OS 側の実装では、テンプレートパラメータで最大上限を設定し、`RegisterProc` 時にアプリの希望容量と OS 上限の min を実容量として使用する。

## パラメータ状態

`params`, `channel`, `input` は共有メモリ上の読み取り専用ビュー。
完全な構造体定義は [10-shared-memory.md](10-shared-memory.md) を参照。以下は process() で使用する主要メンバーの抜粋。

### SharedParamState (164B)

```cpp
struct SharedParamState {
    float values[32];           // パラメータ値
    uint32_t changed_flags;     // 変更フラグ（ビットフィールド）
    uint32_t version;           // 更新カウンタ
};
```

process() 内での読み取り:

```cpp
float cutoff = ctx.params.values[PARAM_CUTOFF];
float reso = ctx.params.values[PARAM_RESONANCE];
```

変換フローの詳細は [04-param-system.md](04-param-system.md) を参照。

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
| バッファポインタの保持 | 次の呼び出しで無効 |
