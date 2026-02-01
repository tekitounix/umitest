# 02 — Processor / Controller モデル

## 概要

UMI アプリケーションは **Processor** と **Controller** の 2 つの役割で構成される。

| 役割 | 責務 | 実行コンテキスト | 禁止事項 |
|------|------|----------------|---------|
| Processor | オーディオ処理、入力イベント処理 | オーディオレート（Realtime タスク） | ヒープ割当、ブロッキング、I/O、例外 |
| Controller | 設定変更、UI 更新、レイヤー切り替え | 制御レート（User タスク / main） | 高頻度処理（オーディオレート） |

## Processor

### ProcessorLike concept

Processor は concept で定義される。継承不要、vtable 不要。

```cpp
namespace umi {

template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};

} // namespace umi
```

`process(AudioContext&)` メソッドを持つ任意の構造体が Processor になれる。

### 最小実装

```cpp
struct Volume {
    float gain = 1.0f;

    void process(umi::AudioContext& ctx) {
        const float* in = ctx.input_buffer(0);
        float* out = ctx.output_buffer(0);
        if (!in || !out) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * gain;
        }
    }
};
```

### 登録

```cpp
int main() {
    static Volume vol;
    umi::register_processor(vol);
    // ...
}
```

`register_processor()` は syscall（組み込み）または バックエンド固有の登録（WASM / Plugin）で Processor を Runtime に渡す。
登録後、Runtime がオーディオコールバックのたびに `process()` を呼ぶ。

### process() の契約

1. **入力**: `AudioContext&` の `input_events`, `params`, `channel`, `input`, オーディオバッファ
2. **出力**: オーディオバッファへの書き込み、`output_events` への書き込み
3. **副作用なし（I/O 禁止）**: LED、ディスプレイ、ログ、ファイル等への直接出力は行わない
4. **リアルタイム安全**: ヒープ割当・ブロッキング・例外・stdio 禁止
5. **バッファ寿命**: `AudioContext` の参照・ポインタを保持しない

### 状態の公開

Processor の内部状態を Controller に公開するには `std::atomic` を使う。

パラメータシステム (`SharedParamState`) は Controller → Processor 方向の制御入力を担うが、Processor 内部で計算された派生状態（現在の発音数、LFO 位相、エンベロープ値等）を Controller に伝える手段は提供しない。これらの値は:

- 毎バッファ更新される（イベントキューでは帯域を浪費する）
- 最新値のみ必要（途中の値は不要）
- 遅延に寛容（表示用途なので数ms の遅れは許容される）

この特性には `std::atomic` による上書き型の公開が最も適している。イベントは「発生した事実」を伝えるものであり、「現在の状態」の継続的な伝達には向かない。

Processor (Audio Task) と Controller (Control Task) は異なるタスクで並行実行されるため、`std::atomic` なしでの読み書きは C++ 標準上 data race（未定義動作）となる。Cortex-M4 では 32bit アラインド書き込みが HW レベルでアトミックなため実害は出にくいが、WASM (SharedArrayBuffer) や Plugin (マルチスレッドホスト) では実際に問題になりうる。`memory_order_relaxed` であればコストはほぼゼロなので、正しく書いておくべきである。

```cpp
struct Synth {
    // Controller から読み取れる公開状態
    std::atomic<float> current_frequency{440.0f};
    std::atomic<uint8_t> active_voices{0};

    void process(umi::AudioContext& ctx) {
        // ... 処理 ...

        // バッファ末尾で状態を公開
        current_frequency.store(freq, std::memory_order_relaxed);
        active_voices.store(voices, std::memory_order_relaxed);
    }
};
```

> **なぜ `store(val, relaxed)` か:**
> - `std::atomic` の `operator=` は `memory_order_seq_cst`（フルバリア）と等価。Cortex-M4 では `DMB` 命令が追加される。
> - Processor→Controller の一方向の状態公開では順序保証が不要なため、`memory_order_relaxed` で十分。通常の `STR` 命令のみで済み、オーディオコールバック内の不要なオーバーヘッドを避けられる。

### Controller からの読み取り

公開状態は **表示専用の読み取り** に限定する。Controller から Processor への制御はイベントシステム（`umi::send_param_request()` / `umi::set_app_config()` 等）を経由すること。

