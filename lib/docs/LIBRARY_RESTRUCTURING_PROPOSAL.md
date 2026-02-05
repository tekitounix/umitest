# UMI ライブラリ構成再検討 — sys/ サブディレクトリ案

**作成日**: 2026-02-05  
**目的**: umios（組み込みOS）概念を保持しつつ、namespace統一を達成する改良案の検討

---

## 背景

LIBRARY_REFACTORING.md では「すべてを `lib/umi/` 以下に平坦に配置」する統合構造を計画している。しかし以下の懸念がある：

1. **umios（組み込みOS関連）の境界が不明確**  
   kernel/, port/, service/, mmio/, boot/ は「組み込みでしか使わない」が、平坦配置ではその区別が視覚的に分からない

2. **ターゲット依存性の混在**  
   dsp/, midi/ はターゲット非依存だが、kernel/ は組み込み専用。同じ階層にあることで誤解を生みやすい

3. **「完全分離」の誘惑**  
   `lib/umios/` を作れば解決するように見えるが、実際は synth_app なども umios 上で動作するため「独立したライブラリ」として分離は不自然

---

## 検討したアプローチ

### 案A: 完全統合（現在の計画）
```
lib/umi/
├── core/       # ターゲット非依存
├── dsp/        # ターゲット非依存
├── kernel/     # 組み込み専用（平坦配置）
├── port/       # HAL（平坦配置）
└── ...
```

**問題**: ターゲット依存性がフォルダ名から読み取れない

---

### 案B: umios 分離（却下）
```
lib/
├── umi/        # umi:: namespace
│   ├── core/
│   └── dsp/
└── umios/      # umios:: namespace?
    ├── kernel/
    └── port/
```

**却下理由**:
- namespace が分裂：`umi::core` vs `umios::kernel`
- includeパスが分裂：`#include <umi/dsp/...>` vs `#include <umios/kernel/...>`
- synth_app は umios 上で動作するため、分離は概念的に不整合

---

### 案C: sys/ サブディレクトリ（推奨）
```
lib/umi/
├── core/           # umi::core — ターゲット非依存（最下層）
├── dsp/            # umi::dsp — ターゲット非依存
├── midi/           # umi::midi — ターゲット非依存
├── synth/          # umi::synth — ターゲット非依存
│
├── sys/            # 【新設】umi::sys — 「umios」相当
│   ├── kernel/     # umi::sys::kernel — RTOS（組み込み専用）
│   ├── port/       # umi::sys::port — HAL/BSP
│   ├── service/    # umi::sys::service — System Services
│   ├── mmio/       # umi::sys::mmio — レジスタ抽象
│   └── boot/       # umi::sys::boot — ブートローダー
│
├── runtime/        # umi::runtime — ランタイム（coreに依存）
├── coro/           # umi::coro — コルーチン（coreに依存）
├── adapter/        # umi::adapter — バックエンドアダプタ
│
├── util/           # umi::util — 共通ユーティリティ
├── crypto/         # umi::crypto — 暗号
├── fs/             # umi::fs — ファイルシステム
├── ui/             # umi::ui — UI状態
├── gfx/            # umi::gfx — グラフィック
├── usb/            # umi::usb — USBスタック
├── shell/          # umi::shell — シェルプリミティブ
├── test/           # umi::test — テストフレームワーク
└── ref/            # umi::ref — リファレンス実装
```

---

## 案Cの詳細

### namespace 設計

| 機能 | namespace | includeパス |
|------|-----------|-------------|
| AudioContext | `umi::core` | `<umi/core/audio_context.hh>` |
| Sine | `umi::dsp` | `<umi/dsp/oscillator/sine.hh>` |
| Scheduler | `umi::sys::kernel` | `<umi/sys/kernel/scheduler.hh>` |
| GPIO | `umi::sys::port` | `<umi/sys/port/gpio.hh>` |
| Shell | `umi::sys::service::shell` | `<umi/sys/service/shell/shell.hh>` |
| MMIO | `umi::sys::mmio` | `<umi/sys/mmio/register.hh>` |
| Boot | `umi::sys::boot` | `<umi/sys/boot/dfu.hh>` |

**原則**:
- すべて `umi::` 以下に統一
- `umi::sys::` = 「組み込み/OS関連」の印

---

### アーキテクチャの視覚化

