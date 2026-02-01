# 08 — バックエンド別アダプタ

## 概要

UMI のマルチターゲット設計では、Application 層（Processor + Controller）はターゲットに依存しない。
差異は全て **Backend Adapter** が吸収する。

各バックエンドの責務:

1. **AudioContext の構築** — オーディオバッファ、イベント、パラメータ状態を組み立てて `process()` に渡す
2. **イベント変換** — ターゲット固有の入力を RawInput / ControlEvent に変換する
3. **パラメータ同期** — SharedParamState への書き込みと読み出し
4. **Syscall の実装** — `umi::wait_event()` 等のアプリ API の内部実装を提供する

## ターゲット比較

| 項目 | 組み込み (Cortex-M) | WASM | Plugin (VST3/AU/CLAP) |
|------|---------------------|------|----------------------|
| AudioContext 構築 | DMA ISR → バッファ切替 | AudioWorklet process() | ホスト processBlock() |
| MIDI 入力 | USB/UART → RawInputQueue | Web MIDI API | ホスト inputEvents |
| パラメータ同期 | syscall 経由 | JS ↔ WASM 共有メモリ | ホストパラメータ自動化 |
| Syscall 実装 | SVC 例外 | WASM import | 直接関数呼び出し |
| process() 呼び出し | Audio Task (DMA 通知) | AudioWorklet スレッド | ホスト audio スレッド |
| Controller 実行 | User Task (非特権) | メインスレッド | GUI スレッド |
| メモリ保護 | MPU | WASM サンドボックス | OS プロセス分離 |
| ファイル形式 | .umia | .umim | .vst3 / .component / .clap |

## 組み込みバックエンド (Cortex-M)

### AudioContext 構築フロー

```
I2S DMA Half/Complete 割り込み
    │
    ▼
Audio Task (優先度 0: Realtime)
    ├── DMA バッファ → SharedMemory.audio_input にコピー
    ├── AudioEventQueue → input_events にコピー
    ├── AudioContext 構築
    │     inputs:         SharedMemory.audio_input
    │     outputs:        SharedMemory.audio_output
    │     input_events:   AudioEventQueue のスナップショット
    │     output_events:  ローカル配列
    │     params:         SharedParamState (読み取り専用)
    │     channel:        SharedChannelState
    │     input:          SharedInputState
    │     sample_rate:    48000
    │     buffer_size:    256 (DMA 64 × 4)
    │     dt:             事前計算
    │     sample_position: 累積カウンタ
    ├── process(ctx) 呼び出し
    ├── output_events → EventRouter へ送信
    └── SharedMemory.audio_output → DMA バッファにコピー
```

### EventRouter

System Task (優先度 1: Server) で動作する System Service の中核。

- **MIDI ルーティング**: RawInputQueue → RouteTable → 各キューへ分配
- **パラメータ更新**: ParamMapping に基づく CC → SharedParamState 変換
- **SysEx 処理**: SysExAssembler → コマンド解釈
- **ダブルバッファ管理**: `set_app_config()` 時に非活性バッファに書き込み、ブロック境界で切替
- **出力イベント処理**: output_events → hw_timestamp 変換 → USB/UART 送信

同じ System Task 上で Shell 等の他の System Service も動作する。

### タスク構成

| タスク | 優先度 | 責務 |
|--------|--------|------|
| Audio Task | 0 (Realtime) | DMA → process() → DMA |
| System Task | 1 (Server) | MIDI ルーティング、SysEx、シェル |
| Control Task | 2 (User) | アプリ main()、syscall 処理 |
| Idle Task | 3 (Idle) | WFI スリープ |

FPU コンテキスト退避ポリシーの詳細は [11-scheduler.md](11-scheduler.md) を参照。

### タスク間シーケンス（DMA 64 × 4 蓄積パターン）

```
時間軸 →

I2S DMA (64 sample 単位で Half/Complete 割り込み)
  ──┬──────┬──────┬──────┬──────┬──
    │ HC   │ HC   │ HC   │ HC   │
    ▼      ▼      ▼      ▼      ▼

Audio Task (prio 0)
  ──[acc]──[acc]──[acc]──[====process(256)====]──
    copy64  copy64 copy64  build ctx → process()
                           → copy out → DMA

System Task (prio 1)                EventRouter
  ─────────────────────────[=====poll=====]──────
                            MIDI route, SysEx,
                            output_events → USB/UART

Control Task (prio 2)         アプリ main()
  ──[wait_event]───────────────[=handle=]────────
                                UI/パラメータ処理

凡例:
  HC    = DMA Half/Complete 割り込み
  [acc] = 64 sample をリングバッファに蓄積（Audio Task が即座に処理）
  [====process(256)====] = 4回蓄積後に process() 呼び出し
  [=====poll=====]       = Audio Task 完了後に System Task が動作
  [=handle=]             = イベント到着で Control Task が起床
```

## WASM バックエンド

### AudioContext 構築フロー

