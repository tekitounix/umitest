# UMI-Controller (UMIC) 仕様書

**バージョン:** 3.0.0-draft
**ステータス:** ドラフト

## 概要

UMI-Controller (UMIC) は、アプリケーションのControl Task（`main()` で実行されるUIロジック）の仕様です。

- **統一モデル** - 組込み/Web共通の `main()` エントリポイント
- **イベント駆動** - `wait_event()` で非同期イベント待機
- **Processor連携** - Processor Task との共有メモリ通信

## 統一アプリケーションモデル

```
+-------------------------------------------------------------+
|                    Application                              |
|  +----------------------------------------------------------+
|  |  main() - Control Task (UMIC)                           ||
|  |    +-- register_processor(synth)                        ||
|  |    +-- while (wait_event()) { handle... }               ||
|  |    +-- shutdown                                         ||
|  +----------------------------------------------------------+
|                           |                                 |
|                    共有メモリ                                |
|                           |                                 |
|  +----------------------------------------------------------+
|  |  Processor::process() - Processor Task (UMIP)           ||
|  |    +-- オーディオ生成                                    ||
|  |    +-- MIDI/イベント処理                                 ||
|  +----------------------------------------------------------+
+-------------------------------------------------------------+
```

## いつ必要か

**単純なエフェクト/シンセ** では UMIC は不要（main が最小限でOK）。
**UI状態を持つアプリケーション** では main() 内で Control Task として実装。

| 機能 | Control Task で実装 | 備考 |
|------|---------------------|------|
| パラメータ選択/ページ切替 | ✓ | UI状態管理 |
| MIDI Learn | ✓ | マッピング状態 |
| プリセット管理 | ✓ | 一覧/選択状態 |
| 専用ハードウェアUI | ✓ | モード遷移 |
| DSP処理のみ | ✗ | 最小 main() でOK |

## MVCにおける位置づけ

```
+-------------------------------------------------------------+
|  Controller (main)       |  Model (Processor)               |
|  +-- UI状態管理          |  +-- DSP処理                     |
|  +-- イベントハンドリング |  +-- パラメータ値                |
|  +-- Processor制御       |  +-- オーディオ生成              |
|         |                          ^                        |
|         +--------共有メモリ---------+                        |
+-------------------------------------------------------------+
```

## プラットフォーム抽象化

| 操作 | 組込み | Web (WASM) |
|------|--------|------------|
| `wait_event()` | syscall + ブロック | Asyncify |
| `send_event()` | syscall | JS import |
| `log()` | syscall → UART | console.log |
| スレッド | 別タスク | AudioWorklet |

## 最小実装

### DSP専用（Control不要）

```cpp
// main.cc
#include <umi/app.hh>
#include "volume.hh"

int main() {
    static Volume vol;
    umi::register_processor(vol);
    
    // イベントを待つだけ（処理は不要）
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

### UI状態を持つController

```cpp
// main.cc
#include <umi/app.hh>
#include "synth.hh"

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    // UI状態
    int page = 0;
    int selected_param = 0;
    bool midi_learn_active = false;
    
    while (true) {
        auto ev = umi::wait_event();
        
        switch (ev.type) {
        case umi::EventType::Shutdown:
            return 0;
            
        case umi::EventType::EncoderRotate:
            synth.adjust_param(selected_param, ev.encoder.delta);
            break;
            
        case umi::EventType::ButtonPress:
            if (ev.button.id == BTN_LEARN) {
                midi_learn_active = true;
            }
            break;
            
        case umi::EventType::MidiCC:
            if (midi_learn_active) {
                // CCマッピング
                midi_learn_active = false;
            }
            break;
        }
    }
}
```

## コルーチンによる実装（推奨）

C++20コルーチンを使用した、より読みやすい実装:

```cpp
// main.cc
#include <umi/app.hh>
#include <umi/coro.hh>
#include "synth.hh"

umi::Task<void> controller_task(Synth& synth) {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        synth.handle_event(ev);
    }
}

umi::Task<void> display_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(33ms);  // 30fps
        update_display(synth);
    }
}

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<4> sched;
    sched.spawn(controller_task(synth));
    sched.spawn(display_task(synth));
    sched.run();
    
    return 0;
}
```

## Processorとの通信

### パラメータ変更

```cpp
// Control Task側
void adjust_volume(float delta) {
    float current = umi::get_param(PARAM_VOLUME);
    umi::set_param(PARAM_VOLUME, current + delta);
}

// Processor側（process()内）
void process(ProcessContext& ctx) {
    float vol = params[PARAM_VOLUME];  // atomic load
    // ...
}
```

### イベント送信

```cpp
// Control -> Processor
umi::send_to_processor(Event::param_change(PARAM_CUTOFF, value));

// Processor -> Control
ctx.send_to_control(Event::meter(channel, level));
```

## イベント処理

### イベントタイプ

```cpp
namespace umi {
enum class EventType {
    Shutdown,
    MidiNoteOn, MidiNoteOff, MidiCC, MidiPitchBend,
    ParamChange,
    EncoderRotate, ButtonPress, ButtonRelease,
    DisplayUpdate, Meter,
};
}
```

### 処理フロー

```
ハードウェア入力
       |
       v
カーネル/ホスト
       |
       +---> Processor (MIDI, Audio)
       |
       +---> Control (UI, パラメータ変更)
                    |
                    v
              共有メモリ経由で Processor に反映
```

## ライセンス

CC0 1.0 Universal (パブリックドメイン)