```cpp
// OK: 表示用途（ディスプレイ更新、LED 制御等）
void update_display(Synth& synth) {
    float freq = synth.current_frequency.load(std::memory_order_relaxed);
    uint8_t voices = synth.active_voices.load(std::memory_order_relaxed);
    draw_status(freq, voices);
}

// NG: 公開状態を読んで制御に使う
void bad_example(Synth& synth) {
    if (synth.active_voices.load(std::memory_order_relaxed) > 8) {
        synth.some_member = 0.5f;  // 直接書き込みは data race
    }
}

// OK: 制御はイベントシステム経由
void good_example(Synth& synth) {
    if (synth.active_voices.load(std::memory_order_relaxed) > 8) {
        umi::send_param_request({PARAM_GAIN, 0.5f});  // OS が仲介
    }
}
```

公開状態を制御判断の入力に使うこと自体は問題ないが、Processor への書き戻しは必ずイベントシステムを経由する。

## Controller

### 実行モデル

Controller はアプリケーションの `main()` 関数として実行される。
イベントループまたはコルーチンで制御フローを記述する。

C++20 コルーチン (`co_await` / `co_return`) は言語機能のみで標準ランタイムを持たない。UMI は独自の軽量コルーチンランタイム（`umi::Task<T>`, `umi::Scheduler`）を提供しており、ヒープ不使用・リアルタイム安全な協調マルチタスクが可能である。

#### 使い分け

| 方式 | 適するケース |
|------|-------------|
| イベントループ | 単一の制御フローで済む場合（シンプルなエフェクト、単機能シンセ等）。状態管理が明示的で見通しが良い |
| コルーチン | 複数の独立した制御フローを並行させたい場合（UI 更新 + イベント処理 + LED アニメーション等）。各タスクを独立した関数として記述でき、状態マシンの手書きを避けられる |

単純なアプリではイベントループで十分であり、不要な複雑さを持ち込むべきではない。並行処理が必要になった時点でコルーチンへ移行すればよい。

#### イベントループ方式

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    umi::set_app_config(SYNTH_CONFIG);

    while (true) {
        auto ev = umi::wait_event();

        switch (ev.type) {
        case umi::EventType::Shutdown:
            return 0;

        case umi::EventType::ControlEvent: {
            auto& cev = ev.control;
            if (cev.type == ControlEventType::INPUT_CHANGE) {
                handle_input(cev.input_id, cev.value);
            }
            break;
        }

        case umi::EventType::Timer:
            update_display(synth);
            break;
        }
    }
}
```

#### コルーチン方式

```cpp
umi::Task<void> event_task(Synth& synth) {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        handle_event(synth, ev);
    }
}

umi::Task<void> display_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(33ms);
        update_display(synth);
    }
}

int main() {
    static Synth synth;
    umi::register_processor(synth);

    umi::Scheduler<4> sched;
    sched.spawn(event_task(synth));
    sched.spawn(display_task(synth));
    sched.run();

    return 0;
}
```

### Controller の責務

- **設定変更**: `umi::set_app_config()`, `umi::set_route_table()` 等
- **UI 更新**: LED、ディスプレイ等の出力（共有メモリ経由）
- **レイヤー切り替え**: ボタン入力に応じて AppConfig を切り替え
- **MIDI Learn**: CC 番号とパラメータの動的マッピング
- **SysEx 処理**: `umi::read_sysex()` / `umi::send_sysex()`

### Controller から呼べる API

| API | 用途 | Syscall Nr |
|-----|------|-----------|
| `umi::exit(code)` | アプリケーション終了 | 0 |
| `umi::register_processor(proc)` | Processor 登録 | 2 |
| `umi::wait_event(mask, timeout)` | イベント待機（ブロッキング） | 10 |
| `umi::get_time()` | 現在時刻（マイクロ秒） | 11 |
| `umi::sleep(duration)` | 指定時間スリープ | 12 |
| `umi::set_app_config(cfg)` | AppConfig 一括適用 | 20 |
| `umi::set_route_table(rt)` | RouteTable 設定 | 21 |
| `umi::set_param_mapping(pm)` | ParamMapping 設定 | 22 |
| `umi::set_input_mapping(im)` | InputParamMapping 設定 | 23 |
| `umi::configure_input(cfg)` | 入力モード設定 | 24 |
| `umi::send_param_request(req)` | パラメータ変更要求 | 25 |
| `umi::read_sysex(buf, len, &src)` | SysEx 受信 | 32 |
| `umi::send_sysex(data, len, dest)` | SysEx 送信 | 33 |
| `umi::log(msg)` | デバッグログ出力 | 50 |

Syscall 番号の詳細は [06-syscall.md](06-syscall.md) を参照。

上記の API はアプリケーションコードから見て全ターゲット共通である。内部実装はバックエンドが差し替える（組み込み: SVC syscall、WASM: import 関数、Plugin: 直接呼び出し）が、アプリ側のコードは変更不要。詳細は [08-backend-adapters.md](08-backend-adapters.md) を参照。

## EventType

Controller が `wait_event()` で受信するイベントの型。

```cpp
namespace umi {

enum class EventType : uint8_t {
    // システム
    Shutdown = 0,       // アプリ終了要求
    Timer = 1,          // タイマーティック

