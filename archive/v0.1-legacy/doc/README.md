# UMI-OS

**電子楽器のためのミニマルRTOS**

組み込み（MCU）で動くコードが、そのままVST/AU/WASMでも動く。

---

## コンセプト

```
┌─────────────────────────────────────────────────────────┐
│           あなたが書くコード（全プラットフォーム共通）      │
│                                                         │
│    struct MySynth : umi::AudioProcessor<MySynth> {      │
│        void process(float** out, ...);                  │
│        void on_midi(const midi::Event& ev);             │
│    };                                                   │
├─────────────────────────────────────────────────────────┤
│                    Host Adapter                         │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐       │
│  │ UMI-OS  │ │  VST3   │ │   AU    │ │  WASM   │       │
│  │  (MCU)  │ │  (DAW)  │ │ (macOS) │ │  (Web)  │       │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘       │
└─────────────────────────────────────────────────────────┘
```

---

## 設計原則

| 原則 | 説明 |
|------|------|
| **組み込みファースト** | MCUで動くコードを基準に設計。他プラットフォームはそれに合わせる |
| **タスク隠蔽** | ユーザーはDSP処理のみ記述。スケジューリングはカーネルが自動化 |
| **マクロ排除** | 全ての切り替えはインクルードパスで行う |
| **ヘッダオンリー** | `#include` のみで使用可能 |
| **C++23** | concepts, constexpr, coroutines を活用 |

---

## 最小限のコード

```cpp
// my_synth.hh - 全プラットフォーム共通
#include <umi/processor.hh>

struct MySynth : umi::AudioProcessor<MySynth> {
    // パラメータ定義
    static constexpr auto params = std::array{
        umi::ParamInfo{"freq", "Frequency", 20, 20000, 440},
    };
    
    // オーディオ処理（必須）
    void process(float** out, const float** in,
                 std::size_t frames, std::size_t channels) {
        for (std::size_t i = 0; i < frames; ++i) {
            out[0][i] = osc.next();
        }
    }
    
    // MIDI処理（オプション）
    void on_midi(const umi::midi::Event& ev) {
        if (ev.type == umi::midi::Type::NoteOn) {
            osc.set_freq(midi_to_freq(ev.data1));
        }
    }
    
private:
    Oscillator osc;
};
```

```cpp
// main.cc (MCU)
#include "my_synth.hh"
int main() {
    MySynth synth;
    umi::run(synth);  // これだけ
}
```

---

## アーキテクチャ概要

```
┌─────────────────────────────────────────────────────┐
│                  AudioProcessor                      │
│            （あなたが書くDSPコード）                   │
├─────────────────────────────────────────────────────┤
│                 System Services                      │
│    Audio Task │ MIDI Server │ Monitor │ Shell       │
│              （カーネルが自動管理）                   │
├─────────────────────────────────────────────────────┤
│                   Core Kernel                        │
│    Scheduler │ Timer │ IPC │ Watchdog               │
├─────────────────────────────────────────────────────┤
│               Hardware Abstraction                   │
│    DMA │ I2S │ GPIO │ UART │ USB                    │
└─────────────────────────────────────────────────────┘
```

---

## 優先度モデル

| 優先度 | 用途 | ユーザー使用 |
|--------|------|-------------|
| Realtime | オーディオタスク | 不可（カーネル予約） |
| High | MIDI等クリティカル | 不可（カーネル予約） |
| Normal | 通常タスク | 可 |
| Low | バックグラウンド | 可 |
| Idle | スリープ | 不可 |

---

## 構成

| 構成 | 用途 | ハードアクセス |
|------|------|---------------|
| **マイクロカーネル** | 製品ファームウェア | カーネル経由のみ（MPU保護） |
| **モノリシック** | 開発ボード | 直接アクセス可 |

どちらでも同じアプリケーションコードが動作。

---

## ディレクトリ構成

```
umi_os/
├── core/                 # カーネル・サブシステム（ヘッダオンリー）
│   ├── umi_kernel.hh
│   ├── umi_audio.hh
│   ├── umi_midi.hh
│   └── ...
├── port/                 # プラットフォーム別実装
│   ├── arm/cortex-m/
│   ├── board/stm32f4/
│   └── board/stub/       # ホストテスト用
├── adapter/              # ホストアダプター
│   ├── mcu/              # UMI-OS (MCU)
│   ├── vst3/             # VST3 プラグイン
│   ├── au/               # Audio Unit
│   └── wasm/             # WebAssembly
├── config/               # 構成別設定
│   ├── microkernel/
│   └── monolithic/
├── doc/                  # ドキュメント
├── test/                 # テスト
└── examples/             # サンプル
```

---

## ビルド

```bash
xmake                    # 全ビルド
xmake test               # ホストテスト
xmake build firmware     # MCUファームウェア
xmake renode-test        # エミュレータテスト
```

---

## ドキュメント

- [設計詳細](DESIGN_DETAIL.md) - カーネル、サブシステム、APIの詳細
- [ホストアダプター](ADAPTER.md) - VST/AU/WASM対応の仕組み
- [ポーティング](../port/README.md) - 新規ボード対応

---

## ターゲット

- **MCU**: ARM Cortex-M4F (STM32F4xx)
- **プラグイン**: VST3, AU
- **Web**: WebAssembly
- **用途**: シンセサイザー、エフェクター、MIDI機器