```
lib/umi/
├── core/           ← Application層（型定義、依存なし）
├── dsp/            ← ドメイン: オーディオ処理（ターゲット非依存）
├── midi/           ← ドメイン: MIDIプロトコル（ターゲット非依存）
├── synth/          ← ドメイン: シンセ実装（dspに依存）
│
├── sys/            ← 【umios 相当】システム層
│   ├── mmio/       ← 最下位（レジスタ抽象）
│   ├── port/       ← HAL（mmioに依存）
│   ├── kernel/     ← RTOS（portに依存、組み込み専用）
│   ├── service/    ← System Services
│   └── boot/       ← ブートローダー（組み込み専用）
│
├── runtime/        ← Runtime層（coreに依存）
├── coro/           ← 協調的マルチタスク（coreに依存）
├── adapter/        ← Backend Adapter
│
├── fs/             ← ドメイン: ファイルシステム
├── usb/            ← ドメイン: USBスタック
├── crypto/         ← ドメイン: 暗号（独立）
├── ui/             ← ドメイン: UI入力
├── gfx/            ← ドメイン: UI描画
├── shell/          ← ドメイン: ホスト側シェルツール
├── util/           ← ドメイン: 共通ユーティリティ
├── test/           ← ドメイン: テストフレームワーク
└── ref/            ← ドメイン: リファレンス実装
```

---

### 依存グラフ（案C）

```
                    ┌─────────────────┐
                    │ umi::sys::mmio  │ ← 最下位（レジスタ抽象）
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ umi::sys::port  │ ← HAL（mmioに依存）
                    └────────┬────────┘
                             │
         ┌───────────────────┼───────────────────┐
         │                   │                   │
         ▼                   ▼                   ▼
  ┌─────────────┐      ┌─────────────┐    ┌─────────────────┐
  │ umi::core   │      │ umi::util   │    │ umi::sys::kernel│ ← 組み込み専用
  │(Application)│      │ (共通)      │    │   (Backend)     │
  └──────┬──────┘      └─────────────┘    └────────┬────────┘
         │                                          │
    ┌────┴────┐                                     │
    │         │                                     │
    ▼         ▼                                     │
 ┌──────┐ ┌──────┐ ┌─────────────────┐              │
 │umi:: │ │umi:: │ │ umi::runtime    │◀─────────────┘
 │app   │ │coro  │ │   (Runtime)     │
 └──────┘ └──────┘ └─────────────────┘
                           │
                           ▼
              ┌─────────────────────────┐
              │ umi::sys::service       │ ← System Service
              │ - shell                 │
              │ - audio                 │
              │ - midi                  │
              │ - storage               │
              └─────────────────────────┘
                           │
                           ▼
                   ┌───────────────┐
                   │ umi::adapter  │
                   └───────────────┘

ドメイン層（独立またはcoreに依存）:
  umi::crypto, umi::fs, umi::ui, umi::gfx, umi::shell,
  umi::util, umi::test, umi::ref, umi::usb

ドメイン層（core + 他ドメインに依存）:
  umi::dsp（ターゲット非依存）
  umi::midi（プロトコル、ターゲット非依存）
  umi::synth（dspに依存）
```

---

## 案Cの利点

### 1. ターゲット依存性の視覚的明確化
```cpp
#include <umi/dsp/oscillator/sine.hh>      // → ターゲット非依存
#include <umi/sys/kernel/scheduler.hh>      // → 組み込み専用（sys/が目印）
```

### 2. namespace の一貫性
```cpp
umi::dsp::Sine osc;                    // OK
umi::sys::kernel::Scheduler sched;     // OK（分裂なし）
```

### 3. 「umios」概念の保持
- 物理フォルダ `sys/` = 論理概念「umios」
- ドキュメント: 「umios は umi::sys のこと」

### 4. 将来的な拡張性
```
lib/umi/sys/
├── kernel/       # Cortex-M用RTOS
├── port/         # HAL/BSP
├── service/      # System Services
├── mmio/         # レジスタ抽象
├── boot/         # ブートローダー
└── host/         # 【将来】ホストOS抽象（Linux/macOS用）
```

### 5. ホスト/WASMビルド時のメンタルモデル
```
「umios 関係ないから sys/ は無視していい」
→ 視覚的に明確
```

---

## 案Cの欠点

### 1. namespace が深くなる
```cpp
// 案A（現在の計画）
umi::kernel::Scheduler sched;

// 案C
umi::sys::kernel::Scheduler sched;
```

### 2. 移行工数が増加
- kernel/, port/, service/, mmio/, boot/ を sys/ 以下に移動する追加作業
- namespace 変更: `umi::kernel` → `umi::sys::kernel`

### 3. 「sys」という名前の曖昧さ
- 「system」の略だが、一般的すぎる可能性
- 代替案: `osi/` (Operating System Interface), `hal/`, `platform/`

---

## 命名案の比較

| フォルダ名 | namespace | 評価 |
|-----------|-----------|------|
| `sys/` | `umi::sys::` | 簡潔だが一般的すぎる |
| `osi/` | `umi::osi::` | 意味は明確だが馴染みがない |
| `hal/` | `umi::hal::` | port/ と重複する意味合い |
| `platform/` | `umi::platform::` | 長いが明確 |
| `system/` | `umi::system::` | sys/ と同義だが長い |

