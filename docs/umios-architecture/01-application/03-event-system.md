# 03 — イベントシステム

## 三層モデル

| 層 | 役割 | 時間制約 | 書き込み権限 |
|----|------|----------|------------|
| **Audio** | 音を出す | サンプル精度、遅延不可 | 読み出しのみ |
| **Control** | UI や状態遷移 | 数ms〜数十ms 遅延を許容 | Request のみ発行 |
| **System** | 時間と仲裁 | — | 唯一の書き込み権限 |

**核心原則: Audio に効く変更は必ず System を経由して確定する。**

Control や外部入力が直接 Audio の状態を書き換えることはない。
System が時刻を確定し、値を検証し、共有メモリに書き込む。
Audio と Control はそれを読むだけ。

## EventRouter

System 層の中核サービス。RawInput の受信からキュー/共有メモリへの書き込みまでを一貫して処理する。

**責務:**
1. タイムスタンプ確定（hw_timestamp → sample_pos）
2. RouteTable 参照 → 経路決定
3. ParamMapping 参照 → 値変換 → SharedParamState 書き込み
4. 各キュー（AudioEventQueue / ControlEventQueue / Stream）への投入
5. output_events の逆方向処理（sample_pos → hw_timestamp → USB/UART 送信）
6. ダブルバッファの swap（ブロック境界）

EventRouter は System Service（OS が提供するサービス群の総称）の一つである。他の System Service には Shell、Driver 等がある。

### 意味付けの分離

EventRouter はメッセージの意味を知らない。意味を与えるのは Controller（アプリ）。

EventRouter は「このメッセージをどこに送るか」のルールを機械的に実行するだけ。
ルールは Controller が syscall で登録する（RouteTable）。

```
EventRouter が知ること:
  「これは UMP32 で command=0xB0（CC）、cc_number=74」
  「RouteTable を引くと ROUTE_CONTROL」
  → ControlEventQueue に送る

EventRouter が知らないこと:
  「CC#74 は filter cutoff である」
  「このアプリではノブ1 が CC#74 にマッピングされている」
```

## 全体フロー

```
┌──────────────────────────────────────────────────────┐
│ 入力ソース                                            │
│                                                       │
│   USB MIDI    UART MIDI    ボタン    ノブ/CV    BLE   │
│   (ISR/DMA)  (DMA/poll)  (GPIO)   (ADC/DMA)  (将来)  │
│       │          │          │         │          │    │
│       ▼          ▼          ▼         ▼          ▼    │
│   ┌───────────────────────────────────────────────┐  │
│   │ 正規化                                         │  │
│   │  MIDI: Parser → UMP32                          │  │
│   │  ボタン: debounce → uint16_t (0/0xFFFF)        │  │
│   │  ノブ/CV: ADC → uint16_t (0x0000〜0xFFFF)      │  │
│   └───────────────────────────────────────────────┘  │
│       │                                               │
│       ▼                                               │
│   RawInputQueue (hw_timestamp 付き)                    │
└──────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────┐
│ EventRouter (System Service の一つ)                      │
│                                                       │
│   1. タイムスタンプ確定 (sample_pos)                     │
│   2. RouteTable 参照 → 経路決定                        │
│   3. ParamMapping 参照 → 値変換                        │
│   4. ParamSetRequest 処理 → 共有メモリ書き込み          │
│                                                       │
│       ┌──────────┬──────────┬──────────┐              │
│       ▼          ▼          ▼          ▼              │
│   AudioEvent  SharedParam ControlEvent  Stream        │
│   Queue       State       Queue        (共有)         │
└──────────────────────────────────────────────────────┘
         │          │          │          │
         ▼          ▼          ▼          ▼
┌──────────────────────────────────────────────────────┐
│ Application (アプリ側)                                 │
│                                                       │
│   Processor::process()                                │
│     読む: input_events, params, channel, input        │
│     書く: output_events, オーディオバッファ             │
│                                                       │
│   Controller (main)                                   │
│     読む: ControlEvent (wait_event 経由)               │
│     書く: ParamSetRequest, RouteTable 等 (syscall)    │
└──────────────────────────────────────────────────────┘
```

## RawInput

RawInputQueue の各エントリ。全入力ソースで共通のフォーマット。

```cpp
struct RawInput {
    uint32_t hw_timestamp;   // イベント受信時点の時刻（μs、steady_clock ベース）
    uint8_t source_id;       // 入力ソース (USB=0, UART=1, GPIO=2, ...)
    uint8_t size;            // payload サイズ
    uint8_t payload[6];      // UMP32(4B) or InputEvent(4B) 等
};
// sizeof = 12B
```

