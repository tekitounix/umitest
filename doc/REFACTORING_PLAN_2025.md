# UMI プロジェクト フォルダ構成リファクタリング計画

## umiosアーキテクチャの整理

### 用語定義

| 用語 | 説明 |
|------|------|
| **umios** | OSインターフェース定義（API抽象層） |
| **umios backend** | RTOSの実装（umios-cm, FreeRTOS, POSIX等） |
| **umios kernel** | umios + backend + HW固有実装 = 動作するカーネル |

### バックエンド一覧

| バックエンド | ターゲット | 状態 | 現在の場所 |
|-------------|-----------|------|-----------|
| **umios-cm** | Cortex-M (独自RTOS) | 実装中 | `port/arm/cortex-m/` + `lib/umios/` |
| **umios-wasm** | Web (シミュレーション) | 実装中 | `lib/umim/web_sim.hh` 等 |
| **FreeRTOS** | ESP32, 汎用 | 計画 | - |
| **POSIX** | Linux/macOS (テスト) | 部分実装 | `port/board/stub/` |

---

## 現状分析

### 現在のディレクトリ構造

```
umi_os/
├── lib/
│   ├── umios/          # インターフェース + カーネル実装 (混在)
│   ├── umim/           # WASM adapter + WebHAL + JS (混在)
│   ├── umidi/          # MIDIライブラリ (独立パッケージ)
│   ├── umidsp/         # DSPコンポーネント (スケルトン)
│   ├── umiboot/        # ブートローダー (スケルトン)
│   ├── umigui/         # GUI描画バックエンド
│   └── umiui/          # UI状態管理 (2ファイル)
├── port/
│   ├── arm/cortex-m/   # Cortex-Mレジスタ/プリミティブ
│   ├── board/          # ボード固有HAL (stm32f4, stub)
│   └── vendor/         # ベンダーペリフェラル (STM32)
├── examples/
├── test/
├── doc/
├── renode/
└── xmake.lua
```

### 問題点

| # | 問題 | 詳細 |
|---|------|------|
| 1 | **umiosの役割混在** | インターフェース定義とカーネル実装が同一ディレクトリ |
| 2 | **umios-cmの所在不明** | Cortex-Mバックエンドが`port/arm/cortex-m/`と`lib/umios/`に分散 |
| 3 | **umios-wasmの所在不明** | Webバックエンドが`lib/umim/`に混在 |
| 4 | **umimの役割過多** | Adapter、WebHAL、JS workletが混在 |
| 5 | **ビルド成果物の混在** | examples/に.js, .wasmが混入 |
| 6 | **ドキュメントと実装の乖離** | README記載の理想構造と実態が不一致 |

---

## 提案: 新しいディレクトリ構造

### 案A: lib/内でバックエンド分離

```
umi/
├── lib/
│   ├── umios/                  # OSインターフェース定義（API）
│   │   ├── types.hh            # 基本型
│   │   ├── audio_context.hh    # AudioContext
│   │   ├── event.hh            # Event
│   │   ├── processor.hh        # Processor concepts
│   │   ├── error.hh            # エラー型
│   │   └── ...
│   │
│   ├── umios-kernel/           # カーネル共通実装（バックエンド非依存）
│   │   ├── umi_kernel.hh       # スケジューラ、タスク管理
│   │   ├── umi_audio.hh        # オーディオサブシステム
│   │   ├── umi_midi.hh         # MIDIサブシステム
│   │   ├── umi_monitor.hh      # モニタリング
│   │   └── umi_shell.hh        # デバッグシェル
│   │
│   ├── umios-cm/               # Cortex-Mバックエンド
│   │   ├── cortex_m4.hh        # M4プリミティブ
│   │   └── common/             # SCB, NVIC, SysTick, DWT
│   │
│   ├── umios-wasm/             # WASMバックエンド（Webシミュレーション）
│   │   ├── web_sim.hh
│   │   ├── web_hal.hh
│   │   └── *.js                # JavaScript worklet
│   │
│   ├── umim/                   # UMIM Adapter層のみ
│   │   ├── umim_adapter.hh
│   │   ├── embedded_adapter.hh
│   │   └── web_adapter.hh
│   │
│   ├── umidi/                  # MIDIライブラリ（独立）
│   ├── umidsp/                 # DSPコンポーネント
│   ├── umigui/                 # GUI/UI
│   └── umiboot/                # ブートローダー
│
├── port/                       # HW固有実装（ボード依存）
│   ├── board/stm32f4/          # STM32F4向けHW実装
│   ├── board/stub/             # テスト用スタブ
│   └── vendor/stm32/           # STM32ペリフェラル
│
├── examples/
├── test/
├── doc/
└── renode/
```