**推奨**: `sys/`（簡潔さを優先。ドキュメントで「umios = umi::sys」と定義）

---

## 案A vs 案C の比較

| 観点 | 案A: 完全統合 | 案C: sys/ サブディレクトリ |
|------|--------------|---------------------------|
| **namespace** | `umi::kernel` | `umi::sys::kernel` |
| **パス** | `<umi/kernel/...>` | `<umi/sys/kernel/...>` |
| **ターゲット依存性の明確さ** | ❌ 分からない | ✅ sys/ が目印 |
| **シンプルさ** | ✅ 平坦で分かりやすい | ⚠️ 1階層深い |
| **umios 概念** | ❌ 消失 | ✅ 保持（sys/ = umios） |
| **業界標準との整合** | ✅ Abseil風 | ✅ Abseil風 + 機能分離 |
| **移行工数** | 基準 | +20%（namespace変更） |

---

## 結論と推奨

### 推奨: 案C（sys/ サブディレクトリ）

理由：
1. **長期的な可読性** > **短期的なシンプルさ**
2. 「組み込み専用」コードが明確に分離されることで、ホスト/WASM開発者の認知負荷が減少
3. umios という概念を構造に残せる

### 採用する場合の修正点

LIBRARY_REFACTORING.md の以下を変更：

```
# 変更前
lib/umi/kernel/ → umi::kernel
lib/umi/port/   → umi::port

# 変更後
lib/umi/sys/kernel/ → umi::sys::kernel
lib/umi/sys/port/   → umi::sys::port
lib/umi/sys/mmio/   → umi::sys::mmio
lib/umi/sys/boot/   → umi::sys::boot
lib/umi/sys/service/ → umi::sys::service
```

### 移行ステップへの影響

Phase 2 に以下を追加：
- Step X: `umi/sys/` サブディレクトリ作成
- kernel/, port/, mmio/, boot/, service/ を sys/ 以下に移動
- namespace 変更: `umi::kernel` → `umi::sys::kernel` など

---

## 補足: 案1~4は一般的な構成か？

### 結論

**案2（include分離）と案3（機能別分割）が業界標準**であり、案1（フラット）と案4（ヘッダオンリー特化）は特定のケースでのみ使われる。

### 主要ライブラリの実際の構成

| ライブラリ | 構成パターン | includeパス | 実装ファイル |
|-----------|------------|-------------|------------|
| **Abseil** | 案2・案3混合 | `include/absl/strings/str_join.h` | `.cc` あり |
| **fmt** | 案2 | `include/fmt/format.h` | `.cc` あり（ヘッダオンリーも可） |
| **spdlog** | 案3 | `include/spdlog/spdlog.h` | ヘッダオンリー基本 |
| **Catch2** | 案3 | `include/catch2/catch_test_macros.hpp` | ヘッダオンリー |
| **nlohmann/json** | 案1（単一ヘッダ） | `include/nlohmann/json.hpp` | ヘッダオンリー |
| **Boost** | 案3 | `boost/algorithm/string.hpp` | ほぼヘッダオンリー |
| **STL** | 案3 | `<vector>`, `<algorithm>` | ヘッダオンリー（実装的には） |

### 詳細

#### Abseil (Google)
```
abseil-cpp/
├── absl/
│   ├── strings/
│   │   ├── str_join.h       # 公開ヘッダ
│   │   ├── str_split.h
│   │   └── internal/        # 内部ヘッダ
│   │       └── str_join_internal.h
│   └── ...
```
- **パターン**: 案3（機能別分割）
- **内部ヘッダ**: `internal/` サブディレクトリ使用
- **命名**: `str_join.h`（スネークケース）

#### fmt
```
fmt/
├── include/fmt/
│   ├── format.h             # メイン公開ヘッダ
│   ├── core.h               # 公開ヘッダ
│   ├── printf.h             # 公開ヘッダ
│   └── ...
├── src/
│   └── format.cc            # 実装ファイル
```
- **パターン**: 案2（include分離）+ `.cc` 実装
- **ヘッダオンリー対応**: `FMT_HEADER_ONLY` マクロで切り替え可能

#### spdlog
```
spdlog/
├── include/spdlog/
│   ├── spdlog.h             # メイン公開ヘッダ
│   ├── logger.h
│   ├── sinks/
│   │   ├── basic_file_sink.h
│   │   └── stdout_color_sinks.h
│   └── fmt/
│       └── ...
```
- **パターン**: 案3（機能別分割）
- **ヘッダオンリー**: 基本構成（コンパイル時間短縮版も提供）

