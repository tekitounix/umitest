# ビルド・生成・ドライバ統合設計

**ステータス:** 設計中  **策定日:** 2026-02-10

**本文書の位置づけ:**
ビルドシステム (xmake)、PAL コード生成、ドライバ (umiport) の3領域は密結合しており、
個別に設計すると必ず矛盾が生じる。本文書はこの3者の **統合ポイント** を一箇所で定義する。

各領域の詳細は以下を参照:
- [build_system.md](build_system.md) — xmake ルール構成、クロスコンパイル
- [codegen_pipeline.md](codegen_pipeline.md) — コード生成ツール、中間表現、差分更新
- [board/architecture.md](board/architecture.md) — ボード定義の二重構造 (Lua + C++)

---

## 1. 密結合の構造

3つの領域が循環的に依存している:

```
                    ┌──────────────────────┐
                    │  ビルドシステム        │
                    │  (xmake)              │
                    │                       │
                    │  board.lua を読み      │
                    │  MCU DB を引き         │
                    │  ツールチェイン決定     │
                    │  include パス設定      │
                    └───┬──────────┬────────┘
                        │          │
          「どの MCU か」│          │「include パスに
          を伝える      │          │  何を置くか」を決める
                        │          │
                        ▼          ▼
┌──────────────────────┐    ┌──────────────────────┐
│  コード生成           │    │  ドライバ (umiport)   │
│  (umipal-gen)        │    │                       │
│                      │    │  #include <umiport/   │
│  SVD + CMSIS         │───►│    pal/stm32f4/       │
│  → C++ ヘッダ生成     │    │    periph/gpio.hh>    │
│                      │    │                       │
│  生成物のパスが       │    │  include パスが        │
│  ドライバの #include  │    │  ビルドシステムの      │
│  と一致する必要       │    │  設定と一致する必要    │
└──────────────────────┘    └──────────────────────┘
```

**同時に決めなければならないこと:**
1. PAL 生成物のディレクトリ構造 (= ドライバの `#include` パス)
2. MCU DB のスキーマ (ビルドルール Lua とコード生成 Python の両方が消費)
3. ボード選択 → MCU 特定 → 生成物選択の解決チェーン
4. 生成タイミング (事前生成 vs ビルド中オンデマンド)

---

## 2. 統合ポイント #1: ディレクトリ構造とインクルードパス

### 2.1 確定: 3者が合意するパス規約

```
lib/umiport/include/umiport/
├── pal/                            ← 生成物ルート (コード生成の出力先)
│   ├── arm/cortex-m/               ← L1/L2: アーキテクチャ共通
│   │   ├── nvic.hh                 ← ドライバが #include する
│   │   └── ...
│   └── stm32f4/                    ← L3/L4: MCU ファミリ固有
│       ├── periph/gpio.hh          ← ドライバが #include する
│       ├── periph/rcc.hh
│       └── ...
├── mcu/stm32f4/                    ← 手書きドライバ (PAL を使用)
│   ├── gpio.hh                     ← #include <umiport/pal/stm32f4/periph/gpio.hh>
│   ├── uart.hh
│   └── ...
├── arm/cortex-m/                   ← 手書き: アーキテクチャ共通ドライバ
│   └── dwt.hh
└── board/stm32f4-disco/            ← 手書き: ボード定義
    ├── platform.hh                 ← #include <umiport/mcu/stm32f4/uart.hh>
    └── board.hh
```

### 2.2 合意事項

| 項目 | 決定 | 根拠 |
|------|------|------|
| 生成物のルート | `umiport/pal/` | ドライバ (`mcu/`) と明確に分離。PAL = 生成、mcu = 手書き |
| アーキテクチャ軸 | `pal/arm/cortex-m/` | PAL 4層モデル L1/L2 に対応 |
| MCU ファミリ軸 | `pal/stm32f4/` | PAL 4層モデル L3/L4 に対応 |
| ペリフェラルレジスタ | `pal/<family>/periph/` | ファイル数が多いためサブディレクトリ化 |
| ドライバの #include | `<umiport/pal/stm32f4/periph/gpio.hh>` | フルパスで MCU 依存を明示 |