    // 制御イベント
    ControlEvent = 10,  // ControlEventQueue からのイベント

    // オーディオ（通常は直接受信しない）
    AudioReady = 20,    // オーディオバッファ準備完了
};

} // namespace umi
```

> **旧ドキュメントとの差異**:
> - 旧 APPLICATION.md の `EncoderRotate`, `ButtonPress`, `MidiCC` 等の細分化された型を廃止
> - ハードウェア入力は `ControlEvent` の `INPUT_CHANGE` に統一
> - MIDI イベントは process() 内の `input_events` で処理。Controller には `ControlEvent` として届く

### ControlEvent

#### 設計原則

ControlEvent は Controller が受信する全制御イベントの統一表現である。以下の原則に従う:

1. **固定長 (8B)** — キューに入る全イベントが同一サイズ
2. **固定長データは inline** — ControlEvent の union に直接格納（UMP32、InputEvent）
3. **可変長データは通知 + API** — ControlEvent は到着通知のみ、本体は `umi::read_sysex()` 等で取得
4. **通常アプリは INPUT_CHANGE のみ扱えばよい** — OS が MIDI CC をハードウェア入力と同じ形式に変換

#### MIDI の Controller への配送モード

RouteTable で `ROUTE_CONTROL` フラグが立った MIDI メッセージの Controller への届け方は 2 モードある:

| モード | RouteFlags | Controller に届く形式 | 用途 |
|--------|-----------|---------------------|------|
| 変換モード（デフォルト） | `ROUTE_CONTROL` | `INPUT_CHANGE` (id + uint16_t value) | シンセ、エフェクト等の通常アプリ |
| RAW モード | `ROUTE_CONTROL_RAW` | `MIDI` (UMP32 そのまま) | MIDI ルーター、MIDI モニター等 |

**変換モード**では OS（EventRouter）が以下の変換を行う:

- MIDI CC → `{INPUT_CHANGE, id = MIDI_CC_BASE + cc_number, value = cc_value << 9}`
- MIDI チャンネルは RouteTable でフィルタ済み（Controller はチャンネルを意識不要）
- Controller から見ると、ハードウェアノブも MIDI CC も同じ `INPUT_CHANGE` として届く

**RAW モード**では UMP32 がそのまま `{MIDI, midi = ump32}` として届く。チャンネル情報を含む完全な MIDI メッセージが必要なアプリ（MIDI ルーター等）向け。

#### SysEx の扱い

SysEx は可変長（最大数百バイト）のため ControlEvent の 8B union に収まらない。そのため通知と取得を分離する:

1. OS が SysEx を受信・再組み立て（SysExAssembler）
2. OS プロトコル SysEx（DFU、シェル等）は OS が消費。アプリに届かない
3. アプリ宛 SysEx は `SYSEX_RECEIVED` 通知を ControlEventQueue に投入
4. Controller が `umi::read_sysex(buf, len, &src)` で本体を取得
5. MIDI ルーターで全 SysEx を受け取りたい場合は、RouteTable で SysEx を `ROUTE_CONTROL` に設定

#### 構造定義

```cpp
enum class ControlEventType : uint8_t {
    MIDI = 0,               // MIDI メッセージ（RAW モード時のみ使用）
    INPUT_CHANGE = 1,       // ハードウェア入力変化 / MIDI CC 変換後
    MODE_CHANGE = 2,        // システムモード変更
    SYSEX_RECEIVED = 3,     // SysEx 到着通知（本体は read_sysex() で取得）
};