**メリット**:
- バックエンドが`lib/umios-*`として明示的に分離
- `umim`はAdapter専用になり役割明確
- `port/`はHW依存のみ（ボード固有HAL）

**デメリット**:
- 既存の`lib/umios/`を大幅に分割する必要あり
- `port/arm/cortex-m/`を`lib/umios-cm/`に移動

---

### 案B: port/内にバックエンド配置

```
umi/
├── lib/
│   ├── umios/                  # インターフェース + カーネル共通
│   │   ├── api/                # インターフェース定義
│   │   │   ├── types.hh
│   │   │   ├── audio_context.hh
│   │   │   └── ...
│   │   └── kernel/             # カーネル共通実装
│   │       ├── umi_kernel.hh
│   │       └── ...
│   │
│   ├── umim/                   # Adapter層
│   ├── umidi/
│   ├── umidsp/
│   └── umigui/
│
├── port/
│   ├── umios-cm/               # Cortex-Mバックエンド
│   │   ├── cortex_m4.hh
│   │   └── common/
│   │
│   ├── umios-wasm/             # WASMバックエンド
│   │   ├── web_sim.hh
│   │   └── *.js
│   │
│   ├── umios-freertos/         # FreeRTOSバックエンド（将来）
│   │
│   ├── board/                  # ボード固有HAL
│   │   ├── stm32f4/
│   │   └── stub/
│   │
│   └── vendor/                 # ベンダーペリフェラル
│       └── stm32/
│
├── examples/
├── test/
├── doc/
└── renode/
```

**メリット**:
- `port/`が「プラットフォーム依存すべて」を包含
- `lib/`は純粋にポータブルなコード
- `lib/umios/`の変更が最小限（サブディレクトリ化のみ）

**デメリット**:
- umios-wasmは「port」というより「lib」寄り？

---

### 案C: 最小変更（現状ベース整理）

```
umi/
├── lib/
│   ├── umios/                  # 現状維持 + サブディレクトリ化
│   │   ├── api/                # インターフェース
│   │   ├── kernel/             # カーネル実装
│   │   └── util/               # ユーティリティ（log, assert, coro）
│   │
│   ├── umim/                   # Adapter + Web（役割明記）
│   │   ├── adapter/            # プラットフォームアダプタ
│   │   └── web/                # umios-wasm相当
│   │
│   ├── umidi/
│   ├── umidsp/
│   └── umigui/
│
├── port/
│   ├── umios-cm/               # 現port/arm/cortex-m/をリネーム
│   ├── board/
│   └── vendor/
│
├── examples/
├── test/
├── doc/
└── renode/
```

**メリット**:
- 変更量が最小
- 既存のincludeパスへの影響が少ない

**デメリット**:
- `lib/umim/web/`が実質umios-wasmで命名が不明確

---

## 推奨: 案A

理由：
1. **明示性**: バックエンドが`umios-cm`, `umios-wasm`として明確に見える
2. **一貫性**: 将来の`umios-freertos`も`lib/umios-freertos/`として追加可能
3. **分離**: インターフェース(umios)、カーネル(umios-kernel)、バックエンドが明確に分離

---

## 詳細リファクタリング計画