```
AudioWorklet.process(inputs, outputs, parameters)
    │
    ▼
WASM Runtime (AudioWorklet スレッド)
    ├── inputs → WASM リニアメモリにコピー
    ├── Web MIDI API 受信キュー → input_events に変換
    ├── AudioContext 構築 (WASM メモリ内)
    ├── process(ctx) 呼び出し
    ├── output_events → Web MIDI API で送信
    └── WASM リニアメモリ → outputs にコピー
```

### ホスト JS の役割

```javascript
// AudioWorklet プロセッサ
class UmiProcessor extends AudioWorkletProcessor {
    process(inputs, outputs, parameters) {
        // 1. inputs → WASM shared memory
        // 2. MIDI events → event queue
        // 3. wasm.exports.umi_process()
        // 4. WASM shared memory → outputs
        return true;
    }
}

// メインスレッド
const importObject = {
    umi: {
        exit: (code) => { /* アプリ終了 */ },
        wait_event: (mask, timeout) => { /* イベント待機 */ },
        get_time: () => { /* performance.now() ベース */ },
        set_app_config: (ptr) => { /* WASM メモリから読み取り適用 */ },
        log: (ptr, len) => { /* console.log */ },
    }
};

const { instance } = await WebAssembly.instantiate(wasmBytes, importObject);
```

### 制約

- AudioWorklet スレッドではブロッキング不可（process() と同じ制約）
- Controller (main) はメインスレッドで実行、`wait_event()` は Promise ベースで実装
- SharedMemory は WASM リニアメモリ内に配置（JS 側から SharedArrayBuffer でアクセス可能）

## Plugin バックエンド

### VST3

```cpp
class UmiVst3Processor : public Steinberg::Vst::AudioEffect {
    tresult process(Steinberg::Vst::ProcessData& data) override {
        // 1. data.inputs → AudioContext.inputs
        // 2. data.inputEvents → input_events に変換
        // 3. data.inputParameterChanges → SharedParamState に適用
        // 4. AudioContext 構築
        // 5. processor.process(ctx)
        // 6. AudioContext.outputs → data.outputs
        // 7. output_events → data.outputEvents に変換
        return kResultOk;
    }
};
```

### Audio Unit (AU)

```cpp
class UmiAudioUnit : public AUAudioUnit {
    AUInternalRenderBlock internalRenderBlock() {
        return ^(/* ... */) {
            // 1. inputBus → AudioContext.inputs
            // 2. MIDIEventList → input_events
            // 3. AUParameter → SharedParamState
            // 4. process(ctx)
            // 5. outputs → outputBus
        };
    }
};
```

### CLAP

```cpp
static bool umi_clap_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    // 1. process->audio_inputs → AudioContext.inputs
    // 2. process->in_events → input_events に変換
    // 3. パラメータイベント → SharedParamState
    // 4. processor.process(ctx)
    // 5. AudioContext.outputs → process->audio_outputs
    // 6. output_events → process->out_events
    return true;
}
```

### Plugin 共通パターン

全 Plugin フォーマットで共通する変換パターン:

| ホスト概念 | UMI 概念 | 変換方向 |
|-----------|----------|---------|
| ホスト audio buffer | AudioContext.inputs/outputs | 双方向 |
| ホスト MIDI events | input_events | ホスト → UMI |
| ホスト parameter changes | SharedParamState | ホスト → UMI |
| UMI output_events | ホスト MIDI out | UMI → ホスト |
| ホスト sample rate | AudioContext.sample_rate | ホスト → UMI |
| ホスト buffer size | AudioContext.buffer_size | ホスト → UMI |

### Syscall の Plugin 実装

Plugin では特権遷移が不要なため、syscall は直接関数呼び出しで実装される。

```cpp
namespace umi::backend::plugin {

class PluginRuntime {
public:
    void exit(int32_t code) { running = false; }

    uint32_t wait_event(uint32_t mask, uint32_t timeout_us) {
        // GUI スレッドのイベントループで実装
        // std::condition_variable で待機
    }

    uint64_t get_time() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    int32_t set_app_config(const AppConfig* config) {
        // 直接 RouteTable / ParamMapping を更新
    }
};

} // namespace umi::backend::plugin
```

## バックエンド選択

コンパイル時に `#ifdef` またはビルドシステム（xmake の `--board` / ターゲット名）でバックエンドが決定される。

```cpp
// umi_app.hh 内部（概念）
namespace umi {

#if defined(UMI_TARGET_CM)
    // Cortex-M: SVC 経由
    inline void exit(int code) { syscall::call(nr::exit, code); }
#elif defined(UMI_TARGET_WASM)
    // WASM: import 関数
    inline void exit(int code) { umi_exit(code); }
#elif defined(UMI_TARGET_PLUGIN)
    // Plugin: 直接呼び出し
    inline void exit(int code) { runtime().exit(code); }
#endif

} // namespace umi
```

アプリケーションコードは `umi::exit()`, `umi::wait_event()` 等の統一 API のみを使用する。
バックエンドの差異はアプリから見えない。
