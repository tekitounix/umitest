# umi アーキテクチャ

**バージョン:** 0.10.0
**更新日:** 2025-01-24

## 概要

umiは「1つのProcessorコードからあらゆるターゲットに向けてコンパイルできる」フレームワークです。

### 設計思想

1. **オーディオ処理を最優先** - すべての設計判断においてリアルタイムオーディオ処理の確実性を優先
2. **正規化されたデータ** - アプリケーションはハードウェア非依存の正規化データのみを扱う
3. **統一main()モデル** - 組込み/Webで同じ `main()` をエントリポイントとする
4. **組み込み最適化** - 制限されたリソースに最適化することで全環境での動作を保証
5. **非特権アプリ実行** - アプリケーションは非特権モードで実行し、カーネル/ハードウェアを保護

### 実装状況凡例

- ✓ 実装済み（v0.10.0時点）
- ◇ 将来仕様（設計済み、未実装）

## 統一アプリケーションモデル

アプリケーションは **Processor Task** と **Control Task** の2つで構成されます。

```
+-------------------------------------------------------------+
|                    Application (.umia / .umim)            |
|                                                             |
|  +------------------------+  +--------------------------+   |
|  |  Processor Task        |  |  Control Task (main)     |   |
|  |  - process() DSP処理   |  |  - register_processor()  |   |
|  |  - リアルタイム        |  |  - wait_event() ループ   |   |
|  |  - syscall禁止         |  |  - UI状態管理            |   |
|  +------------------------+  +--------------------------+   |
|            ^                          |                     |
|            +-----共有メモリ-----------+                     |
|                 (lock-free)                                 |
+-------------------------------------------------------------+
```

### プラットフォーム対応

| プラットフォーム | バイナリ | Control Task | Processor Task | 状態 |
|------------------|----------|--------------|----------------|------|
| **組込み** | `.umia` | syscall でカーネルAPI | カーネルから直接呼出 | ✓ 実装済 |
| **Web** | `.umim` | WASM + Asyncify | AudioWorklet | ◇ 将来仕様 |
| **デスクトップ** | VST3/AU等 | ホストスレッド | オーディオスレッド | ◇ 将来仕様 |

## システム構成

```
+-------------------------------------------------------------+
|                     Application                             |
|              (main() + Processor, プラットフォーム非依存)     |
+-------------------------------------------------------------+
|                    UMI SDK API                              |
|     (register_processor, wait_event, send_event, etc.)      |
+------+----------------+----------------+--------------------+
|      |                |                |                    |
| 組込み Kernel        | WASM Runtime   | DAW Host           |
| (umios + MPU)        | (Asyncify)     | (VST3/AU/CLAP)     |
| ✓ 実装済             | ◇ 将来仕様     | ◇ 将来仕様         |
+------+----------------+----------------+--------------------+
| HAL / PAL            | Browser API    | OS API             |
+------+----------------+----------------+--------------------+
```

### 各層の責務

| 層 | 責務 | 共通化 |
|----|------|--------|
| **Application** | main(), Processor | ✓ 100% |
| **UMI SDK** | プラットフォーム抽象化 | ✓ API統一 |
| **Runtime** | syscall / Asyncify / ホストAPI | ターゲット固有 |

## Processorモデル

### ProcessorLike Concept

継承なしのC++20 concept:

```cpp
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};
```

### ドメイン分離

```
+-------------------------------------------------------------+
|                    Audio Domain (Processor Task)            |
|         サンプル精度が要求される / process()内で処理          |
|                                                             |
|  - Audio Stream                                             |
|  - MIDI Events (サンプル位置補正済み)                        |
|  - Sample-accurate Parameter Changes                        |
+-------------------------------------------------------------+
|                   Control Domain (Control Task)             |
|         バッファ単位以下の精度で十分 / main() 内で処理        |
|                                                             |
|  - UI Events (エンコーダ、ボタン)                           |
|  - File I/O 完了通知                                        |
|  - ネットワークメッセージ                                    |
+-------------------------------------------------------------+
```

## 実装例

### 最小アプリケーション

```cpp
// main.cc - 組込み/Web共通
#include <umi/app.hh>

struct Volume {
    float gain = 1.0f;

    void process(umi::AudioContext& ctx) {
        auto* out = ctx.output(0);
        auto* in = ctx.input(0);
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * gain;
        }
    }
};

int main() {
    static Volume vol;
    umi::register_processor(vol);

    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

### コルーチン対応アプリケーション ✓

**状態:** 実装済み (`lib/umios/kernel/coro.hh`)

```cpp
// main.cc
#include <umi/app.hh>
#include <umi/coro.hh>
#include "synth.hh"