### Phase 0: 即座に対応すべき問題（破壊的変更なし）

#### 1.1 ビルド成果物の除外

**目的**: ソースコードとビルド成果物を分離

```bash
# .gitignoreに追加
examples/**/*.wasm
examples/**/*.js
!examples/**/README.js  # もしあれば除外しない
```

**対象ファイル**:
- `examples/synth/*.js`, `*.wasm`
- `examples/workbench/*.js`, `*.wasm`

#### 1.2 README.mdの参照修正

**目的**: 存在しないパスへの参照を削除

```diff
- - [archive/v0.1-legacy/doc/NEW_DESIGN.md](archive/v0.1-legacy/doc/NEW_DESIGN.md) - 詳細設計仕様書
+ <!-- アーカイブは削除されました -->
```

#### 1.3 doc/archive/の整理

**目的**: 古いリファクタリング計画の整理

- `doc/archive/REFACTORING_PLAN.md` の内容を確認
- 完了済みなら削除、進行中なら本ドキュメントにマージ

---

### Phase 1: umios構造の分離（コア変更）

#### 1.1 lib/umios/ の分割

**現状**: インターフェース定義とカーネル実装が混在

**変更後**:
```
lib/
├── umios/                  # インターフェース定義のみ
│   ├── types.hh
│   ├── audio_context.hh
│   ├── event.hh
│   ├── processor.hh
│   ├── error.hh
│   ├── time.hh
│   ├── triple_buffer.hh
│   └── ui/
│       ├── ui_controller.hh
│       ├── ui_map.hh
│       └── ui_view.hh
│
├── umios-kernel/           # カーネル実装
│   ├── umi_kernel.hh
│   ├── umi_audio.hh
│   ├── umi_midi.hh
│   ├── umi_monitor.hh
│   ├── umi_shell.hh
│   ├── umi_startup.hh
│   ├── coro.hh
│   ├── assert.hh
│   └── log.hh
```

**移動マッピング**:
| 現在の場所 | 移動先 | 理由 |
|-----------|--------|------|
| `lib/umios/types.hh` | `lib/umios/` (維持) | インターフェース |
| `lib/umios/audio_context.hh` | `lib/umios/` (維持) | インターフェース |
| `lib/umios/umi_kernel.hh` | `lib/umios-kernel/` | カーネル実装 |
| `lib/umios/umi_audio.hh` | `lib/umios-kernel/` | カーネル実装 |
| `lib/umios/coro.hh` | `lib/umios-kernel/` | ランタイム |

#### 1.2 umios-cm の作成

**現状**: `port/arm/cortex-m/`

**変更後**: `lib/umios-cm/` に移動

```
lib/umios-cm/
├── cortex_m4.hh
└── common/
    ├── scb.hh
    ├── nvic.hh
    ├── systick.hh
    ├── dwt.hh
    └── vector_table.hh
```

**理由**: Cortex-Mバックエンドはumiosの一部として明示化

#### 1.3 umios-wasm の作成

**現状**: `lib/umim/` 内に混在

**変更後**: `lib/umios-wasm/` として分離

```
lib/umios-wasm/
├── web_sim.hh          # ← lib/umim/web_sim.hh
├── web_hal.hh          # ← lib/umim/web_hal.hh
├── web_sim.js          # ← lib/umim/web_sim.js
└── web_sim_worklet.js  # ← lib/umim/web_sim_worklet.js
```

---

### Phase 2: Adapter/ライブラリ構造の整理（中規模変更）

#### 2.1 lib/umim/ の整理（Adapter専用に）

現状の `lib/umim/` は以下が混在:
- Adapter層（`umim_adapter.hh`, `embedded_adapter.hh`）
- WebHAL（`web_hal.hh`, `web_sim.hh`）
- JavaScript worklet（`*.js`）

