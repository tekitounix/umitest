# UMI API リファレンス

UMI の主要な API を解説します。

---

## ドキュメント構成

| ファイル | 内容 |
|----------|------|
| [API_APPLICATION.md](API_APPLICATION.md) | アプリケーションAPI（main, AudioContext, Event, コルーチン） |
| [API_UI.md](API_UI.md) | UI API（Input/Output抽象化、属性、共有メモリ） |
| [API_DSP.md](API_DSP.md) | DSPモジュール（オシレータ、フィルタ、エンベロープ） |
| [API_KERNEL.md](API_KERNEL.md) | Kernel API（syscall, IRQ, 優先度、キュー） |

---

## クイックスタート

### 最小アプリケーション

```cpp
#include <umi/app.hh>
#include <umi/ui.hh>

struct Synth {
    float gain = 1.0f;
    
    void process(umi::AudioContext& ctx) {
        auto* out = ctx.output(0);
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = generate() * gain;
        }
    }
};

int main() {
    auto& shared = umi::get_shared();
    umi::ui::Input input(shared);
    umi::ui::Output output(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        
        synth.gain = input[0];
        output[0] = synth.get_level();
    }
    
    return 0;
}
```

### 構成要素

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application                                                        │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐      │
│  │ main()          │  │ Processor       │  │ UI (Input/Output)│     │
│  │ Control Task    │  │ process()       │  │ 共有メモリ経由   │     │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              v
┌─────────────────────────────────────────────────────────────────────┐
│  Kernel + BSP                                                       │
│  - syscall処理                                                       │
│  - オーディオDMA                                                     │
│  - HWドライバ                                                        │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [UMIM_SPEC.md](UMIM_SPEC.md) - バイナリ形式
- [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) - セキュリティとメモリ保護
