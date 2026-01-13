# umi

**Universal Musical Instruments Framework**

1つのProcessor実装から、あらゆるターゲットに向けてビルドできるオーディオフレームワーク。

## 特徴

- **ワンソース・マルチターゲット** - 同一のC++コードが組み込み、WASM、デスクトッププラグインで動作
- **モジュラー設計** - Audio / MIDI / CV・Gate を統合的に扱い、モジュール間パッチング可能
- **コンセプトベースAPI** - C++20 conceptsによる軽量な型制約、vtableなし
- **サンプル精度イベント** - MIDIとパラメータ変更をサンプル単位で処理
- **組み込み最適化** - リアルタイム処理に必要な制約を設計に組み込み

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│                     Application                             │
│              (Processor実装、プラットフォーム非依存)           │
├─────────────────────────────────────────────────────────────┤
│                    Processor API                            │
│              (process(AudioContext&), etc.)                 │
├──────────────┬──────────────┬──────────────┬───────────────┤
│   Adapter    │   Adapter    │   Adapter    │    Adapter    │
│   Embedded   │    UMIM      │   VST3/AU    │     CLAP      │
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

## クイックスタート

### 必要環境

- xmake (ビルドシステム)
- C++23対応コンパイラ (Clang 18+, GCC 14+)
- Emscripten (WASMビルド用)

### ビルド

```bash
# ホストテスト
xmake build -a
xmake test

# WASMビルド
xmake f -p wasm
xmake build wasm_synth

# 組み込み (STM32)
xmake f -p cross -a arm --mcu=stm32f407vg
xmake build firmware
```

### 最小のProcessor実装

```cpp
#include <core/audio_context.hh>
#include <dsp/dsp.hh>

// ProcessorLike conceptを満たす最小実装
class MySynth {
    umi::dsp::Sine osc_;
    float freq_norm_ = 0.0f;

public:
    // 必須: process(AudioContext&)
    void process(umi::AudioContext& ctx) {
        auto* out = ctx.output(0);
        if (!out) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = osc_.tick(freq_norm_) * 0.3f;
        }
    }

    // オプション: ポート定義 (HasPorts concept)
    std::span<const umi::PortDescriptor> ports() const { return ports_; }

private:
    static constexpr umi::PortDescriptor ports_[] = {
        {0, "Audio Out", umi::PortKind::Continuous, umi::PortDirection::Out, .channels = 1},
    };
};
```

### UMIMモジュールとしてエクスポート

```cpp
#include <adapter/umim_adapter.hh>
#include "my_synth.hh"

UMIM_MODULE(MySynth)
```

## ディレクトリ構成

```
umi/
├── lib/                    # コアライブラリ
│   ├── core/               # 基本型、AudioContext、Event、Processor
│   ├── dsp/                # DSPコンポーネント (Oscillator, Filter, Envelope)
│   └── adapter/            # プラットフォームアダプタ (UMIM, Embedded)
│
├── port/                   # ハードウェア固有実装
│   └── board/              # ボード固有HAL (stm32f4, esp32等)
│
├── examples/               # サンプルアプリケーション
│   ├── embedded/           # 組み込み向けサンプル
│   └── workbench/          # 開発用ワークベンチ
│
├── test/                   # テスト
├── doc/                    # ドキュメント
└── archive/                # レガシーコード (参照用)
```

## ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) | 全体アーキテクチャと設計思想 |
| [doc/API.md](doc/API.md) | APIリファレンス |
| [doc/UMIM_SPEC.md](doc/UMIM_SPEC.md) | UMI-Module (WASM) フォーマット仕様 |
| [doc/TEST_STRATEGY.md](doc/TEST_STRATEGY.md) | テスト戦略 |

## コンポーネント

### umios (組み込みOS/Kernel)

組み込み環境向けのリアルタイムOS層。バックエンドとして:

- **umios core** - Cortex-M向け独自ミニマルRTOS実装
- **FreeRTOS** - ESP32等のFreeRTOS必須環境向け
- **POSIX** - Linux/macOS向け

### Processor API

全ターゲットで共通のオーディオ処理インターフェース:

```cpp
// C++20 Conceptsによる型制約
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};
```

### DSPライブラリ

組み込み向けに最適化されたDSPコンポーネント:

- **Oscillator** - Sine, Saw, Square, Triangle (BL版あり)
- **Filter** - OnePole, Biquad, SVF
- **Envelope** - ADSR, Ramp
- **Utility** - midi_to_freq, db_to_gain, soft_clip

## ライセンス

MIT License

## 参考資料

- [archive/v0.1-legacy/doc/NEW_DESIGN.md](archive/v0.1-legacy/doc/NEW_DESIGN.md) - 詳細設計仕様書