**提案A: 機能別サブディレクトリ化**
```
lib/umim/
├── adapter/           # プラットフォームアダプタ
│   ├── umim_adapter.hh
│   ├── embedded_adapter.hh
│   └── web_adapter.hh
├── hal/               # Hardware Abstraction Layer
│   ├── web_hal.hh
│   ├── web_sim.hh
│   └── renode_hal.hh
├── web/               # Web/JavaScript関連
│   ├── umi.js
│   ├── umi-worklet.js
│   └── worklet-processor.js
└── README.md
```

**提案B: トップレベルに分離**
```
lib/
├── adapter/           # 全アダプタをここに集約
├── hal/               # HAL層
└── umim/              # UMIM仕様のみ残す
```

**推奨**: 提案A（後方互換性維持しつつ整理）

#### 2.2 umiguiとumiuiの統合検討

現状:
- `lib/umigui/` - 描画バックエンド (7ファイル)
- `lib/umiui/` - UI状態 (2ファイル)

**選択肢**:

| 案 | 構造 | メリット | デメリット |
|----|------|----------|------------|
| A | `lib/ui/` に統合 | シンプル、ドキュメントと一致 | 既存参照の更新必要 |
| B | `lib/umigui/` に `umiui/` を統合 | 変更最小 | `ui/`がなくなる |
| C | 現状維持 | 変更なし | 整合性なし |

**推奨**: 案B（umigui内にuiサブディレクトリ作成）

#### 2.3 スケルトンライブラリの扱い

以下は実質的に空またはスケルトン:
- `lib/umidsp/` - include/dspのみ
- `lib/umiboot/` - include/umibootのみ

**選択肢**:
1. 削除して将来必要時に再作成
2. 明示的に「予約済み」としてドキュメント化
3. 現状維持

**推奨**: 選択肢2（README.mdに「将来実装予定」と明記）

---

### Phase 3: ドキュメント構造の統一（長期）

#### 3.1 ドキュメント配置方針の確定

現在のLIBRARY_PACKAGING.mdに従い:

| レベル | 配置場所 | 内容 |
|--------|----------|------|
| プロジェクト | `doc/` | アーキテクチャ、仕様書、全体設計 |
| ライブラリ | `lib/*/docs/` | APIドキュメント、Sphinx/Doxygen |

**課題**: `lib/umidi/docs/` は充実しているが、他ライブラリは空

**対応**:
- 空の `docs/` ディレクトリは削除
- 必要になった時点で作成

#### 3.2 README.md/ARCHITECTURE.mdの更新

理想構造と実装を一致させる:

```diff
- lib/
- ├── core/               # 基本型、AudioContext、Event
- ├── dsp/                # DSPコンポーネント
- └── adapter/            # プラットフォームアダプタ
+ lib/
+ ├── umios/              # OS/カーネル層（基本型含む）
+ ├── umim/               # UMIM adapter + WebHAL
+ ├── umidi/              # MIDIライブラリ
+ ├── umidsp/             # DSPコンポーネント（計画中）
+ ├── umigui/             # GUI描画 + UI状態
+ └── umiboot/            # ブートローダー（計画中）
```

---

### Phase 4: テスト構造の整理（オプション）

#### 4.1 現状

```
test/                     # プロジェクトレベル（統合テスト）
lib/umidi/test/           # ライブラリ単体テスト
lib/umidsp/test/          # (空に近い)
lib/umiboot/test/         # (空に近い)
```

#### 4.2 方針

| テスト種別 | 配置 | 命名規則 |
|------------|------|----------|
| 単体テスト | `lib/*/test/` | `test_*.cc` |
| 統合テスト | `test/` | `test_*.cc` |
| ベンチマーク | `test/` | `bench_*.cc` |
| Renodeテスト | `test/` または `renode/` | `*_renode.cc` |

現状は概ねこの方針に沿っているため、大きな変更は不要。

---

## 実施優先度