hw_timestamp から sample_pos への変換は EventRouter が行う。

## RouteTable

Controller が「どのメッセージをどこに送るか」を宣言するテーブル。
System はこのテーブルを機械的に参照するだけで、意味を解釈しない。

### 経路フラグ

```cpp
enum RouteFlags : uint8_t {
    ROUTE_NONE        = 0,
    ROUTE_AUDIO       = 1 << 0,   // AudioEventQueue（process() の input_events へ）
    ROUTE_CONTROL     = 1 << 1,   // ControlEventQueue（MIDI CC は INPUT_CHANGE に変換して配送）
    ROUTE_STREAM      = 1 << 2,   // Stream（全メッセージ記録、将来用）
    ROUTE_PARAM       = 1 << 3,   // ParamMapping 経由で SharedParamState に書き込み
    ROUTE_CONTROL_RAW = 1 << 4,   // ControlEventQueue（UMP32 をそのまま配送、変換なし）
};
```

複数フラグを同時に立てられる。例: `ROUTE_AUDIO | ROUTE_PARAM`

#### ROUTE_CONTROL vs ROUTE_CONTROL_RAW

| フラグ | Controller に届く形式 | 用途 |
|--------|---------------------|------|
| `ROUTE_CONTROL` | `INPUT_CHANGE` (id + uint16_t value) | 通常アプリ（シンセ、エフェクト等） |
| `ROUTE_CONTROL_RAW` | `MIDI` (UMP32 そのまま) | MIDI ルーター、MIDI モニター等 |

`ROUTE_CONTROL` では OS が以下の変換を行う:
- MIDI CC → `{INPUT_CHANGE, id=MIDI_CC_BASE+cc_number, value=cc_value<<9}`
- MIDI チャンネルは RouteTable でフィルタ済みのため、Controller はチャンネルを意識不要
- ハードウェア入力も MIDI CC も同じ `INPUT_CHANGE` として統一的に扱える

`ROUTE_CONTROL_RAW` では変換なしに UMP32 を `{MIDI, midi=ump32}` として配送する。チャンネル情報を含む完全な MIDI メッセージが必要なアプリ向け。

両フラグを同時に立てることはできない（`ROUTE_CONTROL` が優先される）。

### テーブル構造

```cpp
struct RouteTable {
    // チャンネルメッセージ: command (8種) × channel (16)
    RouteFlags channel_voice[8][16];   // 128B

    // CC 番号ごとの経路（ROUTE_NONE 以外なら channel_voice より優先）
    RouteFlags control_change[128];       // 128B

    // システムメッセージ (0xF0-0xFF)
    RouteFlags system[16];             //  16B
};
// 合計: 272B
```

### ルックアップ

```cpp
RouteFlags lookup_route(const umidi::UMP32& ump) const {
    if (ump.is_midi1_cv()) {
        uint8_t cmd_idx = (ump.command() >> 4) - 8;
        uint8_t ch = ump.channel();
        RouteFlags flags = channel_voice[cmd_idx][ch];

        if (cmd_idx == 3) {  // CC の場合
            RouteFlags ovr = control_change[ump.cc_number()];
            if (ovr != ROUTE_NONE) flags = ovr;
        }
        return flags;
    }
    if (ump.is_system()) {
        return system[ump.status() & 0x0F];
    }
    return ROUTE_NONE;
}
```

ARM Cortex-M4 で 5-8 命令。272B は L1 キャッシュに収まる。

### デフォルト RouteTable

| メッセージ | デフォルト経路 | 根拠 |
|-----------|--------------|------|
| NoteOn/Off (0x80/0x90) | `ROUTE_AUDIO` | 音の状態変更 |
| Poly Pressure (0xA0) | `ROUTE_AUDIO` | 音の状態変更 |
| CC (0xB0) | `ROUTE_CONTROL` | アプリ依存 |
| Program Change (0xC0) | `ROUTE_CONTROL` | プリセット選択 |
| Channel Pressure (0xD0) | `ROUTE_AUDIO` | 音の状態変更 |
| Pitch Bend (0xE0) | `ROUTE_AUDIO` | 音の状態変更 |
| Clock/Transport (0xF8-FC) | `ROUTE_AUDIO` | テンポ同期 |

## 経路分類

### 1. AudioEventQueue — 状態変更イベント

順序と時刻が重要で、欠落が状態の不整合を起こすもの。

