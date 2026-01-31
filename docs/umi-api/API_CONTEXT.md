# Context API 設計（現行仕様）

本書はUMI-OSの`AudioContext`/`Processor`/`Controller`の責務とデータフローを定義する。

## 1. 設計原則

1. **Processorは信号処理専任**
   - `process(umi::AudioContext&)` はオーディオと入力イベントを処理する。
   - LED/ディスプレイ等のI/O出力は行わない。

2. **ControllerはI/O更新専任**
   - Processorの状態を読み、共有メモリへ反映する。
   - 更新周期はUI相当（~60Hz）で十分。

3. **共有メモリはフレームバッファ**
   - ハードウェアI/Oの状態とイベントの受け渡しに使用する。
   - Processor内部状態の所有はProcessor側に留める。

## 2. 役割分担

| 役割 | 責務 | 実行コンテキスト | 禁止事項 |
|---|---|---|---|
| Processor | オーディオ処理、入力イベント処理 | Realtimeタスク | I/O出力、ブロッキング、ヒープ割当 |
| Controller | I/O出力更新、UI更新 | Controlタスク | 高頻度更新（オーディオレート） |

## 3. AudioContextの内容

`AudioContext`は**オーディオバッファ + 入力イベント + タイミング**を統合する構造体である。

主な内容:
- `outputs` / `inputs`（オーディオバッファ）
- `input_events`（MIDI/パラメータ/ボタン等）
- `output_events`（MIDI out等）
- `sample_rate`, `buffer_size`, `dt`, `sample_position`

### 3.1 バッファの寿命と保持禁止

- `inputs` / `outputs` は**当該`process()`呼び出し中のみ有効**。
- 参照を保持しない（次の呼び出しで無効になる前提）。

### 3.2 入力イベントの扱い

- `input_events` は**同一バッファ内で順序保持**される。
- 同一時刻の優先順位は実装依存のため、イベント内の情報で処理順を決める。

## 4. データフロー

```
[Hardware/USB/MIDI]
        │
        ▼
[Kernel] ──(Event生成)──▶ [AudioContext.input_events]
        │
        ▼
[Processor.process()]  ──(内部状態更新)──▶ [Controller]
        │
        ▼
[Controller] ──(SharedMemory更新)──▶ [LED/Display等のI/O]
```

## 5. 実装指針

### 5.1 Processor実装

```cpp
struct MyProcessor {
    void process(umi::AudioContext& ctx) {
        for (const auto& ev : ctx.input_events) {
            // MIDI/Param/Buttonイベント処理
        }

        auto* out_l = ctx.output(0);
        auto* out_r = ctx.output(1);
        if (!out_l) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            // オーディオ処理
            out_l[i] = /* ... */;
            if (out_r) out_r[i] = out_l[i];
        }
    }
};
```

### 5.2 Controller実装

Controllerは`main()`直書きでもよいが、**Processor同様のクラス化**も許容する。
初期化はコンストラクタで行い、`run()`/`process()`でメインループを表現する。

```cpp
class MyController {
  public:
    MyController(MyProcessor& proc)
        : proc_(proc), shared_(*static_cast<umi::kernel::SharedMemory*>(umi::syscall::get_shared())) {
        shared_.led_state.store(0, std::memory_order_relaxed);
    }

    void run() {
        while (true) {
            umi::syscall::wait_event(umi::syscall::event::Timer, 16000);
            shared_.led_state.store(/* proc状態から算出 */, std::memory_order_relaxed);
        }
    }

  private:
    MyProcessor& proc_;
    umi::kernel::SharedMemory& shared_;
};

int main() {
    MyProcessor proc;
    umi::register_processor(proc);

    MyController controller(proc);
    controller.run();
}
```

## 6. スレッド安全性

- Processor → Controllerの状態公開は`std::atomic`推奨。
- `process()`の末尾で状態を公開し、Controllerは読み取るだけにする。
- 共有データの更新は**バッファ境界**で行う（サンプル単位更新は避ける）。

例:

```cpp
std::atomic<float> lfo_phase_out{0.0f};

void process(umi::AudioContext& ctx) {
    // ...
    lfo_phase_out.store(lfo_phase, std::memory_order_relaxed);
}
```

## 7. 参照実装

- `Processor/Controller`の実装例: [examples/synth_app/src/main.cc](../../../examples/synth_app/src/main.cc)
- `AudioContext`定義: [lib/umios/core/audio_context.hh](../../../lib/umios/core/audio_context.hh)
- 共有メモリ: [lib/umios/kernel/loader.hh](../../../lib/umios/kernel/loader.hh)

## 8. 非推奨・禁止事項

- `process()`内でLED/ディスプレイ操作（SharedMemory経由も含む）
- `process()`内で`syscall`によるブロッキング
- `process()`内でヒープ割当（リアルタイム性のため）
- `process()`内でロック取得やI/O待ち
