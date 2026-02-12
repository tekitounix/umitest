# ビルドシステム設計

**ステータス:** 設計中  **策定日:** 2026-02-10
**根拠:** [foundations/architecture.md](foundations/architecture.md), [board/architecture.md](board/architecture.md), xmake-repo/synthernet/README.md

**関連文書:**
- [integration.md](integration.md) — **ビルド・生成・ドライバの統合設計 (本文書と密結合)**
- [foundations/architecture.md](foundations/architecture.md) — パッケージ構成、依存関係
- [board/architecture.md](board/architecture.md) — ボード定義の二重構造 (Lua + C++)
- [board/project_structure.md](board/project_structure.md) — ユーザープロジェクト構成
- [codegen_pipeline.md](codegen_pipeline.md) — コード生成パイプラインとの連携

---

## 1. 設計目標

| # | 目標 | 制約 |
|---|------|------|
| 1 | **ワンコマンドビルド** | `xmake build <target>` だけで MCU 選択・クロスコンパイル・リンカ生成が完結する |
| 2 | **ターゲット透過性** | ARM 組込み / WASM / ホストの切り替えはターゲット名の変更のみ |
| 3 | **ボード選択だけで全決定** | `--board=stm32f4-disco` で MCU, ツールチェイン, リンカ, フラッシュ手順が自動解決 |
| 4 | **ライブラリ独立ビルド** | umihal, umimmio, umirtm 等は単独で `xmake test` 可能 |
| 5 | **生成物の透過的統合** | PAL コード生成結果が手書きコードと同一の扱いでビルドされる |

---

## 2. ターゲットプラットフォーム構成

### 2.1 プラットフォームマトリクス

| プラットフォーム | ツールチェイン | リンカ | フラッシュ | テスト実行 |
|----------------|--------------|--------|-----------|-----------|
| ARM Cortex-M (組込み) | arm-none-eabi-gcc | 生成 `.ld` | pyOCD / OpenOCD | GDB + RTT / Renode |
| WASM | emscripten (emcc) | emscripten 内蔵 | N/A | Node.js / ブラウザ |
| Host (macOS/Linux) | clang++ / g++ | システムデフォルト | N/A | 直接実行 |
| Renode (仮想実機) | arm-none-eabi-gcc | 生成 `.ld` | Renode ロード | Renode + Robot Framework |

### 2.2 ツールチェイン選択ロジック

```
board.lua の target フィールド
  → "arm-cortex-m"  → arm-none-eabi-gcc + 生成リンカスクリプト
  → "wasm"          → emscripten + WASM 固有フラグ
  → "host"          → システムコンパイラ (clang++ / g++)
  → "renode"        → arm-none-eabi-gcc + Renode .resc 生成
```

---

## 3. パッケージ依存関係グラフ

### 3.1 ライブラリ間依存

```
                 ┌─ umihal (0 deps) ─┐
                 │   Concepts のみ     │
                 └───────┬────────────┘
                         │ satisfies
                 ┌───────▼────────────┐
  umimmio ──────►│   umiport          │◄──── umidevice
  (0 deps)       │   MCU ドライバ実装  │      (Transport 経由)
                 └───────┬────────────┘
                         │ uses
                 ┌───────▼────────────┐
                 │   board/           │
                 │   platform.hh で   │
                 │   MCU × Device 結合 │
                 └───────┬────────────┘
                         │
              ┌──────────▼──────────┐
              │  application/test   │
              └─────────────────────┘

  独立ライブラリ (0 deps):
  umirtm, umibench, umitest
```

### 3.2 xmake パッケージ解決順序

xmake のビルドでは以下の順序でパッケージが解決される:

1. **umimmio** — レジスタ抽象化テンプレート (他に依存なし)
2. **umihal** — Concept 定義 (他に依存なし)
3. **umidevice** — 外部デバイスドライバ (umihal に依存)
4. **umiport** — MCU ドライバ (umihal, umimmio に依存)
5. **umirtm, umibench, umitest** — ユーティリティ (独立)
6. **アプリケーション** — 上記全てを統合

---

## 4. xmake ルール構成

### 4.1 ルール階層

```
xmake-repo/synthernet/packages/a/arm-embedded/
├── rules/
│   ├── board/                  # ボード選択・設定解決ルール
│   │   ├── resolve.lua         # board.lua 読み込み → MCU DB 参照 → 設定展開
│   │   └── inherit.lua         # extends チェーン解決
│   ├── toolchain/              # ツールチェイン設定ルール
│   │   ├── arm-cortex-m.lua    # GCC フラグ, FPU 設定, thumb モード
│   │   ├── wasm.lua            # emscripten 設定
│   │   └── host.lua            # ホストコンパイラ設定
│   ├── linker/                 # リンカスクリプト生成ルール
│   │   └── memory_ld.lua       # MCU DB → memory.ld テンプレート展開
│   ├── flash/                  # フラッシュ・デバッグルール
│   │   ├── pyocd.lua           # pyOCD フラッシュ
│   │   └── openocd.lua         # OpenOCD フラッシュ
│   └── vscode/                 # IDE 統合ルール
│       └── launch_generator.lua
└── plugins/
    └── dev-sync/               # 開発時パッケージ同期
```

### 4.2 ルール実行順序

ビルド時に以下の順序でルールが実行される:

```
1. board/resolve      board.lua を読み込み、extends チェーンを解決
       │
2. board/inherit      親ボードの設定をマージ
       │
3. MCU DB lookup      database/mcu/<family>/<device>.lua から MCU スペック取得
       │
4. toolchain/*        ターゲットに応じたツールチェイン設定
       │
5. linker/memory_ld   メモリマップからリンカスクリプト生成
       │
6. codegen (将来)     PAL コード生成 (svd → C++ ヘッダ)
       │
7. compile            通常のコンパイル・リンク
       │
8. flash/*            フラッシュ (xmake flash-* タスク時のみ)
```

### 4.3 ボード解決の詳細フロー

```lua
-- xmake.lua でのターゲット定義例
target("stm32f4_kernel")
    set_kind("binary")
    add_rules("umiport.board")         -- ボード統合ルール
    set_values("board", "stm32f4-disco")  -- ボード選択

-- ルールが行うこと:
-- 1. boards/stm32f4-disco/board.lua をロード
-- 2. extends チェーンを解決 (親ボードがあれば)
-- 3. board.lua の mcu フィールドから MCU DB を検索
-- 4. MCU DB → コンパイルフラグ (-mcpu, -mfpu, -mfloat-abi)
-- 5. MCU DB → リンカスクリプトのメモリ定義
-- 6. MCU DB → プリプロセッサ定義 (-DSTM32F407xx)
-- 7. ボードの include パス追加
```

---

## 5. クロスコンパイル構成

### 5.1 ARM Cortex-M

```lua
-- MCU DB から自動決定されるフラグ
toolchain("arm-embedded")
    set_sdkdir("/path/to/arm-none-eabi-gcc")
    set_toolset("cc", "arm-none-eabi-gcc")
    set_toolset("cxx", "arm-none-eabi-g++")
    set_toolset("ld", "arm-none-eabi-g++")

-- MCU 固有フラグ (stm32f407vg の場合)
-- -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
-- -DSTM32F407xx -DARM_MATH_CM4
-- -fno-exceptions -fno-rtti
-- -specs=nosys.specs -specs=nano.specs
```

### 5.2 WASM

```lua
toolchain("emscripten")
    set_sdkdir("$(EMSDK)/upstream/emscripten")
    -- -s WASM=1 -s ALLOW_MEMORY_GROWTH=1
    -- -fno-exceptions
```

### 5.3 ホスト

```lua
-- システムコンパイラをそのまま使用
-- -std=c++23 -Wall -Wextra
-- サニタイザ有効化 (デバッグ時):
-- -fsanitize=address,undefined
```

---

## 6. リンカスクリプト生成

### 6.1 テンプレート方式

MCU DB のメモリ定義からリンカスクリプトを自動生成する。

```
MCU DB (stm32f407vg.lua)          テンプレート (sections.ld.in)
┌──────────────────────┐          ┌──────────────────────────┐
│ memory = {           │          │ MEMORY {                 │
│   flash = {          │  ──────► │   FLASH (rx) : ORIGIN =  │
│     base = 0x08000000│          │     ${flash.base},       │
│     size = "1M"      │          │     LENGTH = ${flash.size}│
│   },                 │          │   RAM (rwx) : ORIGIN =   │
│   ram = {            │          │     ${ram.base},          │
│     base = 0x20000000│          │     LENGTH = ${ram.size}  │
│     size = "128K"    │          │   ...                    │
│   },                 │          │ }                        │
│   ccm = { ... }      │          │ SECTIONS { ... }         │
│ }                    │          └──────────────────────────┘
└──────────────────────┘
```

### 6.2 生成物の配置

```
build/.gens/<target>/
├── memory.ld              # MCU DB から生成されたリンカスクリプト
└── sections.ld            # テンプレートから展開されたセクション定義
```

---

## 7. synthernet パッケージとの連携

### 7.1 開発サイクル

xmake-repo/synthernet/ のルール・プラグインは `~/.xmake/` にインストールされる。
ソース編集後は明示的な同期が必要:

```bash
# 編集 → 同期 → キャッシュクリア → ビルド
vim xmake-repo/synthernet/packages/a/arm-embedded/rules/board/resolve.lua
xmake dev-sync                                    # ~/.xmake/ へコピー
rm -f build/.gens/rules/embedded.vscode.d          # 依存キャッシュクリア
xmake build <target>                               # ルール再実行
```

### 7.2 パッケージ構成

```
xmake-repo/synthernet/
├── packages/
│   ├── a/arm-embedded/       # ARM 組込みツールチェイン + ルール
│   ├── u/umi-core/           # UMI コアライブラリパッケージ
│   └── ...
├── xmake.lua                 # リポジトリ定義
└── README.md                 # アーキテクチャとトラブルシューティング
```

---

## 8. 未解決の設計課題

| # | 課題 | 選択肢 | 備考 |
|---|------|--------|------|
| 1 | PAL 生成物の配置 | ビルドディレクトリ vs ソースツリー | [codegen_pipeline.md](codegen_pipeline.md) で検討 |
| 2 | 複数ボード同時ビルド | xmake config 切り替え vs マルチターゲット | 現状は 1 config = 1 ボード |
| 3 | IDE 統合の自動化 | VSCode launch.json 自動生成の範囲 | vscode/launch_generator.lua で部分対応済み |
| 4 | WASM + Host の統合テスト | 同一テストコードの複数ターゲット実行 | CI パイプラインの設計が必要 |
| 5 | パッケージバージョニング | セマンティックバージョン vs コミットハッシュ | synthernet パッケージの更新戦略 |