struct InputEvent {
    uint8_t id;             // 入力 ID (0–15: HW, MIDI_CC_BASE+n: MIDI CC)
    uint8_t _pad;
    uint16_t value;         // 正規化値 (0x0000–0xFFFF)
};

struct SysexNotification {
    uint8_t source;         // ソース ID (USB=0, UART=1, ...)
    uint8_t _pad;
    uint16_t length;        // SysEx バイト数
};

struct ControlEvent {
    ControlEventType type;
    uint8_t _pad[3];                            // 4B アライメント
    union {
        umidi::UMP32 midi;                      // 4B: MIDI（RAW モード）
        InputEvent input;                       // 4B: 入力変化
        SysexNotification sysex;                // 4B: SysEx 到着通知
        struct { uint8_t mode; } mode;          // 1B: モード変更
    };
};
// sizeof = 8B
```

#### 入力値の正規化

ハードウェア入力・MIDI CC とも `uint16_t` (0x0000–0xFFFF) に正規化される:

| 入力ソース | 正規化 | id 範囲 |
|-----------|--------|---------|
| ボタン (GPIO) | デバウンス後 → 0x0000 (OFF) / 0xFFFF (ON) | 0–15 |
| ノブ (ADC 12bit) | `adc_value << 4` | 0–15 |
| エンコーダ | ステップ位置 → 0x0000–0xFFFF | 0–15 |
| CV (ADC 12bit) | `adc_value << 4` | 0–15 |
| MIDI CC（変換モード） | `cc_value << 9` (7bit → 16bit) | MIDI_CC_BASE + cc_number |

Controller のハンドリング例:

```cpp
void handle_control_event(const ControlEvent& ev) {
    switch (ev.type) {
    case ControlEventType::INPUT_CHANGE:
        // ハードウェア入力も MIDI CC も同じパスで処理
        handle_input(ev.input.id, ev.input.value);
        break;

    case ControlEventType::MIDI:
        // RAW モード: UMP32 を直接処理（MIDI ルーター等）
        handle_raw_midi(ev.midi);
        break;

    case ControlEventType::SYSEX_RECEIVED: {
        // SysEx 到着通知 → 本体を取得
        uint8_t buf[256];
        uint8_t src;
        int len = umi::read_sysex(buf, sizeof(buf), &src);
        if (len > 0) handle_sysex(buf, len, src);
        break;
    }

    case ControlEventType::MODE_CHANGE:
        handle_mode(ev.mode.mode);
        break;
    }
}
```

## スレッド安全性

Processor（Audio Task）と Controller（Control Task）は異なるタスクで並行実行される。

### ルール

1. **Processor → Controller**: `std::atomic` で状態を公開（バッファ末尾で store）
2. **Controller → Processor**: `SharedParamState` 経由（Runtime が仲介）。直接書き込み禁止
3. **共有データの更新粒度**: バッファ境界で行う（サンプル単位の atomic 更新は避ける）

### 禁止パターン

```cpp
// NG: Controller から Processor のメンバを直接書き換え
synth.cutoff = 0.5f;  // データレースの危険

// OK: set_app_config / send_param_request 経由
umi::send_param_request({PARAM_CUTOFF, 0.5f});
```

## アプリケーション構造のパターン

### パターン 1: 最小構成（エフェクト）

```cpp
struct Volume {
    float gain = 1.0f;
    void process(umi::AudioContext& ctx) { /* ... */ }
};

