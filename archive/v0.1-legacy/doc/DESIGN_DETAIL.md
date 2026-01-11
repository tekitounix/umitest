# UMI-OS 詳細設計

カーネル、サブシステム、APIの詳細仕様。

概要は [README.md](README.md) を参照。

---

## 目次

1. [構成モデル](#1-構成モデル)
2. [カーネル](#2-カーネル)
3. [オーディオサブシステム](#3-オーディオサブシステム)
4. [MIDIサブシステム](#4-midiサブシステム)
5. [IPC機構](#5-ipc機構)
6. [コルーチン](#6-コルーチン)
7. [モニタリング](#7-モニタリング)
8. [エラーハンドリング](#8-エラーハンドリング)
9. [デバッグシェル](#9-デバッグシェル)
10. [ハードウェア抽象化](#10-ハードウェア抽象化)
11. [ビルドシステム](#11-ビルドシステム)

---

## 1. 構成モデル

### 1.1 マイクロカーネル構成

製品カスタムファームウェア向け。ユーザーコードはMPU保護下で動作。

```
┌─────────────────────────────────────────────────────┐
│  User Space (MPU保護)                                │
│  ┌──────────────┐  ┌──────────────┐                 │
│  │ Audio Task   │  │  User Task   │                 │
│  │ (Realtime)   │  │  (Normal)    │                 │
│  └──────┬───────┘  └──────┬───────┘                 │
│         │                 │                          │
│    [共有メモリ]       [システムコール]               │
│    (低レイテンシ)     (安全性重視)                   │
├─────────┼─────────────────┼──────────────────────────┤
│  Kernel Space                                        │
│         ▼                 ▼                          │
│  ┌──────────────────────────────────┐               │
│  │         Server Tasks              │               │
│  │  MIDI | USB | I2C | GPIO | DMA   │               │
│  └──────────────────────────────────┘               │
└─────────────────────────────────────────────────────┘
```

**データ交換:**
- 速度重視: 共有メモリ直接アクセス（オーディオバッファ）
- 安全重視: システムコール（パラメータ変更等）

### 1.2 モノリシック構成

開発ボード向け。全タスクが同一アドレス空間。

```
┌─────────────────────────────────────────────────────┐
│  Single Address Space (MPU無効)                      │
│                                                      │
│  全タスクがハードウェアに直接アクセス可能             │
└─────────────────────────────────────────────────────┘
```

### 1.3 構成切り替え

インクルードパスで切り替え。マクロ不使用。

```
config/
├── microkernel/
│   └── umi_config.hh    # MPU有効、syscall経由
└── monolithic/
    └── umi_config.hh    # MPU無効、直接呼び出し
```

```lua
-- xmake.lua
target("firmware_product")
    add_includedirs("config/microkernel")

target("firmware_devboard")
    add_includedirs("config/monolithic")
```

---

## 2. カーネル

`core/umi_kernel.hh`

### 2.1 優先度

5段階固定優先度。Normal以下はラウンドロビン。

| 優先度 | 用途 | ユーザー使用 |
|--------|------|-------------|
| **Realtime** | オーディオタスク | 不可（カーネル予約） |
| **High** | MIDI等クリティカル | 不可（カーネル予約） |
| **Normal** | 通常タスク | 可 |
| **Low** | バックグラウンド | 可 |
| **Idle** | スリープ | 不可 |

```cpp
enum class Priority : std::uint8_t {
    Realtime = 0,
    High = 1,
    Normal = 2,
    Low = 3,
    Idle = 4,
};
```

### 2.2 タスクの隠蔽

ユーザーは `AudioProcessor` を実装するだけ。

```cpp
// カーネル内部で自動生成
// - Audio Task (Realtime): DMA notify → process() → 負荷計測
// - MIDI Server (High): MIDI受信 → on_midi()
// - Monitor (Low): 負荷表示、ドロップ検知
```

### 2.3 負荷計測

オーディオタスク内で計測、低優先度タスクで集計。

```cpp
void audio_task_loop(AudioProcessor& app) {
    while (true) {
        kernel.wait(Event::AudioReady);
        
        auto start = DWT::cycles();
        app.process(out, in, frames, channels);
        auto elapsed = DWT::cycles() - start;
        
        load_monitor.record(elapsed);
        if (elapsed > deadline_cycles) {
            drop_counter++;
        }
    }
}
```

### 2.4 タスク管理（モノリシック構成）

開発用に直接タスク作成可能。

```cpp
struct TaskConfig {
    void (*entry)(void*) {nullptr};
    void* arg {nullptr};
    Priority prio {Priority::Normal};
    const char* name {"<unnamed>"};
};

TaskId id = kernel.create_task(config);
kernel.notify(id, Event::AudioReady);
```

### 2.5 イベントシステム

```cpp
enum class Event : std::uint32_t {
    None = 0,
    AudioReady = 1 << 0,
    MidiReady = 1 << 1,
    UsbReady = 1 << 2,
    // 最大32イベント
};

kernel.wait(Event::AudioReady, 1000_usec);
```

### 2.6 タイマーキュー（Tickless）

```cpp
kernel.call_later(deadline_usec, [] { handle_envelope(); });
kernel.call_every(interval_usec, [] { update_lfo(); });
```

### 2.7 ウォッチドッグとパニック

```cpp
kernel.register_watchdog(task_id, 10000_usec);
kernel.kick_watchdog(task_id);
kernel.panic("Buffer underrun");
```

---

## 3. オーディオサブシステム

`core/umi_audio.hh`

### 3.1 DMAダブルバッファ

```cpp
template<typename HW, std::size_t BufferSize = 256>
class AudioEngine {
    alignas(4) std::array<Sample, BufferSize> buffer_a_;
    alignas(4) std::array<Sample, BufferSize> buffer_b_;
};
```

### 3.2 レイテンシ

| バッファサイズ | 48kHz時レイテンシ |
|----------------|------------------|
| 32 samples | 0.67ms |
| 64 samples | 1.33ms |
| 128 samples | 2.67ms |
| 256 samples | 5.33ms |

---

## 4. MIDIサブシステム

`core/umi_midi.hh`

### 4.1 イベント構造

```cpp
namespace midi {
    struct Event {
        Type type;
        std::uint8_t channel;
        std::uint8_t data1;
        std::uint8_t data2;
    };
}
```

### 4.2 対応メッセージ

- **チャンネル**: NoteOn, NoteOff, CC, ProgramChange, PitchBend, Aftertouch
- **システム**: Clock, Start, Stop, Continue
- **SysEx**: 可変長対応

---

## 5. IPC機構

### 5.1 SpscQueue（ロックフリー）

```cpp
SpscQueue<midi::Event, 64> midi_queue;

// Producer (ISR)
midi_queue.try_push(event);

// Consumer (Task)
if (auto ev = midi_queue.try_pop()) {
    process(*ev);
}
```

### 5.2 Notification

```cpp
kernel.notify(task_id, Event::MidiReady);
auto events = kernel.wait(Event::MidiReady, timeout);
```

### 5.3 データフローパターン

```
[MIDI ISR] ──SpscQueue──> [Audio Task] ──DMA──> [DAC]
                               ↑
                          Notification
```

---

## 6. コルーチン

`core/umi_coro.hh`

```cpp
umi::coro::Task<void> blink_led() {
    while (true) {
        led.toggle();
        co_await ctx.sleep(500ms);
    }
}

umi::coro::Scheduler<8> scheduler;
scheduler.spawn(blink_led());
```

---

## 7. モニタリング

`core/umi_monitor.hh`

```cpp
// スタック
std::size_t used = StackMonitor::get_used(task_id);

// ヒープ
auto [used, total] = HeapMonitor::get_usage();

// CPU
float cpu = TaskProfiler::get_cpu_percent(task_id);

// 負荷
float load = LoadMonitor::get_percent();
```

---

## 8. エラーハンドリング

`core/umi_expected.hh`

```cpp
enum class Error {
    OutOfMemory, OutOfTasks, Timeout, WouldBlock,
    BufferOverrun, BufferUnderrun,
    MidiParseError, MidiBufferFull,
};

Result<TaskId> result = kernel.try_create_task(config);
if (!result) {
    Error err = result.error();
}
```

---

## 9. デバッグシェル

`core/umi_shell.hh`

| コマンド | 説明 |
|----------|------|
| `ps` | タスク一覧 |
| `mem` | メモリ使用状況 |
| `load` | CPU負荷 |
| `reboot` | 再起動 |

出力先: UART または MIDI SysEx

---

## 10. ハードウェア抽象化

### 10.1 HWトレイト

```cpp
struct HW {
    static void enable_interrupts();
    static void disable_interrupts();
    static usec monotonic_time_usecs();
    static void mute_audio_dma();
    static void reboot();
};
```

### 10.2 ポート構成

```
port/
├── arm/cortex-m/
│   └── cortex_m.hh
├── board/
│   ├── stm32f4/
│   │   └── hw.hh
│   └── stub/
│       └── hw.hh
└── vendor/stm32/stm32f4/
    └── peripheral.hh
```

インクルードパスで切り替え:

```lua
target("firmware_stm32f4")
    add_includedirs("port/board/stm32f4")

target("test_kernel")
    add_includedirs("port/board/stub")
```

---

## 11. ビルドシステム

### 11.1 コマンド

```bash
xmake                    # 全ビルド
xmake test               # ホストテスト
xmake build firmware     # ファームウェア
xmake renode-test        # Renodeテスト
xmake robot              # Robot Framework
```

### 11.2 実装状況

| ファイル | 状態 |
|----------|------|
| `umi_kernel.hh` | ✅ |
| `umi_audio.hh` | ✅ |
| `umi_midi.hh` | ✅ |
| `umi_coro.hh` | ✅ |
| `umi_monitor.hh` | ✅ |
| `umi_shell.hh` | ✅ |
| `umi_expected.hh` | ✅ |

---

## 付録: 追加しない機能

| 機能 | 理由 |
|------|------|
| Mutex/Semaphore | SpscQueue + Notification で十分 |
| MPSCキュー | 電子楽器はSPSCで完結 |
| 実行時動的メモリ | オーディオ処理中は禁止 |
| ファイルシステム | フラッシュ直接アクセス |
| ネットワーク | 過剰 |