### 2.3 xmake 側の include パス設定

```lua
-- rules/board/resolve.lua (on_config 内)
-- MCU DB の family フィールドからインクルードパスを追加
local family = mcu_config.family  -- "stm32f4"
target:add("includedirs", path.join(umiport_dir, "include"))
-- → <umiport/pal/stm32f4/...> が解決可能になる
-- → <umiport/mcu/stm32f4/...> も同一パスで解決
```

**重要:** include パスは `umiport/include` の1本のみ。
PAL 生成物とドライバが同じ include ルートを共有し、
`pal/` と `mcu/` のプレフィックスで生成物/手書きを区別する。

---

## 3. 統合ポイント #2: MCU DB スキーマ

### 3.1 2つの消費者

MCU DB は以下の2つから消費される:

| 消費者 | 言語 | 読み方 | 使う情報 |
|--------|------|--------|---------|
| xmake ルール (board/resolve) | Lua | `dofile()` | core, memory, vendor, toolchain, flash_tool |
| コード生成 (umipal-gen) | Python | パーサーで読み込み | family, device_name, memory (リンカ生成時) |

### 3.2 共通スキーマ定義

MCU DB の Lua ファイルが返すテーブルの必須フィールド:

```lua
-- database/mcu/stm32f4/stm32f407vg.lua が返すテーブルの型定義
{
    -- === ビルドシステムが使うフィールド ===
    core            = "cortex-m4f",      -- xmake: ツールチェイン選択
    vendor          = "st",              -- xmake: ベンダー固有ルール
    device_name     = "STM32F407VG",     -- xmake: -D プリプロセッサ定義
    flash_tool      = "pyocd",           -- xmake: フラッシュ方法
    openocd_target  = "stm32f4x",       -- xmake: OpenOCD ターゲット
    src_dir         = "stm32f4",         -- xmake: startup.cc の検索ディレクトリ

    -- === ビルドシステム + コード生成 の両方が使うフィールド ===
    family          = "stm32f4",         -- 両方: PAL ヘッダのディレクトリ名
    memory = {                           -- 両方: リンカスクリプト + メモリマップ生成
        FLASH = { attr = "rx",  origin = 0x08000000, length = "1M" },
        SRAM  = { attr = "rwx", origin = 0x20000000, length = "192K" },
        CCM   = { attr = "rwx", origin = 0x10000000, length = "64K" },
    },

    -- === コード生成が使うフィールド ===
    svd_file        = "STM32F407.svd",   -- codegen: SVD パーサーの入力
    pin_package     = "LQFP100",         -- codegen: GPIO AF テーブルのフィルタ
}
```

### 3.3 スキーマの一貫性保証

```
MCU DB (Lua)
    │
    ├──► xmake ルール (Lua): dofile() で直接ロード
    │    → family フィールドで PAL ヘッダの include パスを構成
    │    → memory フィールドで memory.ld を生成
    │
    └──► umipal-gen (Python): Lua テーブルをパースして読み込み
         → family フィールドで生成先ディレクトリを決定
         → svd_file フィールドで SVD パーサーの入力を特定
         → memory フィールドで memory_map.hh の constexpr を生成

「family」フィールドが統合のキー:
  xmake:    include/umiport/pal/{family}/
  codegen:  生成先 → include/umiport/pal/{family}/
  driver:   #include <umiport/pal/{family}/periph/gpio.hh>
```

### 3.4 Python から Lua テーブルを読む方式

| 方式 | 利点 | 欠点 | 評価 |
|------|------|------|------|
| **lupa** (Python-Lua ブリッジ) | `dofile()` 完全互換、継承チェーン解決可能 | 依存追加 | **推奨** |
| Lua → JSON 変換スクリプト | Python 側は JSON パースのみ | 別途変換ステップ必要、継承未解決 | フォールバック |
| MCU DB を JSON/YAML で書き直す | Python ネイティブ | xmake との二重管理発生 | **不採用** |

---

## 4. 統合ポイント #3: ボード → MCU → PAL 解決チェーン

### 4.1 解決フロー全体