int main() {
    static Volume vol;
    umi::register_processor(vol);
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

### パターン 2: シンセ + Controller

```cpp
struct Synth {
    std::atomic<float> lfo_phase_out{0.0f};
    void process(umi::AudioContext& ctx) { /* ... */ }
};

int main() {
    static Synth synth;
    umi::register_processor(synth);
    umi::set_app_config(SYNTH_CONFIG);

    while (true) {
        auto ev = umi::wait_event(umi::event::ControlEvent | umi::event::Timer, 16000);
        if (ev.type == umi::EventType::Shutdown) return 0;
        // UI 更新、レイヤー切り替え等
    }
}
```

### パターン 3: コルーチン（複数並行タスク）

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);

    umi::Scheduler<4> sched;
    sched.spawn(event_task(synth));
    sched.spawn(display_task(synth));
    sched.spawn(led_task());
    sched.run();
    return 0;
}
```

### パターン 4: ヘッドレス構成（Processor + AppConfig のみ）

Controller ロジックを一切書かず、Processor と AppConfig だけでアプリを構成するパターン。MIDI CC → パラメータ変換は EventRouter が機械的に処理するため、Controller の介入は不要である。

```cpp
// my_filter.hh — Processor のみ
struct MyFilter {
    void process(umi::AudioContext& ctx) {
        float cutoff = ctx.params.values[0];  // CC#74 から自動変換
        float reso = ctx.params.values[1];    // CC#71 から自動変換
        // ... フィルタ処理 ...
    }
};

// my_config.hh — 設定のみ
constexpr umi::AppConfig MY_CONFIG = [] {
    auto cfg = umi::default_app_config();
    cfg.route_table.control_change[74] = ROUTE_PARAM;
    cfg.route_table.control_change[71] = ROUTE_PARAM;
    cfg.param_mapping.control_change[74] = {0, {}, 0.0f, 1.0f};
    cfg.param_mapping.control_change[71] = {1, {}, 0.0f, 1.0f};
    return cfg;
}();

// main.cc — 最小 Controller
int main() {
    static MyFilter filter;
    umi::register_processor(filter);
    umi::set_app_config(MY_CONFIG);
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

この main() はボイラープレートであり、ターゲット別のデフォルト main テンプレートとして提供できる。開発者は Processor と AppConfig のみを記述すればよい。

#### パラメータメタデータと Plugin 公開

Processor は `HasParams` concept により、パラメータの正式な定義（名前、値域、デフォルト値、カーブ）を公開できる:

```cpp
// lib/umios/core/processor.hh に既存
struct ParamDescriptor {
    param_id_t id = 0;
    std::string_view name;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    ParamCurve curve = ParamCurve::Auto;  // 名前と範囲から自動推定
};

template<typename T>
concept HasParams = requires(const T& p) {
    { p.params() } -> std::convertible_to<std::span<const ParamDescriptor>>;
};
```

ヘッドレス構成での実装例:

```cpp
struct MyFilter {
    static constexpr ParamDescriptor param_descriptors[] = {
        {0, "Cutoff",    0.5f, 20.0f, 20000.0f, ParamCurve::Log},
        {1, "Resonance", 0.0f, 0.0f,  1.0f,     ParamCurve::Linear},
    };

    std::span<const ParamDescriptor> params() const { return param_descriptors; }

    void process(umi::AudioContext& ctx) {
        float cutoff = ctx.params.values[0];
        float reso = ctx.params.values[1];
        // ...
    }
};
```

**ParamDescriptor と ParamMapping の役割の違い:**

| | ParamDescriptor | ParamMapping |
|---|---|---|
| 定義元 | Processor（`params()`） | AppConfig |
| 内容 | パラメータの正式な定義（名前、値域、デフォルト、カーブ） | CC/入力からパラメータへの変換ルール |
| 値域 | min_value / max_value（実値の上下限） | range_low / range_high（正規化範囲 0.0–1.0） |
| 用途 | バリデーション、カーブ変換、Plugin 公開、UI 表示 | EventRouter の値変換 |

ParamMapping の range_low / range_high は正規化範囲（0.0–1.0）で指定し、実値への変換は ParamDescriptor の denormalize() が担う。例えば Cutoff (20–20000Hz, Log) のパラメータに対し、CC#74 を正規化範囲 0.15–0.85 にマップすると、中域 (約200–8000Hz) のみを制御できる。

Plugin バックエンド（VST3/AU/CLAP）は `params()` から ParamDescriptor を取得し、ホストにパラメータリストを公開する。WASM アダプタも同様に ParamDescriptor を参照して JS 側にメタデータを公開する。組み込みでは EventRouter が ParamDescriptor の値域でクランプに使える。

この構成により、ヘッドレスアプリでも Plugin ホストのパラメータ自動化・プリセット管理と完全に統合できる。