umi::Task<void> ui_task(Synth& synth) {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        synth.handle_event(ev);
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
    sched.spawn(ui_task(synth));
    sched.spawn(display_task(synth));
    sched.run();

    return 0;
}
```

## UMIM (UMI-Module)

Processorを最小単位としたヘッドレスなオーディオモジュール。
モジュラーシンセ/エフェクターのように、モジュール同士をパッチングして接続可能。

### 特徴

- **ヘッドレス** - UIを持たない純粋なオーディオ処理単位
- **統合I/O** - Audio / MIDI / CV・Gate / パラメータを統一的に扱う
- **パッチング** - モジュール間の入出力を自由に接続 ◇
- **クロスプラットフォーム** - WASM / 組み込み / デスクトップで動作

### ポートタイプ

| タイプ | 説明 | 例 |
|--------|------|-----|
| `Continuous` | サンプルレート同期の連続信号 | Audio, CV |
| `Event` | 不定期のイベント | MIDI, Gate, ParamChange |

```cpp
// ポート定義例
static constexpr PortDescriptor ports_[] = {
    {0, "Audio Out", PortKind::Continuous, PortDirection::Out, .channels = 2},
    {1, "CV In",     PortKind::Continuous, PortDirection::In,  .channels = 1},
    {2, "Gate In",   PortKind::Event,      PortDirection::In,  .type_hint = TypeHint::ParamChange},
    {3, "MIDI In",   PortKind::Event,      PortDirection::In,  .type_hint = TypeHint::MidiBytes},
};
```

### パッチング ◇

**状態:** 将来仕様

ノードベースエディタでモジュール間を接続:

```
┌─────────┐     ┌─────────┐     ┌─────────┐
│   VCO   │────▶│   VCF   │────▶│   VCA   │────▶ Out
└─────────┘     └─────────┘     └─────────┘
     ▲               ▲               ▲
     │               │               │
┌─────────┐     ┌─────────┐     ┌─────────┐
│   LFO   │     │  ADSR   │     │  ADSR   │
└─────────┘     └─────────┘     └─────────┘
```

## アダプタ層

### 対応アダプタ

| アダプタ | ターゲット | 状態 |
|---------|-----------|------|
| `embedded_adapter.hh` | STM32, ESP32等 | ✓ 実装済 |
| `umim_adapter.hh` | WASM (ブラウザ、Node.js) | ◇ 将来仕様 |
| VST3/AU/CLAP | DAW | ◇ 将来仕様 |

## umios (組み込みKernel) ✓

組み込み環境向けのリアルタイムカーネル。アプリケーションはsyscall経由でカーネルAPIを呼び出します。

**v0.10.0で実装完了:**
- カーネル/アプリ分離
- 非特権モードでのアプリ実行
- RTOS タスク管理（Control Task, Audio Task）
- Ed25519署名検証
- MPU によるメモリ保護

### Syscall ABI ✓

```cpp
// syscall番号（実装済み）
namespace umi::syscall::nr {
    // Process control
    constexpr uint32_t Exit          = 0;   // アプリ終了
    constexpr uint32_t RegisterProc  = 1;   // Processor登録

    // Event handling
    constexpr uint32_t WaitEvent     = 2;   // イベント待機
    constexpr uint32_t SendEvent     = 3;   // イベント送信
    constexpr uint32_t PeekEvent     = 4;   // イベント確認（非ブロック）
    constexpr uint32_t Yield         = 5;   // 制御を返す

    // Time
    constexpr uint32_t GetTime       = 10;  // 時刻取得
    constexpr uint32_t Sleep         = 11;  // スリープ

    // Debug/Log
    constexpr uint32_t Log           = 20;  // ログ出力
    constexpr uint32_t Panic         = 21;  // パニック

    // Parameters
    constexpr uint32_t GetParam      = 30;  // パラメータ取得
    constexpr uint32_t SetParam      = 31;  // パラメータ設定

    // Shared memory
    constexpr uint32_t GetShared     = 40;  // 共有メモリ取得
}

