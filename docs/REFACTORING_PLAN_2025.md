# UMI リファクタリング計画

## 概要

- プロジェクト名: `umi_os` → `umi`
- ライブラリとして使いやすい構造に再編成
- `port/` は削除、カスタムボードはアプリ側で管理

## 最終構造

```
umi/
├── lib/
│   ├── umios/                  # OS
│   │   ├── core/               # インターフェース定義
│   │   ├── kernel/             # カーネル実装
│   │   ├── backend/
│   │   │   ├── cm/             # Cortex-M
│   │   │   ├── wasm/           # WASM
│   │   │   └── freertos/       # (将来)
│   │   ├── adapter/
│   │   │   ├── umim_adapter.hh
│   │   │   ├── embedded_adapter.hh
│   │   │   └── web/
│   │   ├── docs/
│   │   └── xmake.lua
│   │
│   ├── hal/                    # Hardware Abstraction Layer
│   │   ├── stm32/
│   │   ├── stm32f4/
│   │   ├── docs/
│   │   └── xmake.lua
│   │
│   ├── bsp/                    # Board Support Package
│   │   ├── stm32f4-disco/
│   │   ├── stub/
│   │   └── xmake.lua
│   │
│   ├── umidi/                  # MIDIライブラリ
│   │   ├── include/umidi/
│   │   ├── docs/
│   │   ├── test/
│   │   ├── examples/
│   │   └── xmake.lua
│   │
│   ├── umidsp/                 # DSPライブラリ
│   │   ├── include/umidsp/
│   │   ├── docs/
│   │   ├── test/
│   │   └── xmake.lua
│   │
│   ├── umiui/                  # UI状態管理
│   │   ├── include/umiui/
│   │   ├── docs/
│   │   ├── test/
│   │   └── xmake.lua
│   │
│   ├── umigui/                 # GUI描画
│   │   ├── include/umigui/
│   │   ├── docs/
│   │   ├── test/
│   │   └── xmake.lua
│   │
│   └── umiboot/                # ブートローダーロジック
│       ├── include/umiboot/
│       ├── docs/
│       ├── test/
│       └── xmake.lua
│
├── docs/                       # プロジェクト全体ドキュメント
├── examples/                   # アプリケーション例
├── tests/                      # 統合テスト
├── tools/
│   └── renode/                 # シミュレーション環境
└── xmake.lua                   # ルートビルド
```

## ライブラリ独立性

| ライブラリ | 独立 | 依存先 |
|-----------|------|--------|
| umidi | ✓ | なし |
| umidsp | ✓ | なし |
| umiui | ✓ | なし |
| umiboot | ✓ | なし |
| umigui | ✓ | なし |
| umios | ✓ | なし |
| hal | ✓ | なし |
| bsp | - | hal, umios/backend |

## includeパス

```cpp
#include <umios/core/types.hh>
#include <umios/kernel/umi_kernel.hh>
#include <umios/backend/cm/cortex_m4.hh>
#include <umios/adapter/umim_adapter.hh>
#include <hal/stm32/gpio.hh>
#include <bsp/stm32f4-disco/hw_impl.hh>
#include <umidi/ump.hh>
#include <umidsp/filter.hh>
#include <umiui/controls.hh>
#include <umigui/backend.hh>
```

## アプリケーションでの使用

```
my-synth/
├── umi/                    # git submodule
├── boards/                 # カスタムボード（必要なら）
│   └── my-custom-hw/
├── src/
│   └── my_processor.hh
└── xmake.lua
```

```lua
-- xmake.lua
includes("umi")
target("my-synth")
    add_deps("umios", "umidi", "bsp-stm32f4-disco")
```

## 移行マッピング

| 現在 | 移行先 |
|------|--------|
| `lib/umios/*.hh` (types等) | `lib/umios/core/` |
| `lib/umios/umi_*.hh` | `lib/umios/kernel/` |
| `port/arm/cortex-m/` | `lib/umios/backend/cm/` |
| `lib/umim/web_sim.hh` 等 | `lib/umios/backend/wasm/` |
| `lib/umim/*_adapter.hh` | `lib/umios/adapter/` |
| `lib/umim/*.js` | `lib/umios/adapter/web/` |
| `port/vendor/stm32/` | `lib/hal/stm32/` |
| `port/board/stm32f4/` | `lib/bsp/stm32f4-disco/` |
| `port/board/stub/` | `lib/bsp/stub/` |
| `doc/` | `docs/` |
| `test/` | `tests/` |
| `renode/` | `tools/renode/` |
| `lib/umim/` | 削除（umios/に統合） |
| `port/` | 削除 |

## 実施手順

1. [ ] プロジェクト名変更 (umi_os → umi)
2. [ ] lib/umios/ 分割 (core/, kernel/)
3. [ ] lib/umios/backend/ 作成 (cm/, wasm/)
4. [ ] lib/umios/adapter/ 作成 (umim統合)
5. [ ] lib/hal/ 作成 (port/vendor/から移動)
6. [ ] lib/bsp/ 作成 (port/board/から移動)
7. [ ] lib/umim/ 削除
8. [ ] port/ 削除
9. [ ] doc/ → docs/ リネーム
10. [ ] test/ → tests/ リネーム
11. [ ] renode/ → tools/renode/ 移動
12. [ ] 独立ライブラリにdocs/, xmake.lua追加
13. [ ] .gitignore更新 (ビルド成果物除外)
14. [ ] ルートxmake.lua更新
15. [ ] README.md, ARCHITECTURE.md更新
