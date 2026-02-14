# CAT_G: ライブラリドキュメント — 統合内容要約 v2.0

**カテゴリ:** G. ライブラリドキュメント
**配置先:** `lib/<libname>/docs/`（12ライブラリ、LIBRARY_SPEC v1.3.0 準拠）
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. カテゴリ概要

### 1.1 クリーンスレートによる根本的変更

IMPLEMENTATION_PLAN v1.1.0 のクリーンスレート戦略により、本カテゴリは v1.0 から**根本的に変更**される:

| 観点 | v1.0 (旧) | v2.0 (新) |
|------|-----------|-----------|
| 対象ライブラリ | 公開5ライブラリ + 内部7ライブラリ (lib/umi/*) | **12スタンドアロンライブラリ** (lib/<libname>/) |
| ドキュメント規格 | 公開のみ LIBRARY_SPEC 準拠、内部は自由形式 | **全ライブラリ UMI Strict Profile 準拠** |
| 構築方式 | 既存ドキュメントの保持・統合 | **Phase 0 でアーカイブ → Phase 1-4 で新規構築** |
| lib/umi/*/docs/ | 保持 | **lib/_archive/umi/*/docs/ にアーカイブ → 参照元として利用** |

### 1.2 UMI Strict Profile ドキュメント要件

LIBRARY_SPEC v1.3.0 §8.1 により、全12ライブラリに以下が必須:

```
lib/<libname>/
├── README.md           # リリースステータス、Why、Quick Start、公開ヘッダ、Documentation
├── Doxyfile            # Doxygen 生成設定
└── docs/
    ├── DESIGN.md       # 11セクション設計文書
    ├── INDEX.md        # API リファレンスマップ
    ├── TESTING.md      # テスト戦略・品質ゲート
    ├── examples/       # 最低1つの最小サンプル
    └── ja/             # 日本語版（ミラー）
        ├── README.md
        ├── DESIGN.md
        └── ...
```

---

## 2. 12ライブラリのドキュメント計画

### L0: Infrastructure (開発支援ツール)

#### 2.1 umitest — テストフレームワーク（IMPL Phase 1）

| 現行資産 | 新規作成 | 方針 |
|---------|---------|------|
| lib/umitest/README.md (★) | — | コピー＋修正 |
| lib/umitest/docs/DESIGN.md (★) | — | コピー＋修正 |
| lib/umitest/docs/ja/README.md (★) | — | コピー＋修正 |
| — | docs/INDEX.md | **新規作成** (API リファレンスマップ) |
| — | docs/TESTING.md | **新規作成** (テスト戦略) |
| — | compile_fail/ テスト | **新規追加** (Concept 定義ライブラリのため: §8.2) |

#### 2.2 umibench — ベンチマークフレームワーク（IMPL Phase 1）

| 現行資産 | 新規作成 | 方針 |
|---------|---------|------|
| lib/umibench/README.md (★★★★★) | — | コピー＋修正 |
| lib/umibench/docs/DESIGN.md (★★★★★) | — | コピー＋修正 |
| lib/umibench/docs/ja/README.md (★★★★★) | — | コピー＋修正 |
| — | docs/INDEX.md | **新規作成** |
| — | docs/TESTING.md | **新規作成** |

**ゴールドリファレンス**: umibench は既に最高品質。他ライブラリのドキュメントはこれを模範とする。

#### 2.3 umirtm — リアルタイムモニタ（IMPL Phase 1）

umibench と同パターン（コピー + INDEX.md, TESTING.md 新規追加）。

### L1: Foundation (型・概念・基盤)

#### 2.4 umicore — コア型・概念定義（IMPL Phase 1）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/umi/core/ (2,111行) | 全ファイル | **全新規作成** |
| lib/_archive/umi/shell/ (516行) | — | 参照のみ |

**DESIGN.md の主要内容:**
- AudioContext, ProcessorLike concept, Event/EventQueue の設計判断
- irq.hh と PAL の関係（2層構造: backend-agnostic インターフェース + MCU 固有実装）
- 名前空間: `umi::core`, `umi` (トップレベル Concept)

#### 2.5 umihal — HAL Concept 定義（IMPL Phase 1）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/standalone/umihal/ (81行, stub) | 全ファイル | **全新規作成** |
| lib/_archive/umi/port/ (Concept 抽出) | — | 参照のみ |

**DESIGN.md の主要内容:**
- Concept 定義のみ（実装なし）の設計思想
- GpioLike, UartLike, I2cLike, UsbDeviceLike 等の Concept 一覧
- compile-fail テストの設計（§8.2 必須）

#### 2.6 umimmio — MMIO レジスタアクセス（IMPL Phase 1）

| 現行資産 | 新規作成 | 方針 |
|---------|---------|------|
| lib/umimmio/README.md (★) | — | コピー＋修正 |
| lib/umimmio/docs/DESIGN.md (★) | — | **5ファイル統合先** |
| lib/umimmio/docs/ja/README.md (★) | — | コピー＋修正 |
| lib/umimmio/docs/USAGE.md (68行) | → DESIGN.md | **統合** |
| lib/umimmio/docs/EXAMPLES.md (22行) | → DESIGN.md | **統合** |
| lib/umimmio/docs/GETTING_STARTED.md | → DESIGN.md | **統合** |
| lib/umimmio/docs/INDEX.md | — | **新規作成** (現行版は旧形式) |
| lib/umimmio/docs/TESTING.md (60行) | — | **新規作成** (現行版は旧形式) |

**追加参照元:**
- lib/_archive/umi/mmio/docs/USAGE.md (436行) — 内部向け使用ガイド
- lib/_archive/umi/mmio/docs/NAMING.md (119行) — ALL_CAPS 命名規則
- lib/_archive/umi/mmio/docs/IMPROVEMENTS.md (1037行) — 改善提案（未解決項目は Issue 化）

**PAL 生成基盤としての文書化:**
- DESIGN.md に umimmio テンプレートが PAL コード生成の出力先として機能する関係を記述 (LIBRARY_SPEC §5)

### L2: Platform (ハードウェア抽象化)

#### 2.7 umiport — ハードウェアポーティングキット（IMPL Phase 2a-2c）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/umi/port/docs/ (9ファイル) | 全ファイル | **全新規作成** |
| lib/_archive/docs/design/pal/ (20ファイル) | — | PAL 設計参照 |
| lib/_archive/docs/design/board/ (4ファイル) | — | ボード設計参照 |

**最大規模のドキュメント**: umiport は12ライブラリ中最も複雑な構造を持つ。DESIGN.md は以下をカバー:

- 2軸構造 (スコープ軸: arch/core/mcu + 種別軸: pal/driver/board/platform)
- HAL Concept → PAL → ドライバの3層パターン
- board.lua → C++ 生成チェーン
- MCU データベーススキーマ
- PAL コード生成パイプライン（Phase 2c で追加）

**lib/docs/design/ との役割分担:**
- `lib/docs/design/`: フレームワーク全体に関わる設計資料（比較分析、アーキテクチャ決定）
- `lib/umiport/docs/`: umiport 固有の実装設計

#### 2.8 umidevice — デバイスドライバ（IMPL Phase 2c）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/standalone/umidevice/ (41行, stub) | 全ファイル | **全新規作成** |
| lib/_archive/umi/port/device/ (コーデック) | — | 参照のみ |

### L3: Domain (ドメイン固有ライブラリ)

#### 2.9 umidsp — DSP アルゴリズム（IMPL Phase 3）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/umi/dsp/README.md | 全ファイル | **全新規作成** |
| docs/dsp/ (CAT_F, 10ファイル) | — | **独立保持** (移動しない) |

**docs/dsp/ との関係:**
- `docs/dsp/`: TB-303 VCF/VCO 回路解析、VA フィルター理論 — **独立した技術資料** (再現不可能な研究記録)
- `lib/umidsp/docs/`: umidsp ライブラリの API 設計・実装設計 — **ライブラリドキュメント**
- 両者は補完関係であり、DESIGN.md から docs/dsp/ へのリンクで接続

#### 2.10 umidi — MIDI プロトコル（IMPL Phase 3）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/umi/midi/docs/PROTOCOL.md | 全ファイル | **全新規作成** |
| lib/_archive/umi/midi/docs/design.md | — | 参照のみ |
| docs/umi-sysex/ (CAT_C) | — | **独立保持** |

**docs/umi-sysex/ との関係:**
- `docs/umi-sysex/`: UMI SysEx プロトコル仕様 — **プロトコル仕様書** (CAT_C)
- `lib/umidi/docs/`: umidi ライブラリの API 設計 — **ライブラリドキュメント**
- DESIGN.md から docs/umi-sysex/ へのリンクで接続

#### 2.11 umiusb — USB デバイススタック（IMPL Phase 3）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/umi/usb/docs/ (14ファイル) | 全ファイル | **全新規作成** |

**最大規模の参照元** (14ファイル): USB Audio Class, ASRC, HAL 分離等の設計資料は豊富。新 DESIGN.md に統合する際、以下の配分を行う:
- **umiusb/docs/**: プロトコル層の設計 (USB クラス、ディスクリプタ、Audio/MIDI)
- **umiport/docs/**: USB HAL ドライバの設計 (STM32 OTG FS, UsbDeviceLike 充足)

### L4: System (OS / カーネル統合)

#### 2.12 umios — OS カーネル + サービス（IMPL Phase 4）

| 現行参照元 | 新規作成 | 方針 |
|-----------|---------|------|
| lib/_archive/umi/kernel/ (~3,138行) | 全ファイル | **全新規作成** |
| lib/_archive/umi/runtime/ (~580行) | — | 参照のみ |
| lib/_archive/umi/service/ (~3,666行) | — | 参照のみ |
| lib/_archive/umi/fs/docs/ (6ファイル) | — | StorageService 設計参照 |
| docs/umios-architecture/ (41ファイル, CAT_B) | — | **独立保持** |
| docs/umi-kernel/spec/ (4ファイル, CAT_B) | — | **独立保持** |

**最大規模のライブラリ**: DESIGN.md は以下をカバー:
- 内部5層構造 (kernel → ipc → runtime → service → adapter)
- kernel → service の依存方向逆転パターン
- 3つの xmake ターゲット (umios, umios.service, umios.crypto)

**docs/umios-architecture/ との関係:**
- `docs/umios-architecture/`: 外部設計仕様書 (41ファイルの包括的アーキテクチャ)
- `lib/umios/docs/`: 実装設計 (新12ライブラリ構成に基づくコード構造)
- DESIGN.md から umios-architecture/ へのリンクで接続

---

## 3. 現行 lib/umi/*/docs/ の処理マップ

Phase 0 で全て `lib/_archive/umi/*/docs/` にアーカイブ。以下は参照・移行の対応表:

| 現行パス | ファイル数 | 行数 | 移行先 | 処理 |
|---------|:---------:|:----:|--------|------|
| lib/umi/fs/docs/ | 6 | ~1,200 | umios 内部参照 | StorageService 設計統合 |
| lib/umi/usb/docs/ | 14 | ~3,500 | umiusb + umiport | プロトコル/HAL 分配 |
| lib/umi/midi/docs/ | 3 | ~500 | umidi | DESIGN.md 参照 |
| lib/umi/port/docs/ | 9 | ~1,500 | umiport | DESIGN.md 参照 |
| lib/umi/mmio/docs/ | 3 | ~1,600 | umimmio | DESIGN.md 統合 |
| lib/umi/dsp/ | 1 | ~40 | umidsp | 新規 DESIGN.md |
| lib/umi/bench_old/ | 2 | ~740 | — | **削除** (umibench 置換済み) |
| lib/umi/ref/ | 1 | ~30 | — | 保持 (規約検証用) |

---

## 4. 品質評価

### 4.1 現行ライブラリ（Phase 0 前の状態）

| ライブラリ | 現行品質 | SPEC準拠 | 新構築方針 |
|-----------|---------|---------|-----------|
| umibench | ★★★★★ | ★★★★★ | コピー＋INDEX/TESTING追加 |
| umimmio | ★★★★☆ | ★★★☆☆ | コピー＋5ファイル統合＋INDEX/TESTING作成 |
| umirtm | ★★★★☆ | ★★★★★ | コピー＋INDEX/TESTING追加 |
| umitest | ★★★★☆ | ★★★★★ | コピー＋INDEX/TESTING追加 |
| umicore | — | — | 全新規作成 |
| umihal | — | — | 全新規作成 |
| umiport | ★★☆☆☆ | ★☆☆☆☆ | 全新規作成（9ファイル参照） |
| umidevice | ★☆☆☆☆ | ★☆☆☆☆ | 全新規作成 |
| umidsp | ★★☆☆☆ | — | 全新規作成 |
| umidi | ★★★★☆ | — | 全新規作成（3ファイル参照） |
| umiusb | ★★★★☆ | — | 全新規作成（14ファイル参照） |
| umios | — | — | 全新規作成（最大規模） |

### 4.2 目標品質

全12ライブラリが **★★★★☆ (4/5) 以上** を達成すること (LIBRARY_SPEC §8.3)。

---

## 5. 推奨事項

1. **umibench を模範として活用** — 全ライブラリのドキュメントは umibench のフォーマットを参考に作成
2. **Phase 1 で6ライブラリのドキュメントテンプレートを確立** — 残りのフェーズで効率的に再利用
3. **docs/ (CAT_A-F) との接続を明確化** — 各ライブラリの DESIGN.md から関連する docs/ ファイルへのリンクを設置
4. **日本語ドキュメントは自然な翻訳** — word-for-word ではなく、umibench ja/ のスタイルを踏襲
5. **lib/umi/mmio/docs/IMPROVEMENTS.md の未解決項目** — 新規 Issue として管理し、umimmio の開発ロードマップに統合