- NoteOn/Off、Pitch Bend、Channel Pressure、Poly Pressure
- `ROUTE_AUDIO` フラグで振り分けられたメッセージ

process() に `input_events` として渡される。sample_pos 付き。

```cpp
// AudioEventQueue は SPSC リングバッファ
// System が push、Audio Task が pop して AudioContext.input_events に詰める
using AudioEventQueue = RingBuffer<umi::Event, 64>;  // 24B × 64 = 1536B
```

> umi::Event のサイズ (24B) については [01-audio-context.md](../00-fundamentals/01-audio-context.md) を、SharedMemory 内の配置は [10-shared-memory.md](10-shared-memory.md) を参照。

### 2. SharedParamState — 連続値パラメータ

最新値だけが意味を持ち、中間値の欠落が問題にならないもの。

- CC → ParamMapping 経由で float 値に変換
- ハードウェア入力 → InputParamMapping 経由で float 値に変換
- Controller → ParamSetRequest 経由

process() に `params` として渡される。

### 3. ControlEventQueue — 制御イベント

Controller の `wait_event()` で受信するイベント。

- `ROUTE_CONTROL` で振り分けられた MIDI → OS が `INPUT_CHANGE` に変換して投入
- `ROUTE_CONTROL_RAW` で振り分けられた MIDI → `MIDI` (UMP32) としてそのまま投入
- ハードウェア入力変化 → `INPUT_CHANGE` として投入
- SysEx 到着 → `SYSEX_RECEIVED` 通知を投入（本体は `umi::read_sysex()` で取得）
- `InputConfig.mode == EVENT_ONLY` の入力

```cpp
using ControlEventQueue = RingBuffer<ControlEvent, 32>;  // 256B
```

#### EventRouter の MIDI→INPUT_CHANGE 変換

```cpp
void dispatch_to_control(const umidi::UMP32& ump, RouteFlags flags) {
    if (flags & ROUTE_CONTROL) {
        // 変換モード: CC を INPUT_CHANGE に正規化
        if (ump.command() == 0xB0) {
            ControlEvent ev;
            ev.type = ControlEventType::INPUT_CHANGE;
            ev.input.id = MIDI_CC_BASE + ump.cc_number();
            ev.input.value = uint16_t(ump.cc_value()) << 9;
            control_event_queue.push(ev);
        }
        // Note, Program Change 等も同様に変換可能（将来拡張）
    } else if (flags & ROUTE_CONTROL_RAW) {
        // RAW モード: UMP32 をそのまま配送
        ControlEvent ev;
        ev.type = ControlEventType::MIDI;
        ev.midi = ump;
        control_event_queue.push(ev);
    }
}
```

### 4. Stream — 全メッセージ記録（将来用）

`ROUTE_STREAM` フラグで振り分けられたメッセージの記録。
MIDI モニタ、デバッグ用途。

## 値の正規化一覧

同一の MIDI CC メッセージが RouteFlags によって異なる表現で各経路に届く。以下は CC#74 値=100 の場合の例。

| 経路 | フラグ | 届く先 | 値の表現 | 変換式 |
|------|--------|-------|---------|--------|
| ParamMapping | `ROUTE_PARAM` | `SharedParamState.values[n]` | `4536.0` (Hz, denormalize 後の実値) | 100/127=0.787 → denormalize(0.787)=4536 Hz |
| ControlEventQueue (変換) | `ROUTE_CONTROL` | `ControlEvent.input.value` | `0xC800` (uint16_t) | 100 << 9 = 0xC800 |
| AudioEventQueue | `ROUTE_AUDIO` | `umidi::Event.data` | `0xB04A6400` (UMP32 エンコード) | 生の MIDI データをそのまま格納 |
| ControlEventQueue (RAW) | `ROUTE_CONTROL_RAW` | `ControlEvent.midi` | `UMP32{0x20B04A64}` | UMP32 をそのまま配送 |

### 変換の理由

- **ROUTE_PARAM**: 連続値制御に最適化。Processor は `ctx.params.values[n]` を読むだけで実値が得られる
- **ROUTE_CONTROL**: ハードウェア入力（ADC 12bit → 16bit）と MIDI CC（7bit → 16bit）を同一スケール (0x0000–0xFFFF) に統一
- **ROUTE_AUDIO**: MIDI 処理の自由度を確保。アプリ側で `umidi::Decoder` を使った独自解釈が可能
- **ROUTE_CONTROL_RAW**: チャンネル情報を含む完全な MIDI メッセージが必要な場合（MIDI ルーター等）