```
ユーザーが指定:
  set_values("umiport.board", "stm32f4-disco")

        │
        ▼
Step 1: board.lua ロード
        boards/stm32f4-disco/board.lua
        → mcu = "stm32f407vg"
        → extends チェーン解決

        │
        ▼
Step 2: MCU DB ルックアップ
        database/mcu/stm32f4/stm32f407vg.lua
        → family = "stm32f4"
        → core = "cortex-m4f"
        → memory = { FLASH = {...}, SRAM = {...} }

        │
        ▼
Step 3: ビルド設定解決 (xmake on_load/on_config)
        → ツールチェイン: arm-none-eabi-gcc
        → コンパイルフラグ: -mcpu=cortex-m4 -mfpu=fpv4-sp-d16
        → include パス: lib/umiport/include
        → リンカスクリプト: build/.gens/.../memory.ld

        │
        ▼
Step 4: PAL ヘッダ解決 (コンパイル時)
        ドライバが #include <umiport/pal/stm32f4/periph/gpio.hh>
        → include パスから解決
        → 生成済みヘッダがソースツリーに存在

        │
        ▼
Step 5: Concept 検証 (コンパイル時)
        platform.hh の static_assert(umi::hal::Platform<Platform>)
        → ドライバが HAL Concept を満たすことをコンパイル時検証
```

### 4.2 各ステップの責務所在

| Step | 実行タイミング | 実行者 | 入力 | 出力 |
|------|-------------|--------|------|------|
| 1 | xmake on_load | board/resolve.lua | ボード名 | board テーブル |
| 2 | xmake on_load | board/resolve.lua | MCU 名 | MCU スペック |
| 3 | xmake on_config | board/resolve.lua + embedded ルール | MCU スペック | コンパイル設定 |
| 4 | コンパイル | コンパイラ | include パス | ヘッダ解決 |
| 5 | コンパイル | コンパイラ | static_assert | 型検証 |

### 4.3 PAL コード生成はどこに入るか

```
通常ビルド: Step 1 → 2 → 3 → 4 → 5
             PAL ヘッダは事前生成済み。Python 不要。

PAL 更新時: xmake pal-gen → Step 4 の入力を更新 → git commit
             ビルドとは独立したタスクとして実行。
```

**設計判断:** コード生成はビルドパイプラインの外に置く。
理由:
- ビルドのたびに Python を実行するのは遅い
- 生成物が変わらない限り再生成は不要 (SVD は不変)
- git diff でレビュー可能であるべき
- CI では生成物のコンパイルのみ行い、生成自体は行わない

---

## 5. 統合ポイント #4: 生成タイミングと整合性

### 5.1 整合性の保証

PAL 生成物とドライバの `#include` が一致しなければコンパイルエラーになるため、
整合性は **コンパイル時に自動検出** される。追加の検証メカニズムは不要。

```
整合性チェックの自然な仕組み:

1. コード生成が pal/stm32f4/periph/gpio.hh を生成
2. ドライバが #include <umiport/pal/stm32f4/periph/gpio.hh> する
3. → ファイルがなければコンパイルエラー (不整合検出)
4. → ファイルの型定義がドライバの期待と異なればコンパイルエラー
5. → static_assert が concept 充足を検証
```

### 5.2 ありえる不整合シナリオと対策

| シナリオ | 症状 | 対策 |
|---------|------|------|
| PAL 未生成でビルド | `#include` が見つからない | エラーメッセージで `xmake pal-gen` を案内 |
| MCU DB 更新後に PAL 未再生成 | memory_map.hh と board.hh の定数が不一致 | CI で `xmake pal-gen --check` (生成物が最新か検証) |
| SVD 更新でレジスタ定義変更 | ドライバのフィールドアクセスがコンパイルエラー | ドライバ側を修正 (手書きコード) |
| family 名の不一致 | include パス解決失敗 | MCU DB の `family` を唯一の情報源とし、全箇所で参照 |

### 5.3 CI での整合性検証

```bash
# CI パイプライン
# Step 1: PAL 生成物が最新か検証 (生成はしない)
xmake pal-gen --check
# → 差分があれば CI 失敗: "PAL headers are out of date. Run: xmake pal-gen"

# Step 2: 通常ビルド (生成済みヘッダを使用)
xmake build stm32f4_kernel

# Step 3: テスト
xmake test
```