| 優先度 | Phase | 作業 | 工数 |
|--------|-------|------|------|
| **高** | 0 | ビルド成果物の.gitignore追加 | 小 |
| **高** | 0 | README.mdの参照修正 | 小 |
| **高** | 1.1 | lib/umios/ → umios + umios-kernel分割 | 大 |
| **高** | 1.2 | port/arm/cortex-m/ → lib/umios-cm/ 移動 | 中 |
| **高** | 1.3 | lib/umim/からumios-wasm分離 | 中 |
| 中 | 2.1 | lib/umim/の整理（Adapter専用化） | 小 |
| 中 | 3.2 | ドキュメント更新 | 中 |
| 低 | 2.2 | umigui/umiui統合 | 小 |
| 低 | 2.3 | スケルトンライブラリ整理 | 小 |

---

## 推奨される最終構造（案A採用時）

```
umi/
├── lib/
│   ├── umios/              # OSインターフェース定義（API）
│   │   ├── types.hh
│   │   ├── audio_context.hh
│   │   ├── event.hh
│   │   ├── processor.hh
│   │   ├── error.hh
│   │   ├── time.hh
│   │   ├── triple_buffer.hh
│   │   └── ui/
│   │       ├── ui_controller.hh
│   │       ├── ui_map.hh
│   │       └── ui_view.hh
│   │
│   ├── umios-kernel/       # カーネル共通実装
│   │   ├── umi_kernel.hh
│   │   ├── umi_audio.hh
│   │   ├── umi_midi.hh
│   │   ├── umi_monitor.hh
│   │   ├── umi_shell.hh
│   │   ├── umi_startup.hh
│   │   ├── coro.hh
│   │   ├── assert.hh
│   │   └── log.hh
│   │
│   ├── umios-cm/           # Cortex-Mバックエンド
│   │   ├── cortex_m4.hh
│   │   └── common/
│   │       ├── scb.hh
│   │       ├── nvic.hh
│   │       ├── systick.hh
│   │       ├── dwt.hh
│   │       └── vector_table.hh
│   │
│   ├── umios-wasm/         # WASMバックエンド（Webシミュレーション）
│   │   ├── web_sim.hh
│   │   ├── web_hal.hh
│   │   ├── web_sim.js
│   │   └── web_sim_worklet.js
│   │
│   ├── umim/               # UMIM Adapter層
│   │   ├── umim_adapter.hh
│   │   ├── embedded_adapter.hh
│   │   ├── web_adapter.hh
│   │   ├── umi.js
│   │   └── umi-worklet.js
│   │
│   ├── umidi/              # MIDIライブラリ（独立パッケージ）
│   │   ├── include/umidi/
│   │   ├── test/
│   │   ├── examples/
│   │   ├── docs/
│   │   └── xmake.lua
│   │
│   ├── umidsp/             # DSPコンポーネント
│   ├── umigui/             # GUI/UI
│   └── umiboot/            # ブートローダー
│
├── port/                   # HW固有実装（ボード依存のみ）
│   ├── board/
│   │   ├── stm32f4/        # STM32F4 HAL実装
│   │   └── stub/           # テスト用スタブ
│   └── vendor/
│       └── stm32/          # STM32ペリフェラル
│
├── examples/
│   ├── synth/              # シンセサンプル（ソースのみ）
│   ├── workbench/          # 開発ワークベンチ（ソースのみ）
│   └── embedded/           # 組み込みサンプル
│
├── test/                   # 統合テスト、ベンチマーク
├── renode/                 # Renodeシミュレーション環境
├── doc/                    # プロジェクトドキュメント
├── .build/                 # ビルド成果物（gitignore）
└── xmake.lua
```

---

## 次のアクション

1. [ ] Phase 0: .gitignoreにビルド成果物パターン追加
2. [ ] Phase 0: README.mdの無効な参照を修正
3. [ ] **決定**: 案A/B/Cのいずれを採用するか
4. [ ] Phase 1: umios構造の分離（採用案に基づき実施）
5. [ ] ドキュメント更新（ARCHITECTURE.md, README.md）