// ARM Cortex-M: svc #0 命令で呼び出し
// r0 = syscall番号, r1-r4 = 引数, r0 = 戻り値
```

### メモリ保護（MPU） ✓

**v0.10.0 実装済みリージョン:**

| リージョン | アドレス範囲 | アクセス | 内容 |
|------------|-------------|----------|------|
| 0 | 0x20000000 | RW | カーネルSRAM |
| 1 | 0x08060000 | RO+X | アプリコード（Flash） |
| 2 | 0x2000C000 | RW | アプリRAM（32KB） |
| 3 | 0x20018000 | RW | 共有メモリ（16KB） |
| 5 | 0x40000000 | Device | ペリフェラル |
| 6 | 0x10000000 | RW | CCM（タスクスタック） |
| 7 | 0x08000000 | RO+X | カーネルFlash |

### バックエンド切替

| バックエンド | 対象 | 状態 |
|-------------|------|------|
| **umios core** | Cortex-M4 | ✓ 実装済 |
| **POSIX** | Linux, macOS | ✓ ホストテスト用 |
| **FreeRTOS** | ESP32, STM32 | ◇ 将来仕様 |
| **Zephyr** | 多様なMCU | ◇ 将来仕様 |

### 保護レベル

| レベル | ハードウェア | 保護方式 | 対象 | 状態 |
|--------|--------------|----------|------|------|
| L3 | MMUあり | 完全メモリ分離 | Cortex-A | ◇ 将来仕様 |
| L2 | MPUあり | 軽量メモリ分離 | Cortex-M3/M4/M7 | ✓ 実装済 |
| L1 | 保護機構なし | 論理分離のみ | ESP32, RP2040, M0 | ◇ 将来仕様 |

## UI/View システム ◇

**状態:** 将来仕様（設計済み）

### 構成

```
┌─────────────────────────────────────────────────────────────┐
│                   Model (Processor State)                   │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────┴─────────────────────────────┐
│                    UI Controls (lib/ui/)                   │
│   Knob, Slider, Button, Selector - 値と状態のみ            │
│   ハードウェアUI（エンコーダ、物理ボタン）でも使用可能       │
└─────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┴───────────────────┐
          ▼                                       ▼
┌─────────────────────────┐         ┌─────────────────────────┐
│   GUI (lib/gui/)        │         │   Hardware UI           │
│   ISkin + IBackend      │         │   エンコーダ / LED      │
│                         │         │   7セグ / ボタン        │
│ FrameBufferBackend (LCD)│         │                         │
│ Canvas2DBackend (Web)   │         │                         │
└─────────────────────────┘         └─────────────────────────┘
```

### UIMap / MidiMap

| 概念 | 責務 | 変更頻度 |
|------|------|----------|
| **UIMap** | Control → Parameter | Skin依存 |
| **MidiMap** | CC → Parameter | ランタイム変更可 (MIDI Learn) |

```cpp
// UIMap: UI Control → Processor Parameter
inline constexpr UIMap<3> kSynthUIMap = {{
    {{"volume",    0, Curve::Linear, 0.0f, 1.0f}},
    {{"cutoff",    1, Curve::Log,    20.0f, 20000.0f, "Hz"}},
    {{"resonance", 2, Curve::Linear, 0.0f, 1.0f}},
}};

// MidiMap: MIDI CC → Processor Parameter
inline constexpr MidiMap<2> kSynthMidiMap = {{
    {{0, 1,  1}},   // CC1 → Cutoff
    {{0, 74, 2}},   // CC74 → Resonance
}};
```

## C++言語方針

### C++23機能

| 機能 | 用途 |
|------|------|
| `std::expected` | エラーハンドリング |
| `std::span` | 非所有バッファ参照 |
| `std::numbers` | 数学定数 (π等) |
| Concepts | 型制約 |
| `constexpr` 拡張 | コンパイル時計算 |

### コンテナ使用

| コンテナ | 使用 | 条件 |
|----------|------|------|
| `std::array` | ✓ 推奨 | サイズ固定 |
| `std::span` | ✓ 推奨 | 非所有参照 |
| `std::string_view` | ✓ 推奨 | 非所有文字列 |
| `std::optional` | ✓ 許可 | オーバーヘッド最小 |
| `std::expected` | ✓ 許可 | エラーハンドリング |
| `std::vector` | △ 条件付 | 初期化時のみ |
| `std::map/set` | ✗ 非推奨 | ヒープ使用 |

### 禁止事項 (Audio Thread)

- 動的メモリ確保 (`malloc`, `new`, `push_back`等)
- ブロッキングI/O
- ミューテックスロック
- 例外送出 (`throw`)

## スレッド安全性モデル

### スレッド構成

```
┌─────────────────────────────────────────────────────────────┐
│                    Audio Thread (リアルタイム)               │
│  process() のみ実行、割り込み禁止、優先度最高               │
└─────────────────────────────────────────────────────────────┘
                              ▲
                     TripleBuffer (lock-free)
                              │
