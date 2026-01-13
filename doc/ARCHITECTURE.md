# umi アーキテクチャ

## 概要

umiは「1つのProcessorコードからあらゆるターゲットに向けてコンパイルできる」フレームワークです。

### 設計思想

1. **オーディオ処理を最優先** - すべての設計判断においてリアルタイムオーディオ処理の確実性を優先
2. **正規化されたデータ** - アプリケーションはハードウェア非依存の正規化データのみを扱う
3. **プラットフォーム抽象化** - Adapter層とPAL層でホスト環境の差異を吸収
4. **組み込み最適化** - 制限されたリソースに最適化することで全環境での動作を保証

## システム構成

```
┌─────────────────────────────────────────────────────────────┐
│                     Application                             │
│              (Processor実装、プラットフォーム非依存)           │
├─────────────────────────────────────────────────────────────┤
│                    Processor API                            │
│              (process(AudioContext&), etc.)                 │
├──────────────┬──────────────┬──────────────┬───────────────┤
│   Adapter    │   Adapter    │   Adapter    │    Adapter    │
│   Embedded   │   UPF/WASM   │   VST3/AU    │     CLAP      │
├──────────────┼──────────────┴──────────────┴───────────────┤
│   umios      │                                             │
│   ┌────────┐ │              Host Runtime                   │
│   │  PAL   │ │       (Browser, DAW, Standalone)            │
│   ├────────┤ │                                             │
│   │  RTOS  │ │                                             │
│   └────────┘ │                                             │
└──────────────┴─────────────────────────────────────────────┘
     組み込み              デスクトップ / Web
```

### 各層の責務

| 層 | 責務 | 共通化 |
|----|------|--------|
| **Application** | DSP処理、パラメータ状態 | ✓ 100% |
| **Processor API** | オーディオI/Oの抽象化 | ✓ 100% |
| **Adapter** | ホスト形式との変換 | ターゲット固有 |
| **umios** | 組み込み向けOS層 (PAL + RTOS) | 組み込みのみ |

## Processorモデル

### ProcessorLike Concept

継承なしのC++20 concept:

```cpp
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};
```

Processor Concepts:

| Concept | 必須メソッド | 説明 |
|---------|--------------|------|
| `ProcessorLike` | `process(AudioContext&)` | オーディオ処理 (リアルタイム) |
| `Controllable` | `control(ControlContext&)` | パラメータ更新など (非リアルタイム) |
| `HasParams` | `params()` | パラメータメタデータ |
| `HasPorts` | `ports()` | ポートメタデータ |
| `Stateful` | `save_state()`, `load_state()` | 状態の保存/復元 |

**注**: `note_on/off`, `set_param/get_param` はProcessor APIではなく、
アプリケーション固有のメソッドとしてアダプタが呼び出します。

### ドメイン分離

```
┌─────────────────────────────────────────────────────────────┐
│                    Audio Domain                             │
│         サンプル精度が要求される / process()内で処理          │
│                                                             │
│  - Audio Stream                                             │
│  - MIDI Events (サンプル位置補正済み)                        │
│  - Sample-accurate Parameter Changes                        │
├─────────────────────────────────────────────────────────────┤
│                   Control Domain                            │
│         バッファ単位以下の精度で十分 / 非同期で処理可          │
│                                                             │
│  - UI Events                                                │
│  - File I/O 完了通知                                        │
│  - Network Messages                                         │
└─────────────────────────────────────────────────────────────┘
```

## UMIM (UMI-Module)

Processorを最小単位としたヘッドレスなオーディオモジュール。
モジュラーシンセ/エフェクターのように、モジュール同士をパッチングして接続可能。

### 特徴

- **ヘッドレス** - UIを持たない純粋なオーディオ処理単位
- **統合I/O** - Audio / MIDI / CV・Gate / パラメータを統一的に扱う
- **パッチング** - モジュール間の入出力を自由に接続
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

### パッチング (将来構想)

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
| `umim_adapter.hh` | WASM (ブラウザ、Node.js) | ✓ 実装済 |
| `embedded_adapter.hh` | STM32, ESP32等 | ✓ 実装済 |
| VST3/AU/CLAP | DAW | 計画中 |

## umios (組み込みOS/Kernel)

組み込み環境向けのリアルタイムOS層。

### バックエンド切替

| バックエンド | 対象 | 状態 |
|-------------|------|------|
| **umios core** | Cortex-M | ✓ 実装中 |
| **POSIX** | Linux, macOS | ✓ ホストテスト用 |
| **FreeRTOS** | ESP32, STM32 | 計画中 |
| **Zephyr** | 多様なMCU | 計画中 |

### 保護レベル

| レベル | ハードウェア | 保護方式 | 対象 |
|--------|--------------|----------|------|
| L3 | MMUあり | 完全メモリ分離 | Cortex-A |
| L2 | MPUあり | 軽量メモリ分離 | Cortex-M3/M4/M7 |
| L1 | 保護機構なし | 論理分離のみ | ESP32, RP2040, M0 |

## UI/View システム

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
├── core/              # 基本型、AudioContext、Event
├── dsp/               # DSPコンポーネント
├── adapter/           # プラットフォームアダプタ
├── ui/                # UI Controls (状態のみ)
└── gui/               # 描画 (Skin, Backend)

port/
├── arm/               # ARM共通
└── board/             # ボード固有HAL (stm32f4, esp32)

examples/
├── embedded/          # 組み込みサンプル
└── workbench/         # 開発ワークベンチ
```

## 参考資料

- [API.md](API.md) - APIリファレンス
- [UMIM_SPEC.md](UMIM_SPEC.md) - WASMプラグイン仕様
- [archive/v0.1-legacy/doc/NEW_DESIGN.md](../archive/v0.1-legacy/doc/NEW_DESIGN.md) - 詳細設計仕様書
