# CAT_A: コア仕様 — 統合内容要約

**カテゴリ:** A. コア仕様
**配置先:** `docs/refs/` (既存構成を維持)
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. カテゴリ概要

UMI プロジェクトの根幹を定義する仕様群。アーキテクチャ、コンポーネント仕様（UMIP/UMIC/UMIM）、型制約（Concepts）、命名体系、セキュリティ、API リファレンスを含む。

**対象読者:** 全開発者（アプリ開発者、カーネル開発者、移植者）
**正本の場所:** `docs/refs/`（14ファイル）+ `docs/NOMENCLATURE.md`

---

## 2. 所属ドキュメント一覧

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 1 | docs/refs/ARCHITECTURE.md | ~200 | ★ | **保持（正本）** |
| 2 | docs/refs/CONCEPTS.md | ~200 | ★ | **保持（正本）** |
| 3 | docs/refs/SECURITY.md | ~1100 | ★ | **保持（正本）** |
| 4 | docs/refs/UMIP.md | ~200 | ★ | **保持（正本）** |
| 5 | docs/refs/UMIC.md | ~200 | ★ | **保持（正本）** |
| 6 | docs/refs/UMIM.md | ~250 | ★ | **保持（正本）** |
| 7 | docs/refs/UMIM_NATIVE_SPEC.md | ~200 | ★ | **保持（正本）** |
| 8 | docs/refs/API_APPLICATION.md | ~150 | ★ | **保持（正本）** |
| 9 | docs/refs/API_KERNEL.md | ~150 | ★ | **保持（正本）** |
| 10 | docs/refs/API_DSP.md | ~150 | ★ | **保持（正本）** |
| 11 | docs/refs/API_BSP.md | ~120 | ◆ | **保持（将来仕様）** |
| 12 | docs/refs/API_UI.md | ~120 | ◆ | **保持（将来仕様）** |
| 13 | docs/refs/UMIDSP_GUIDE.md | ~1000 | ★ | **保持（正本）** |
| 14 | docs/NOMENCLATURE.md | ~200 | ★ | **保持（正本）** パス更新が必要 |

---

## 3. ドキュメント別内容要約

### 3.1 ARCHITECTURE.md — アーキテクチャ仕様

**バージョン:** 0.10.0 | **ステータス:** 実装済み/将来仕様混在

UMI 全体アーキテクチャの正本。CLAUDE.md から参照される最重要文書。

**主要内容:**
- **設計思想** — オーディオ処理最優先、正規化データ、統一main()モデル、非特権アプリ実行
- **統一アプリケーションモデル** — Processor Task + Control Task の2タスク構成
- **プラットフォーム対応** — 組込み(✓実装済)、Web(.umim/AudioWorklet、将来)、デスクトップ(VST3/AU、将来)
- **システム構成** — Application → UMI SDK API → Runtime(Kernel/WASM/DAW) → HAL/PAL
- **ProcessorLike Concept** — C++20 Concepts による型制約
- **AudioContext** — process() に渡される統一コンテキスト

**関連文書:** UMIP.md, UMIC.md, CONCEPTS.md, NOMENCLATURE.md

**統廃合時の注意:**
- CLAUDE.md の `docs/refs/specs/ARCHITECTURE.md` 参照パスが壊れている → `docs/refs/ARCHITECTURE.md` に修正
- 実装状況凡例（✓/◇）は定期的な更新が必要

---

### 3.2 CONCEPTS.md — C++20 Concepts 設計

UMI で使用される全 Concepts の定義と設計原則。

**主要内容:**
- **設計原則** — 静的ディスパッチ優先、型消去は明示的、コンパイル時検証、ゼロコスト抽象化
- **既存 Concepts:**
  - DSP系: `ProcessorLike`, `Controllable`, `HasParams`, `HasPorts`, `Stateful`
  - HAL系: `Hal`, `Class` (USB)
  - 動的ディスパッチ: `AnyProcessor` (型消去)
- **追加すべき Concepts** — Transport, Serializable, EventHandler 等

**関連文書:** ARCHITECTURE.md（ProcessorLikeの使用箇所）, UMIP.md