---

## 6. データフロー全体図

```
┌─────────────────────────────────────────────────────────────────┐
│                        開発者の操作                               │
│                                                                  │
│  (A) 新 MCU 追加:                                                │
│      database/mcu/ に .lua 追加 → xmake pal-gen → git commit    │
│                                                                  │
│  (B) ボード追加:                                                 │
│      boards/<name>/ に board.lua + platform.hh 追加              │
│                                                                  │
│  (C) 通常開発:                                                   │
│      xmake build <target> → flash → debug                       │
└──────────────────┬──────────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────────┐
│                     データフロー                                  │
│                                                                  │
│  ┌─────────┐     ┌──────────┐     ┌──────────────────┐         │
│  │ SVD     │     │ MCU DB   │     │ board.lua        │         │
│  │ CMSIS   │     │ (Lua)    │     │                  │         │
│  │ PinData │     │          │     │ mcu = "stm32f407"│         │
│  └────┬────┘     └────┬─────┘     └────┬─────────────┘         │
│       │               │                │                        │
│       ▼               │                ▼                        │
│  ┌─────────┐          │          ┌──────────┐                   │
│  │umipal-  │          │          │ xmake    │                   │
│  │gen      │◄─────────┘          │ resolve  │◄──────────┐      │
│  │(Python) │  family, svd_file   │ (Lua)    │  family   │      │
│  └────┬────┘                     └────┬─────┘           │      │
│       │                               │                 │      │
│       ▼                               ▼                 │      │
│  ┌─────────────────────┐    ┌──────────────────┐        │      │
│  │ pal/stm32f4/        │    │ コンパイル設定     │        │      │
│  │   periph/gpio.hh    │    │ -mcpu, -mfpu     │        │      │
│  │   periph/rcc.hh     │    │ -DSTM32F407xx    │        │      │
│  │   memory_map.hh     │    │ include パス      │────────┘      │
│  │   vectors.hh        │    │ memory.ld 生成    │               │
│  └────────┬────────────┘    └────────┬─────────┘               │
│           │                          │                          │
│           │  #include                │  コンパイルフラグ          │
│           ▼                          ▼                          │
│      ┌─────────────────────────────────────┐                    │
│      │  ドライバ (umiport/mcu/stm32f4/)    │                    │
│      │  → PAL ヘッダを #include            │                    │
│      │  → HAL Concept を satisfy           │                    │
│      │  → platform.hh で統合               │                    │
│      └─────────────────────────────────────┘                    │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 7. 命名規約の統一

3領域で同じ概念を異なる名前で呼ぶと混乱するため、統一する。

| 概念 | MCU DB フィールド | ディレクトリ名 | xmake 変数 | コード生成引数 |
|------|-----------------|--------------|------------|--------------|
| MCU ファミリ | `family` | `pal/{family}/` | — | `--family` |
| MCU デバイス | `device_name` | — | — | `--device` |
| CPU コア | `core` | `pal/arm/cortex-m/` | `embedded.core` | — |
| ボード名 | — | `board/{name}/` | `umiport.board` | — |

**`family` が統合のキー。** ディレクトリ名、include パス、生成先はすべて `family` から導出される。

---

## 8. 未解決の統合課題

| # | 課題 | 影響範囲 | 備考 |
|---|------|---------|------|
| 1 | MCU DB を Python から読む方式の確定 | codegen ↔ MCU DB | lupa vs JSON 変換。Phase 3 で決定 |
| 2 | `xmake pal-gen --check` の実装 | CI | 生成物のハッシュ比較。生成ツール依存を CI に入れるか |
| 3 | PAL バリアント (同一ファミリの複数デバイス) | codegen ↔ ディレクトリ | `pal/stm32f4/` は全バリアント共通？バリアント別？ |
| 4 | 非 ARM の PAL ディレクトリ構造 | 全体 | `pal/riscv/` vs `pal/esp32/` — アーキテクチャ軸 vs ベンダー軸 |
| 5 | PAL 生成物のライセンスヘッダ | codegen | SVD ベンダーライセンスの継承要否 |