#### nlohmann/json
```
json/
├── include/nlohmann/
│   ├── json.hpp             # 単一ヘッダ（全機能）
│   └── json_fwd.hpp         # 前方宣言のみ
```
- **パターン**: 案1（フラット・単一ヘッダ）
- **特徴**: 小規模ライブラリの理想形

### 一般的な傾向

| 観点 | 一般的な選択 |
|------|-------------|
| **include/<libname>/** | ✅ ほぼ必須。直下配置は稀 |
| **src/** | コンパイルライブラリなら必須。ヘッダオンリーなら不要 |
| **internal/detail/** | どちらも使用。Abseilは`internal/`, fmtは階層なし |
| **機能別サブディレクトリ** | ヘッダ数が5個以上なら必須 |
| **ファイル名** | スネークケースが主流（`str_join.h`） |

### UMIへの推奨

現状（ヘッダオンリー基本方針）に基づくと：

| 規模 | 推奨パターン | 理由 |
|------|-------------|------|
| **core, util** | 案4（ヘッダオンリー特化） | ヘッダ数が少なく、シンプルさ優先 |
| **dsp, kernel, port** | 案3（機能別分割） | ヘッダ数が多く、整理が必要 |
| **midi, fs** | 案2 or 案4 | 中規模。必要に応じて選択 |

**重要**: 案1（フラット）は「単一ヘッダライブラリ」として使われる場合のみ一般的。複数ヘッダを持つライブラリでフラット構成は非推奨。

---

## 個別ライブラリの内部構成

上記は「lib/以下にどう配置するか」の話。ここでは「各ライブラリ内部の構成」を検討する。

### 現状の LIBRARY_STRUCTURE.md の標準構造

```
lib/<libname>/
├── README.md                  # [必須] ライブラリ概要
├── xmake.lua                  # [必須] ビルド定義
├── include/<libname>/         # [必須] 公開ヘッダ
│   ├── core/                  #   機能別サブディレクトリ（任意）
│   └── ...
├── src/                       # [任意] 実装ファイル
├── test/                      # [必須] テスト
└── examples/                  # [推奨] サンプル
```

### 問題点

1. **`include/<libname>/` の冗長性**  
   `#include <umi/core/audio_context.hh>` としたいのに、現状は `lib/umi/core/` 直下にヘッダが存在。  
   → `include/` サブディレクトリを挟むとビルド設定が複雑化

2. **テスト・ベンチマークの分散**  
   各モジュールに `test/`, `bench/` が分散しており、全体像が掴みにくい

3. **`src/` の曖昧さ**  
   ヘッダオンリーが基本方針なのに、`src/` を持つライブラリと持たないライブラリが混在

4. **サブディレクトリの命名規則の不統一**  
   - `dsp/include/core/`, `dsp/include/filter/`  
   - `midi/include/core/`, `midi/include/messages/`  
   - 何を基準に分けるかの指針が不明確

---

### 案1: フラット構造（シンプル優先）

```
lib/umi/core/
├── README.md
├── xmake.lua
├── audio_context.hh      # 公開ヘッダ（直接配置）
├── processor.hh
├── event.hh
├── types.hh
├── src/                    # .cc が必要な場合のみ（任意）
│   └── error.cc
├── test/
│   ├── test_audio_context.cc
│   └── test_event.cc
└── examples/
    └── basic_processor.cc
```

**includeパス**: `#include <umi/core/audio_context.hh>`

**メリット**:
- シンプル、深いネストなし
- xmake.lua の設定が容易

**デメリット**:
- 公開ヘッダと内部ヘッダの区別が不明確
- ファイル数が増えるとごちゃつく

---

### 案2: include分離（厳格な公開API）

```
lib/umi/core/
├── README.md
├── xmake.lua
├── include/umi/core/       # 公開ヘッダ
│   ├── audio_context.hh
│   ├── processor.hh
│   └── event.hh
├── internal/               # 内部ヘッダ（外部非公開）
│   └── detail/
│       └── event_impl.hh
├── src/                    # 実装
│   ├── audio_context.cc
│   └── error.cc
├── test/
│   └── test_*.cc
└── examples/
    └── *.cc
```

**includeパス**: `#include <umi/core/audio_context.hh>`

**メリット**:
- 公開APIと内部実装の明確な分離
- 外部公開するヘッダが明確

**デメリット**:
- xmake.lua で includeパス設定が必要
- 深いネスト（`include/umi/core/`）が冗長

---

### 案3: 機能別分割（推奨・中型〜大型ライブラリ向け）

```
lib/umi/dsp/
├── README.md
├── xmake.lua
├── include/umi/dsp/        # 公開ヘッダ
│   ├── core/
│   │   ├── sample.hh
│   │   └── block.hh
│   ├── oscillator/
│   │   ├── sine.hh
│   │   ├── saw.hh
│   │   └── square.hh
│   ├── filter/
│   │   ├── biquad.hh
│   │   └── svf.hh
│   └── envelope/
│       └── adsr.hh
├── src/                    # 実装（必要な場合）
│   └── biquad.cc
├── test/
│   ├── test_oscillator.cc
│   ├── test_filter.cc
│   └── test_envelope.cc
└── examples/
    └── basic_synth.cc
```

**includeパス**:
```cpp
#include <umi/dsp/oscillator/sine.hh>
#include <umi/dsp/filter/biquad.hh>
```

**メリット**:
- 機能別に整理され、見通しが良い
- 公開APIの構造が明確

**デメリット**:
- 小規模ライブラリではオーバーエンジニアリング
- xmake.lua の設定がやや複雑

---

### 案4: ヘッダオンリー特化（現状の最適化）

```
lib/umi/core/
├── README.md
├── xmake.lua               # headeronly ターゲット
├── audio_context.hh
├── processor.hh
├── event.hh
├── types.hh
├── detail/                 # 内部実装（公開しない）
│   └── event_packing.hh
├── test/
│   └── test_*.cc
└── examples/
    └── *.cc
```

**ポリシー**:
- ヘッダオンリーが基本
- `.cc` が必要になった時点で `src/` を作成
- 内部ヘッダは `detail/` に配置

**メリット**:
- シンプル（案1と同様）
- ヘッダオンリー方針との整合

**デメリット**:
- 「内部実装」を `detail/` に置く習慣が必要

---

### 各ライブラリ規模に応じた推奨構成

| 規模 | 推奨 | 例 |
|------|------|-----|
| **小規模**（ヘッダ5個以下） | 案1: フラット | `core/`, `util/`, `coro/` |
| **中規模**（ヘッダ6-20個） | 案4: ヘッダオンリー特化 + detail/ | `midi/`, `fs/slim/` |
| **大規模**（ヘッダ20個以上） | 案3: 機能別分割 | `dsp/`, `kernel/`, `port/` |
| **厳格API分離が必要** | 案2: include分離 | `crypto/`（検証済みAPI） |

---

### 命名規約の統一

#### サブディレクトリ名（include/以下）

```
include/<libname>/
├── core/           # 基本型・共通定義
├── detail/         # 内部実装（公開しない）
├── codec/          # エンコード/デコード
├── message/        # メッセージ・パケット定義
├── protocol/       # プロトコル実装
├── filter/         # フィルタ処理
├── oscillator/     # オシレータ
├── envelope/       # エンベロープ
├── synth/          # シンセ関連
├── arch/           # アーキテクチャ固有
├── mcu/            # MCU固有
├── board/          # ボード固有
├── platform/       # プラットフォーム固有
└── device/         # デバイスドライバ
```

#### ファイル名

```cpp
// クラス名とファイル名の対応
AudioContext    → audio_context.hh
UMP32          → ump.hh または ump_32.hh
MidiParser     → midi_parser.hh
// 複数クラスを含む場合
Oscillator     → oscillator.hh（Sine, Saw, Squareを含む）
```

---

### xmake.lua の標準テンプレート

#### ヘッダオンリー（小規模）

```lua
target("umi_core")
    set_kind("headeronly")
    add_headerfiles("*.hh")
    add_includedirs(".", { public = true })
```

#### 機能別分割（大規模）

```lua
target("umi_dsp")
    set_kind("headeronly")
    add_headerfiles("include/(umi/dsp/**.hh)")
    add_includedirs("include", { public = true })
```

#### 実装ファイルあり

```lua
target("umi_crypto")
    set_kind("static")
    add_files("src/*.cc")
    add_headerfiles("include/(umi/crypto/**.hh)")
    add_includedirs("include", { public = true })
```

---

## 付録A: 新しい全体構成案（Flat構成）

上述の議論を経て、以下の構成を提案する。

### 基本方針

1. **lib直下に独立ライブラリを配置** — `lib/umi/` 統合ではなく、`lib/umios/`, `lib/umimmio/` など並列配置
2. **namespaceは `umi::` 統一** — フォルダ名とnamespaceは分離（`umios/` → `umi::os`）
3. **重複を避ける** — umiosが依存するmmio/util/cryptoは独立ライブラリとして配置
4. **サブモジュール方式は採用しない** — 依存関係はビルド設定で明示

### ディレクトリ構成

```
lib/
├── umios/                          # OS/組み込み層（親フォルダ）
│   ├── README.md
│   ├── xmake.lua
│   ├── kernel/
│   │   └── scheduler.hh            → #include <umios/kernel/scheduler.hh>
│   │                                 namespace umi::os::kernel
│   ├── port/
│   │   └── gpio.hh                 → #include <umios/port/gpio.hh>
│   │                                 namespace umi::os::port
│   ├── service/
│   │   ├── loader/
│   │   │   └── loader.hh           → #include <umios/service/loader/loader.hh>
│   │   ├── shell/
│   │   │   └── shell.hh            → #include <umios/service/shell/shell.hh>
│   │   ├── audio/
│   │   │   └── audio.hh            → #include <umios/service/audio/audio.hh>
│   │   └── storage/
│   │       └── storage.hh          → #include <umios/service/storage/storage.hh>
│   ├── runtime/
│   │   └── event_router.hh         → #include <umios/runtime/event_router.hh>
│   ├── coro/
│   │   └── task.hh                 → #include <umios/coro/task.hh>
│   └── adapter/
│       ├── embedded.hh             → #include <umios/adapter/embedded.hh>
│       └── web/
│           └── web_adapter.hh      → #include <umios/adapter/web/web_adapter.hh>
│
├── umimmio/                        # MMIO抽象（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   └── register.hh                 → #include <umimmio/register.hh>
│                                     namespace umi::mmio
│
├── umiutil/                        # 共通ユーティリティ（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   ├── ring_buffer.hh              → #include <umiutil/ring_buffer.hh>
│   ├── triple_buffer.hh            → #include <umiutil/triple_buffer.hh>
│   └── log.hh                      → #include <umiutil/log.hh>
│                                     namespace umi::util
│
├── umicrypto/                      # 暗号（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   ├── ed25519.hh                  → #include <umicrypto/ed25519.hh>
│   ├── sha256.hh                   → #include <umicrypto/sha256.hh>
│   └── sha512.hh                   → #include <umicrypto/sha512.hh>
│                                     namespace umi::crypto
│
├── umidsp/                         # DSP処理（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   ├── oscillator/
│   │   ├── sine.hh                 → #include <umidsp/oscillator/sine.hh>
│   │   └── saw.hh                  → #include <umidsp/oscillator/saw.hh>
│   ├── filter/
│   │   ├── biquad.hh               → #include <umidsp/filter/biquad.hh>
│   │   └── svf.hh                  → #include <umidsp/filter/svf.hh>
│   └── envelope/
│       └── adsr.hh                 → #include <umidsp/envelope/adsr.hh>
│                                     namespace umi::dsp
│
├── umimidi/                        # MIDI処理（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   ├── ump.hh                      → #include <umimidi/ump.hh>
│   ├── message.hh                  → #include <umimidi/message.hh>
│   └── sysex.hh                    → #include <umimidi/sysex.hh>
│                                     namespace umi::midi
│
├── umifs/                          # ファイルシステム（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   ├── fat/
│   │   └── ff.hh                   → #include <umifs/fat/ff.hh>
│   └── slim/
│       └── slim.hh                 → #include <umifs/slim/slim.hh>
│                                     namespace umi::fs
│
├── umiui/                          # UI入力（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   └── controls.hh                 → #include <umiui/controls.hh>
│                                     namespace umi::ui
│
├── umigfx/                         # グラフィック描画（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   ├── canvas.hh                   → #include <umigfx/canvas.hh>
│   └── framebuffer.hh              → #include <umigfx/framebuffer.hh>
│                                     namespace umi::gfx
│
├── umisynth/                       # シンセ実装（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   └── voice.hh                    → #include <umisynth/voice.hh>
│                                     namespace umi::synth
│
├── umiusb/                         # USBスタック（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   └── audio_class.hh              → #include <umiusb/audio_class.hh>
│                                     namespace umi::usb
│
├── umitest/                        # テストフレームワーク（独立ライブラリ）
│   ├── README.md
│   ├── xmake.lua
│   └── test.hh                     → #include <umitest/test.hh>
│                                     namespace umi::test
│
└── umiref/                         # リファレンス実装（独立ライブラリ）
    ├── README.md
    ├── xmake.lua
    └── reference.hh                → #include <umiref/reference.hh>
                                        namespace umi::ref
```

### namespace対応表

| フォルダ | namespace | 例 |
|----------|-----------|-----|
| `umios/` | `umi::os` | `umi::os::kernel::Scheduler` |
| `umios/kernel/` | `umi::os::kernel` | `umi::os::kernel::Scheduler` |
| `umios/port/` | `umi::os::port` | `umi::os::port::Gpio` |
| `umios/service/` | `umi::os::service` | `umi::os::service::Loader` |
| `umimmio/` | `umi::mmio` | `umi::mmio::Register` |
| `umiutil/` | `umi::util` | `umi::util::RingBuffer` |
| `umicrypto/` | `umi::crypto` | `umi::crypto::Ed25519` |
| `umidsp/` | `umi::dsp` | `umi::dsp::Sine` |
| `umimidi/` | `umi::midi` | `umi::midi::UMP` |
| `umifs/` | `umi::fs` | `umi::fs::fat::FileSystem` |
| `umiui/` | `umi::ui` | `umi::ui::Controls` |
| `umigfx/` | `umi::gfx` | `umi::gfx::Canvas` |
| `umisynth/` | `umi::synth` | `umi::synth::Voice` |
| `umiusb/` | `umi::usb` | `umi::usb::AudioClass` |
| `umitest/` | `umi::test` | `umi::test::TestCase` |
| `umiref/` | `umi::ref` | `umi::ref::Reference` |

### 依存関係

```
umios/
├── add_deps("umimmio")      # MMIO抽象
├── add_deps("umiutil")       # 共通ユーティリティ
├── add_deps("umicrypto")     # 暗号（署名検証など）
└── add_deps("umiref")        # リファレンス実装

umisynth/
└── add_deps("umidsp")        # DSP処理

umiusb/
└── add_deps("umios")         # OS層（USBドライバはOSに依存）
```

### 重複を避ける理由

`umios/mmio/` を作らず、`umimmio/` を使う理由：

1. **単一ソース（DRY原則）** — バグ修正を1箇所で済ませられる
2. **再利用性** — `umimmio` は `umios` 以外からも使える
3. **テスト容易性** — `umimmio` 単体でテスト可能
4. **明確な依存** — `xmake.lua` で依存関係が明示される

### 採用しない構成

```
❌ 非推奨（重複あり）:
lib/
├── umimmio/
│   └── register.hh
└── umios/
    └── mmio/           # umimmioと重複！
        └── register.hh

❌ 非推奨（サブモジュール）:
lib/
└── umios/
    └── third_party/    # サブモジュール方式
        └── mmio/
```

---

## 付録B: 漏れていたライブラリの配置

### 追加ライブラリ

```
lib/
├── umibench/                       # ベンチマークフレームワーク
│   ├── README.md
│   ├── xmake.lua
│   └── include/umibench/
│       └── bench.hh                → #include <umibench/bench.hh>
│                                     namespace umi::bench
│
├── umishell/                       # ホスト側シェルツール（独立）
│   ├── README.md
│   ├── xmake.lua
│   └── primitives.hh               → #include <umishell/primitives.hh>
│                                     namespace umi::shell
│
└── umitools/                       # 開発ツール（独立）
    ├── README.md
    ├── xmake.lua
    └── dev/
        └── tool.hh                 → #include <umitools/dev/tool.hh>
                                          namespace umi::tools::dev
```

### umiosに統合されるライブラリ

以下は独立ライブラリではなく、`umios/` のサブディレクトリとして統合：

| 旧フォルダ | 新配置 | 理由 |
|-----------|--------|------|
| `boot/` | `umios/boot/` | umios専用（ブートローダー） |
| `core/` | `umios/core/` | umiosの基盤（AudioContext等） |
| `app/` | `umios/app/` | umios上で動作するアプリSDK |
| `coro/` | `umios/coro/` | umiosのコルーチンサポート |
| `runtime/` | `umios/runtime/` | umiosのランタイム |
| `service/` | `umios/service/` | umiosのSystem Services |
| `port/` | `umios/port/` | umiosのHAL/BSP |
| `kernel/` | `umios/kernel/` | umiosのRTOSカーネル |

```
lib/umios/
├── kernel/                         # RTOSカーネル
├── port/                           # HAL/BSP
├── service/                        # System Services
│   ├── loader/
│   ├── shell/
│   ├── audio/
│   ├── midi/
│   └── storage/
├── runtime/                        # ランタイム
├── coro/                           # コルーチン
├── app/                            # アプリSDK
├── core/                           # 基本型（AudioContext等）
├── boot/                           # ブートローダー
└── adapter/                        # アダプタ
```

---

## 付録C: 内部ヘッダとsrcの扱い

### 方針

| 項目 | 配置 | 説明 |
|------|------|------|
| **公開ヘッダ** | `include/<libname>/` | 外部向けAPI |
| **内部ヘッダ** | `detail/` または `internal/` | 実装詳細（非公開） |
| **実装ファイル** | `src/` | `.cc` ファイル |

### 具体例

```
lib/umicrypto/                      # 実装ファイルありの例
├── README.md
├── xmake.lua
├── include/umicrypto/              # 公開ヘッダ
│   ├── ed25519.hh                  → #include <umicrypto/ed25519.hh>
│   ├── sha256.hh                   → #include <umicrypto/sha256.hh>
│   └── detail/                     # 公開ヘッダ内の内部実装
│       └── ed25519_impl.hh         # テンプレート実装等
├── src/                            # 実装ファイル
│   ├── ed25519.cc
│   └── sha256.cc
└── test/
    └── test_crypto.cc
```

```
lib/umidsp/                         # ヘッダオンリーの例
├── README.md
├── xmake.lua
├── include/umidsp/                 # 公開ヘッダ
│   ├── oscillator/
│   │   ├── sine.hh
│   │   └── saw.hh
│   ├── filter/
│   │   ├── biquad.hh
│   │   └── svf.hh
│   └── detail/                     # 内部実装
│       └── oscillator_impl.hh      # 共通実装
└── test/
    └── test_dsp.cc
```

### 内部ヘッダの命名規則

**原則：内部ヘッダは常に相対パスでinclude**

```cpp
// 公開ヘッダ（ユーザーがinclude）- 絶対パス
#include <umidsp/oscillator/sine.hh>

// 内部ヘッダ（ライブラリ内でのみ使用）- 相対パス
#include "detail/oscillator_impl.hh"
```

**理由**:
- 絶対パス（`<umidsp/detail/...>`）だと、ヘッダの場所が変わった時に修正が大変
- 相対パスなら、ファイル構造が保たれればパス変更不要
- 内部ヘッダは「ライブラリ内部でのみ使う」ため、相対パスで十分

---

### 単一ヘッダ + 機能別分割の両対応構造

**原則**: 単一ヘッダライブラリ（`umimmio`）と機能別ライブラリ（`umibench`）の両方に対応できる構造

```
lib/umimmio/                      # 単一ヘッダライブラリの例
├── README.md
├── xmake.lua
├── include/
│   └── umimmio.hh                # 単一ヘッダ → #include <umimmio.hh>
└── test/
    └── test_mmio.cc

lib/umibench/                     # 機能別ライブラリの例
├── README.md
├── xmake.lua
├── include/
│   ├── umibench.hh               # 単一ヘッダ（統合版）→ #include <umibench.hh>
│   └── umibench/                 # 機能別ヘッダ
│       ├── core/
│       │   ├── measure.hh        # → #include <umibench/core/measure.hh>
│       │   ├── runner.hh
│       │   └── stats.hh
│       ├── platform/
│       │   ├── host.hh
│       │   └── stm32f4.hh
│       ├── timer/
│       │   └── chrono.hh
│       └── output/
│           └── stdout.hh
├── test/
├── examples/
└── target/
```

**使い分け**:

```cpp
// 方法1: 単一ヘッダとして使う（シンプル）
#include <umibench.hh>

// 方法2: 必要な機能のみinclude（コンパイル時間短縮）
#include <umibench/core/runner.hh>
#include <umibench/platform/host.hh>
```

**ファイル名の規則**:

| ライブラリ名 | 単一ヘッダ | 機能別ディレクトリ | 例 |
|-------------|-----------|------------------|-----|
| `umimmio` | `umimmio.hh` | なし（単一機能） | `#include <umimmio.hh>` |
| `umibench` | `umibench.hh` | `umibench/core/*.hh` | `#include <umibench.hh>` or `<umibench/core/*.hh>` |
| `umidsp` | `umidsp.hh` | `umidsp/oscillator/*.hh` | `#include <umidsp.hh>` or `<umidsp/oscillator/sine.hh>` |
| `umicrypto` | `umicrypto.hh` | `umicrypto/detail/*.hh` | `#include <umicrypto.hh>` or `<umicrypto/ed25519.hh>` |

**重要**:
- 単一ヘッダは `include/<ライブラリ名>.hh`（`.hh` 拡張子付き）
- 機能別ヘッダは `include/<ライブラリ名>/**/*.hh`（サブディレクトリ）
- これにより、どちらのパターンも自然にサポート

### xmake.lua の設定例

```lua
-- ヘッダオンリー + 内部ヘッダ
target("umidsp")
    set_kind("headeronly")
    add_headerfiles("include/(umidsp/**.hh)")
    add_includedirs("include", { public = true })
    -- detail/ も公開パスに含まれるが、命名規約で「非公開」を示す

-- 実装ファイルあり
target("umicrypto")
    set_kind("static")
    add_files("src/*.cc")
    add_headerfiles("include/(umicrypto/**.hh)")
    add_includedirs("include", { public = true })
    add_includedirs("include/umicrypto/detail", { public = false })  -- 非公開
```

---

## 参考

- [LIBRARY_REFACTORING.md](LIBRARY_REFACTORING.md) — 元の統合計画
- [LIBRARY_STRUCTURE.md](LIBRARY_STRUCTURE.md) — ライブラリ構造規約
- Abseil, Folly, Zephyr の構造 — 大規模C++プロジェクトの標準的なパターン