**統廃合時の注意:**
- コードパス `lib/umi/core/processor.hh`, `lib/umiusb/include/hal.hh` は実際のコードと整合確認が必要

---

### 3.3 SECURITY.md — セキュリティリスク分析と対策指針

**行数:** ~1100行の大規模文書

組込み電子楽器のセキュリティを体系的に分析。

**主要内容:**
- **権限モデル** — カーネル(特権) / ドライバ(特権) / アプリ(非特権) の3層
- **脅威モデル** — 外部入力(WebMIDI/USB)、ユーザーアプリ、物理攻撃
- **対策:**
  - MPU による非特権保護
  - Ed25519 署名検証
  - SysEx 入力バリデーション
  - アプリローダーの検証チェーン
- **ビルドターゲット別** — 配布版（署名必須）vs 開発版（署名オプション）

**関連文書:** docs/umios-architecture/05-binary/14-security.md（重複あり、要整理）

**統廃合時の注意:**
- umios-architecture の14-security.md と内容が重複する箇所がある
- 本文書がより包括的なリスク分析を含むため正本として保持し、14-security.md は実装仕様として補完関係

---

### 3.4 UMIP.md — UMI-Processor 仕様書

**バージョン:** 3.0.0-draft

ヘッドレスオーディオ処理ユニットの仕様。UMI の DSP コア。

**主要内容:**
- **最小インターフェース** — `process()` のみ必須
- **言語非依存** — C/C++, Rust, Zig 等で実装可能
- **統一 main() パターン** — 全プラットフォーム共通のアプリコード
- **process() のリアルタイム制約** — ヒープ禁止、mutex 禁止、例外禁止、stdio 禁止

**関連文書:** UMIC.md（Controller側）, UMIM.md（バイナリ形式）, ARCHITECTURE.md

---

### 3.5 UMIC.md — UMI-Controller 仕様書

**バージョン:** 3.0.0-draft

Control Task（main() で実行される UI ロジック）の仕様。

**主要内容:**
- **統一モデル** — 組込み/Web 共通の main() エントリポイント
- **イベント駆動** — `wait_event()` で非同期イベント待機
- **MVC 位置づけ** — Controller = main(), Model = Processor
- **プラットフォーム抽象化** — 組込み(syscall) vs Web(Asyncify) vs デスクトップ(ホストスレッド)
- **必要性の判断基準** — UI 状態を持つアプリは必要、DSP 専用なら不要

**関連文書:** UMIP.md, API_APPLICATION.md

---

### 3.6 UMIM.md — UMI-Module バイナリ形式仕様

**バージョン:** 3.0.0-draft

UMI アプリケーションのバイナリ配布形式。

**主要内容:**
- **2形式** — `.umim`(WASM/Web) と `.umia`(ネイティブ/組込み)
- **WASM エクスポート** — `umi_init()`, `umi_process()`, `umi_push_event()` 等
- **UMIP/UMIC/UMIM の関係整理** — UMIP=DSP処理、UMIC=UI、UMIM=バイナリ形式

**関連文書:** UMIM_NATIVE_SPEC.md（ネイティブ拡張）, UMIP.md, UMIC.md

---

### 3.7 UMIM_NATIVE_SPEC.md — UMIM-Native 仕様

**バージョン:** 1.0.0-draft

組込み環境でアプリケーションが動的にロード可能な DSP モジュールの仕様。

**主要内容:**
- **背景** — 既存の `.umia` はアプリ単位。モジュール単位の動的ロードが必要
- **DSP Chain** — 複数のモジュール（Synth, Effect, Filter）を連結
- **UMIP 互換** — process(AudioContext&) のインターフェースを共有
- **ロード/アンロード** — アプリ内でのモジュールライフサイクル

**関連文書:** UMIM.md

---

### 3.8 API_APPLICATION.md — アプリケーション API

**ステータス:** 実装済み（v0.10.0）

統一 main() モデルで使用する API リファレンス。

**主要内容:**
- **ライフサイクル** — `register_processor()`, `wait_event()`, `send_event()`, `log()`, `get_time()`
- **使用例** — 最小実装、UI 連携（SharedMemory 経由の Input/Output）
- **ヘッダ** — `<umi/app.hh>`

