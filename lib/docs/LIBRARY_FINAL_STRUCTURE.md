# UMI ライブラリ構成最終案

**決定日**: 2026-02-05  
**方針**: 独立ライブラリ構成 + 統一された内部構造

---

## 基本方針

1. **lib直下に独立ライブラリを配置** — `lib/umi/` 統合ではなく、`lib/umios/`, `lib/umimmio/` など並列配置
2. **常に `include/<lib>/` ディレクトリを使用** — 単一ヘッダでも機能別でも統一
3. **namespaceは `umi::` 以下に統一** — フォルダ名とnamespaceは分離
4. **統合ヘッダは機能名.hh** — 例: `test.hh`, `bench.hh`, `mmio.hh`
5. **重複を避ける** — umiosが依存するライブラリは独立ライブラリとして配置

---

## 標準ディレクトリ構造

各ライブラリは以下の構造に従う：

```
lib/<libname>/
├── README.md                  # [必須] ライブラリ概要
├── xmake.lua                  # [必須] ビルド定義
├── docs/                      # [必須] ドキュメント
│   └── DESIGN.md             # 設計思想
├── include/<libname>/         # [必須] 公開ヘッダ
│   ├── <feature>.hh          # [必須] 統合ヘッダ（機能名）
│   └── ...                    # 機能別ヘッダ
├── test/                      # [必須] テスト
│   ├── xmake.lua
│   └── test_*.cc
├── examples/                  # [任意] サンプル
└── target/                    # [任意] ターゲット固有
```

---

## 作成済みライブラリ

### umitest - テストフレームワーク

```
lib/umitest/
├── README.md
├── xmake.lua
├── docs/DESIGN.md
├── include/umitest/
│   └── test.hh               # 統合ヘッダ → #include <umitest/test.hh>
└── test/
    ├── xmake.lua
    └── test_umitest.cc
```

**namespace**: `umi::test`  
**統合ヘッダ**: `#include <umitest/test.hh>`

### umibench - ベンチマークフレームワーク

```
lib/umibench/
├── README.md
├── xmake.lua
├── docs/
├── include/umibench/
│   ├── bench.hh              # 統合ヘッダ → #include <umibench/bench.hh>
│   ├── core/
│   ├── platform/
│   ├── timer/
│   └── output/
├── test/
│   ├── xmake.lua
│   └── test_bench.cc
├── examples/
└── target/stm32f4/
```

**namespace**: `umi::bench`  
**統合ヘッダ**: `#include <umibench/bench.hh>`

### umimmio - MMIO抽象

```
lib/umimmio/
├── README.md
├── xmake.lua
├── docs/
└── include/umimmio/
    ├── mmio.hh               # 統合ヘッダ → #include <umimmio/mmio.hh>
    ├── register.hh
    └── transport/
```

**namespace**: `umi::mmio`  
**統合ヘッダ**: `#include <umimmio/mmio.hh>`

### umistring - 文字列ユーティリティ（検証済み）

```
lib/umistring/
├── README.md
├── xmake.lua
├── docs/DESIGN.md
├── include/umistring/
│   ├── string.hh
│   ├── convert.hh
│   ├── split.hh
│   └── trim.hh
└── test/
    └── test_umistring.cc
```

**namespace**: `umi::string`  
**統合ヘッダ**: 未作成（個別include推奨）

---

## namespace 対応表

| ライブラリ | namespace | include例 |
|-----------|-----------|-----------|
| `umitest` | `umi::test` | `#include <umitest/test.hh>` |
| `umibench` | `umi::bench` | `#include <umibench/bench.hh>` |
| `umimmio` | `umi::mmio` | `#include <umimmio/mmio.hh>` |
| `umistring` | `umi::string` | `#include <umistring/string.hh>` |

---

## 親 xmake.lua

```lua
includes("lib/umimmio")
includes("lib/umitest")
includes("lib/umibench")
includes("lib/umistring")
```

---

## ビルド検証済み

```bash
xmake build umitest      # ✅ success
xmake build umibench     # ✅ success
xmake build umimmio      # ✅ success
xmake build umistring    # ✅ success
```

---

## 次のステップ

1. 残りのライブラリ移行: `umidsp`, `umimidi`, `umicrypto`, `umiutil`, etc.
2. 既存 `lib/umi/` の削除（移行完了後）
3. `examples/` の再有効化