### 同時送信時の注意

`ROUTE_PARAM | ROUTE_CONTROL` で同一 CC を両経路に送る場合:
- Processor が読む `ctx.params.values[n]` は **denormalize 後の実値**（例: 4536.0 Hz）
- Controller が受け取る `ControlEvent.input.value` は **0x0000–0xFFFF の uint16_t**（例: 0xC800）

両者は異なるスケールであり、相互変換するには ParamDescriptor の `normalize()` / `denormalize()` を使う。

## ダブルバッファ管理

RouteTable、ParamMapping、InputParamMapping はダブルバッファで管理する。

```
          ┌─────────┐   ┌─────────┐
active →  │ Table A  │   │ Table B  │  ← inactive (syscall で書き込み)
          └─────────┘   └─────────┘
                    ↕
            ブロック境界で swap
```

- syscall 中は inactive バッファに書き込む
- Audio ブロック境界で active/inactive を swap（フラグ1つのフリップ）
- ブロック途中で設定が変わることはない
- 変更頻度: 初期化時、MIDI Learn 時、レイヤー切替時のみ

## InputConfig — ハードウェア入力の購読モード

```cpp
struct InputConfig {
    uint8_t input_id;
    InputMode mode;
    uint16_t deadzone;      // 不感帯 (0-65535、raw 値のスケール)
    uint16_t smoothing;     // スムージング時定数 (ms、EMA フィルタの時定数)
    uint16_t threshold;     // ボタン閾値 (0-65535、この値を超えたら ON と判定)
};
// sizeof = 8B

enum class InputMode : uint8_t {
    DISABLED = 0,       // 無効
    PARAM_ONLY = 1,     // InputParamMapping 経由のみ（イベントなし）
    EVENT_ONLY = 2,     // ControlEventQueue のみ（パラメータ変換なし）
    PARAM_AND_EVENT = 3, // PARAM_ONLY | EVENT_ONLY（両方に送信）
};
```

## 使用例

### シンセアプリ（初期化時）

```cpp
void init() {
    AppConfig cfg = AppConfig::make_default();
    cfg.route_table.control_change[1]  = ROUTE_PARAM;  // CC#1 (Mod) → ParamMapping
    cfg.route_table.control_change[74] = ROUTE_PARAM;  // CC#74 (Filter) → ParamMapping
    cfg.param_mapping.entries[74] = {0, {}, 20.0f, 20000.0f};  // CC#74 → Cutoff
    umi::set_app_config(&cfg);
}
```

### MIDI Learn

RouteTable と ParamMapping の動的書き換えにより、MIDI Learn の**メカニズム**（ルーティング変更とマッピング設定）は OS が提供する。**ポリシー**（UI フロー、CC 検出ロジック、対象パラメータの選択）はアプリ側の責務である。

```cpp
void on_learn_start() {
    AppConfig cfg = current_config;
    for (int i = 0; i < 128; ++i)
        cfg.route_table.control_change[i] = ROUTE_CONTROL;  // 全 CC を Controller に
    umi::set_app_config(&cfg);
}

void on_learn_complete(uint8_t learned_cc, param_id_t target) {
    AppConfig cfg = current_config;
    for (int i = 0; i < 128; ++i)
        cfg.route_table.control_change[i] = ROUTE_NONE;
    cfg.route_table.control_change[learned_cc] = ROUTE_PARAM;
    cfg.param_mapping.entries[learned_cc] = {target, {}, 0.0f, 1.0f};
    umi::set_app_config(&cfg);

    // 割り当て結果を永続化（次回起動時に復元するため）
    save_mapping_to_flash(learned_cc, target);
}
```

Learn 結果の永続化は `umi::flash_write()` syscall（将来）または SysEx 経由でホスト側に保存する。組み込みでは Flash セクタへの書き込み、Plugin ではホストのプリセットシステム、WASM では IndexedDB 等、バックエンドにより永続化先が異なる。AppConfig の一部として保存し、起動時に `umi::set_app_config()` で復元するのが標準パターンとなる。

### 複数フラグの組み合わせ

```cpp
// CC#1 を Audio イベントとしても、ParamMapping 経由でも受け取る
rt.control_change[1] = ROUTE_AUDIO | ROUTE_PARAM;

// NoteOn を Audio と Control 両方に（Controller で MIDI モニタ表示等）
rt.channel_voice[1][0] = ROUTE_AUDIO | ROUTE_CONTROL;  // NoteOn ch1
```