**関連文書:** UMIP.md, UMIC.md, API_KERNEL.md

---

### 3.9 API_KERNEL.md — カーネル API

**ステータス:** 実装済み（v0.10.0）

組込み環境でのカーネル直接操作 API。

**主要内容:**
- **Syscall ABI** — 番号体系（Exit=0, RegisterProc=1, WaitEvent=2, ..., GetShared=40）
- **呼び出し規約** — ARM Cortex-M `svc #0`、r0=番号, r1-r4=引数
- **カーネル内部 API** — タスク管理、IRQ 設定、MPU 構成

**関連文書:** docs/umi-kernel/spec/system-services.md（Syscall 詳細）

---

### 3.10 API_DSP.md — DSP モジュール API

**ステータス:** 実装済み

DSP モジュール（Oscillator, Filter, Envelope）の設計原則と API。

**主要内容:**
- **設計原則** — `operator()` をプライマリ、`set_params(dt)` で係数事前計算、`dt` 使用で除算回避
- **パフォーマンス指向** — Cortex-M4 で除算14サイクル vs 乗算1サイクル
- **ヘッダ** — `<oscillator.hh>`, `<filter.hh>`, `<envelope.hh>`

**関連文書:** UMIDSP_GUIDE.md（詳細ガイド）

---

### 3.11 API_BSP.md — BSP I/O 型定義

**ステータス:** 将来仕様

ボードサポートパッケージの I/O 型定義。

**主要内容:**
- **HwType** — Adc, Gpio, Encoder, Touch, Pwm, PwmRgb, GpioOut, I2c7Seg, SpiOled
- **ValueType** — Percent, Bipolar, Db, Frequency, Time, Note, Enum
- **Curve** — 入力の非線形マッピング定義
- **テンプレートベース** — 属性あり/なしをコンパイル時切り替え

**関連文書:** API_UI.md（アプリ側のI/O抽象化）

---

### 3.12 API_UI.md — UI API (入出力抽象化)

**ステータス:** 将来仕様

ハードウェア非依存の統一 I/O インターフェース。

**主要内容:**
- **設計思想** — 物理デバイスの違いはアプリから見えない（ノブ/スライダー → `input[i]`）
- **Input** — `float input[i]`（0.0-1.0）、`changed(i)`、`triggered(i)`
- **Output** — `float output[i]`、`canvas`（ディスプレイ）
- **SharedMemory 経由** — カーネル/ドライバが物理デバイスをマッピング

**関連文書:** API_BSP.md（BSP側のマッピング定義）

---

### 3.13 UMIDSP_GUIDE.md — DSP 設計・実装ガイド

**行数:** ~1000行の大規模ガイド

DSP API の利用者向け情報と実装設計指針の統合ガイド。

**主要内容:**
- **コア原則** — 状態を持つユニット: 係数 + 状態 + `process()`
- **Data/State 分離** — 係数は共有・不変、状態はユニット内保持
- **構造分類** — 純粋関数 / 状態を持つユニット / 複合モデル
- **SPSC 通信** — リアルタイムスレッドとの安全なデータ交換
- **C++ と Rust の両方でコード例**を提示

**関連文書:** API_DSP.md, docs/dev/GUIDELINE.md（設計パターン）

---

### 3.14 NOMENCLATURE.md — 命名体系・用語定義

**バージョン:** 0.10.0

UMI プロジェクト全体の用語・命名の正本。

**主要内容:**
- **プロジェクト階層図** — UMI Project > UMI-OS Specification > UMI-OS Kernel > UMI Application
- **用語定義** — Processor, Controller, Module, AudioContext, Event, SharedMemory 等
- **名前空間規約** — `umi::`, `umi::kernel::`, `umi::app::` 等
- **バージョニング** — セマンティックバージョニングのルール

**統廃合時の注意:**
- 一部パス（`lib/umi/port` 等）が古い可能性 → コードベースと整合確認

---

## 4. カテゴリ内の関連性マップ