┌─────────────────────────────────────────────────────────────┐
│                    Control Thread (非リアルタイム)           │
│  control(), set_param(), UI更新                             │
└─────────────────────────────────────────────────────────────┘
```

### 通信パターン

| 通信方向 | 方式 | データ型 |
|----------|------|----------|
| Control → Audio | TripleBuffer | パラメータ、状態 |
| Audio → Control | EventQueue (SPSC) | メーター、トリガー |
| MIDI → Audio | EventQueue (SPSC) | MIDIイベント |

### TripleBuffer

パラメータ変更をロックフリーでオーディオスレッドに伝達:

```cpp
// Control Thread
triple_buffer.write([](ParamState& state) {
    state.cutoff = new_value;
});

// Audio Thread
auto& params = triple_buffer.read();
filter.set_cutoff(params.cutoff);
```

### 安全性ルール

1. **process()** - ロック禁止、アロケート禁止、ブロック禁止
2. **control()** - TripleBuffer経由でのみAudioと通信
3. **EventQueue** - SPSC (Single Producer, Single Consumer) のみ保証

## メモリ・性能制約

### ターゲット別制約

| ターゲット | RAM | Flash | CPU | 備考 | 状態 |
|------------|-----|-------|-----|------|------|
| STM32F4 | 192KB | 1MB | 168MHz Cortex-M4F | 最小ターゲット | ✓ 実装済 |
| ESP32 | 520KB | 4MB | 240MHz Xtensa LX6 | WiFi込み | ◇ 将来仕様 |
| RP2040 | 264KB | 2MB | 133MHz Cortex-M0+ | デュアルコア | ◇ 将来仕様 |
| WASM | 無制限 | 無制限 | ホスト依存 | ブラウザ/Node | ◇ 将来仕様 |

### 処理時間バジェット

```
Sample Rate: 48kHz, Buffer Size: 64 samples
Buffer Period: 1.33ms

STM32F4 @ 168MHz:
  - 利用可能サイクル: 224,000 cycles/buffer
  - 目標CPU使用率: < 70%
  - 実効バジェット: 156,800 cycles/buffer

WASM (典型的なブラウザ):
  - 目標: < 50% of buffer period
  - レイテンシ: 128-512 samples (2.6-10.6ms)
```

### メモリ使用ガイドライン

| カテゴリ | 推奨最大 | 用途 |
|----------|----------|------|
| オーディオバッファ | 16KB | 入出力バッファ、遅延線 |
| パラメータ状態 | 1KB | TripleBuffer用 |
| イベントキュー | 2KB | MIDI、パラメータ変更 |
| DSP係数 | 4KB | フィルタ係数、ウェーブテーブル |

## 時間管理

### 2種類の時間

| 種類 | 型 | 精度 | 用途 |
|------|-----|------|------|
| サンプル時間 | `uint64_t` | サンプル精度 | DSP、イベント、同期 |
| システム時間 | `std::chrono` | μs | タイムアウト、ログ |

```cpp
void process(AudioContext& ctx) {
    // サンプル時間ベースで処理
    uint64_t buf_start = ctx.sample_position;

    // NG: process()内でchrono使用
    // auto now = std::chrono::steady_clock::now();
}
```

## ディレクトリ構成

```
lib/
├── umi/               # アプリケーションSDK
├── umios/             # カーネル・アプリランタイム
│   ├── kernel/        # カーネル実装
│   ├── app/           # アプリSDK
│   ├── crypto/        # Ed25519署名検証
│   └── backend/       # HAL抽象化
├── umidsp/            # DSPモジュール（Pure DSP）
├── umiusb/            # USB Audio/MIDI
└── umimidi/           # MIDI処理

examples/
├── stm32f4_kernel/    # カーネルバイナリ
├── synth_app/         # シンセアプリ（.umia）
└── headless_webhost/  # WASM開発用
```

## 参考資料

- [UMIP.md](UMIP.md) - Processor仕様
- [UMIC.md](UMIC.md) - Controller仕様
- [UMIM.md](UMIM.md) - バイナリ形式仕様
- [SECURITY.md](SECURITY.md) - セキュリティ分析とアプリ分離
- [CONCEPTS.md](CONCEPTS.md) - C++20 Concepts設計

## 変更履歴

| バージョン | 日付 | 変更内容 |
|------------|------|----------|
| 0.10.0 | 2025-01-24 | 非特権アプリ実行、RTOS タスク管理、Ed25519署名検証 |
| 3.0.0-draft | 2025-01 | 統一main()モデル、UMIP/UMIC/UMIM仕様 |
| 2.0.0 | 2025-01 | パラメータをメンバ変数化、Controller任意化 |