```
NOMENCLATURE.md ← 用語の正本（全文書が参照すべき）
    │
    ▼
ARCHITECTURE.md ← 全体アーキテクチャ（コア仕様の中心）
    │
    ├── CONCEPTS.md ← 型制約定義（ProcessorLike 等）
    │
    ├── UMIP.md ←── Processor仕様
    │     │
    │     ├── UMIC.md ←── Controller仕様
    │     │
    │     └── UMIM.md ←── バイナリ形式仕様
    │           │
    │           └── UMIM_NATIVE_SPEC.md ← ネイティブモジュール
    │
    ├── SECURITY.md ← セキュリティモデル
    │
    └── API群（4+1ファイル）
          ├── API_APPLICATION.md ← アプリAPI
          ├── API_KERNEL.md ← カーネルAPI
          ├── API_DSP.md ← DSP API
          │     └── UMIDSP_GUIDE.md ← DSP詳細ガイド
          ├── API_BSP.md ← BSP型定義（将来）
          └── API_UI.md ← UI抽象化（将来）
```

---

## 5. 統廃合アクション

### 5.1 変更不要（そのまま保持）

| ファイル | 理由 |
|---------|------|
| ARCHITECTURE.md | 正本。CLAUDE.md から参照 |
| CONCEPTS.md | 正本。コードベースの型制約の定義元 |
| UMIP.md | コア仕様の正本 |
| UMIC.md | コア仕様の正本 |
| UMIM.md | コア仕様の正本 |
| UMIM_NATIVE_SPEC.md | コア仕様の正本 |
| API_APPLICATION.md | 正本。CLAUDE.md から参照 |
| API_KERNEL.md | 正本 |
| API_DSP.md | 正本 |
| UMIDSP_GUIDE.md | 正本。1000行の貴重な技術ガイド |
| NOMENCLATURE.md | 用語の正本 |

### 5.2 軽微な更新が必要

| ファイル | 必要な更新 |
|---------|-----------|
| API_BSP.md | 将来仕様マーク維持。実装時に更新 |
| API_UI.md | 将来仕様マーク維持。実装時に更新 |
| SECURITY.md | umios-architecture/05-binary/14-security.md との重複箇所を相互参照リンクで整理 |
| NOMENCLATURE.md | コードパス（lib/umi/port 等）の整合性確認 |

### 5.3 他カテゴリとの重複整理

| 重複箇所 | 正本 | 参照側 | アクション |
|---------|------|--------|-----------|
| セキュリティモデル | SECURITY.md | umios-architecture/05-binary/14-security.md | 相互リンクで役割分担を明確化 |
| Syscall ABI | API_KERNEL.md | umi-kernel/spec/system-services.md | system-services.md が詳細版、API_KERNEL.md は概要版として共存 |
| DSP設計パターン | UMIDSP_GUIDE.md | docs/dev/GUIDELINE.md | GUIDELINE.md は独立した設計パターン集として保持（名前変更: DESIGN_PATTERNS.md） |

---

## 6. 品質評価

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★★ | 全コンポーネントの仕様が揃っている |
| 一貫性 | ★★★★☆ | バージョン表記が統一されている。一部パスが古い |
| 更新頻度 | ★★★☆☆ | v0.10.0（2025-01）以降の大きな更新なし |
| 読みやすさ | ★★★★☆ | 図やコード例が豊富。ASCII アートの構成図が理解を助ける |
| コードとの整合 | ★★★☆☆ | 将来仕様（◇）のマーキングはあるが、コードパスの一部が古い可能性 |

---

## 7. 推奨事項

1. **本カテゴリは高品質で安定している** — 大規模な統廃合は不要
2. **パス整合性チェック** — NOMENCLATURE.md と CONCEPTS.md のコードパス参照を実コードベースと照合
3. **SECURITY.md と umios-architecture の住み分け** — SECURITY.md はリスク分析・方針、14-security.md は実装仕様と明確化
4. **将来仕様文書（API_BSP, API_UI）の実装追従** — 実装が進んだらステータスマーキングを更新
5. **CLAUDE.md / copilot-instructions.md の参照パス修正** — Phase 6 で実施（`docs/refs/specs/` → `docs/refs/`）
