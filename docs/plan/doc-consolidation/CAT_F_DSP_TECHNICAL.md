# CAT_F: DSP/技術資料 — 統合内容要約

**カテゴリ:** F. DSP/技術資料
**配置先:** `docs/dsp/`（DSP 技術資料）+ `docs/hw_io/`（HW I/O 処理設計）
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. カテゴリ概要

DSP アルゴリズムの設計・解析文書と、ハードウェア I/O 処理設計文書。独立性が高く、コードの実装と直結する技術資料。回路解析の貴重な記録を含む。

**対象読者:** DSP 開発者、ドライバ開発者
**特徴:** 各サブディレクトリが完全に独立しており、統廃合の必要性が最も低いカテゴリ

---

## 2. 所属ドキュメント一覧

### 2.1 docs/dsp/tb303/vcf/ — TB-303 VCF フィルター（5ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 1 | TB303_VCF_COMPLETE.md | ~2000+ | ★ | **保持** — 完全解析ガイド |
| 2 | TB303VCF.md | ~1000+ | ★ | **保持** — 初期解析 |
| 3 | TB303VCF2.md | ~1000+ | ★ | **保持** — 第2版解析 |
| 4 | TB303_VCF_ERRATA.md | ~200 | ★ | **保持** — 正誤表 |
| 5 | S0_OPTIMIZATION.md | ~300 | ★ | **保持** — 最適化 |

### 2.2 docs/dsp/tb303/vco/ — TB-303 VCO Waveshaper（3 + 1ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 6 | TB303_WAVESHAPER_DOC.md | ~500 | ★ | **保持** — Waveshaper 技術文書 |
| 7 | WAVESHAPER_EVALUATION.md | ~300 | ★ | **保持** — 評価結果 |
| 8 | 2SA733.md | ~200 | ★ | **保持** — トランジスタ解析 |
| 9 | test/README.md | ~50 | ★ | **保持** — テスト説明 |

### 2.3 docs/dsp/vafilter/ — Virtual Analog フィルター（1ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 10 | VAFILTER_DESIGN.md | ~1076 | ★ | **保持** — VA フィルター設計 |

### 2.4 docs/hw_io/ — ハードウェア I/O 処理設計（5ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 11 | README.md | ~33 | ★ | **保持** — 共通方針 |
| 12 | button.md | ~150 | ★ | **保持** — ボタン入力処理 |
| 13 | encoder.md | ~200 | ★ | **保持** — エンコーダー処理 |
| 14 | potentiometer.md | ~200 | ★ | **保持** — ADC 入力処理 |
| 15 | midi_uart.md | ~200 | ★ | **保持** — MIDI UART 処理 |

---

## 3. ドキュメント別内容要約

### 3.1 TB-303 VCF 解析群（5ファイル、合計5000+行）

Roland TB-303 のダイオードラダーフィルターの完全解析。回路レベルから DSP 実装まで。

**TB303_VCF_COMPLETE.md（主文書）:**
- **回路解析** — ダイオードラダー構造（4極、24dB/oct）、C₁ = C/2 による 18dB/oct 的応答
- **Tim Stinchcombe 解析** — 6つの追加極と6つの零点（カップリングコンデンサ由来）
- **1/√2 補正係数** — 数学的根拠の導出
- **離散化手法** — BLT, TPT/ZDF
- **Karrikuh (Wurtz) 実装解析** — 既存オープンソース実装の分析
- **TPT/ZDF 形式への変換** — リアルタイム実装向け
- **組込み最適化** — Cortex-M4 向けパフォーマンスチューニング

**TB303VCF.md / TB303VCF2.md:**
- VCF 解析の初期版と第2版。COMPLETE.md の根拠・発展資料

**TB303_VCF_ERRATA.md:**
- 解析・実装上の正誤表

**S0_OPTIMIZATION.md:**
- SIMD/固定小数点/ルックアップテーブル等の最適化手法

---

### 3.2 TB-303 VCO Waveshaper（3ファイル）

TB-303 のウェーブシェイパー回路の SPICE 精度実装。

**TB303_WAVESHAPER_DOC.md:**
- **回路トポロジー** — PNP トランジスタ（2SA733P）ベースの波形整形
- **コンポーネント値** — R35=100kΩ, R34=10kΩ, R45=22kΩ, C10=10nF, C11=1μF
- **Ebers-Moll モデル** — 完全な PNP トランジスタモデルでの Newton-Raphson 解析
- **Falstad 回路シミュレータとの一致** — 検証済み

**2SA733.md:**
- 2SA733P PNP トランジスタの詳細特性解析（β=100, VBE(sat), hFE ランク等）

**WAVESHAPER_EVALUATION.md:**
- 各実装アプローチの評価（精度、計算コスト、安定性）

---

### 3.3 VAFILTER_DESIGN.md — VA フィルター設計（1076行）

Virtual Analog フィルターの包括的設計ガイド。

**主要内容:**
- **離散化手法** — BLT（双一次変換）、TPT（トポロジー保存変換）、ZDF（ゼロ遅延フィードバック）
- **Vadim Zavalishin「The Art of VA Filter Design」** — 主要参考文献
- **Andy Simper (Cytomic) SVF/SKF** — 技術文書群の要約
- **mystran (Teemu Voipio)** — 安価な非線形ゼロ遅延フィルター
- **SVF（State Variable Filter）** — TPT 形式の実装
- **SKF（Sallen-Key Filter）** — 非線形サチュレーション
- **ラダーフィルター** — Moog/TB-303 ラダーの VA 実装
- **安定性** — ナイキスト限界での振る舞い、補正手法

---

### 3.4 hw_io/ — ハードウェア I/O 処理設計（5ファイル）

組込みデバイスの入力処理を統一フォーマットで設計。

**README.md（共通方針）:**
- **ISR/非ISR 分離** — ISR は最小処理（GPIO読み取り、DMA バッファ書き込み）のみ
- **周期設計** — ボタン/ポテンショメータ/MIDI = 1kHz統一、エンコーダーサンプリング = 4kHz

**button.md:**
- デバウンスアルゴリズム（積分方式）、長押し検出、イベント生成

**encoder.md:**
- Gray コードデコード（LUT方式）、4kHz サンプリング、Velocity 計算

**potentiometer.md:**
- ADC/DMA オーバーサンプリング（10kHz → 1kHz）、ヒステリシスフィルタ、デッドゾーン処理

**midi_uart.md:**
- DMA 連続受信、Running Status 対応、リアルタイムメッセージの優先処理

---

## 4. カテゴリ内の関連性マップ

```
docs/dsp/ ← DSP 技術資料（独立）
├── tb303/
│   ├── vcf/ ← VCF フィルター解析（5ファイル、相互参照）
│   │   ├── TB303_VCF_COMPLETE.md ← 主文書
│   │   ├── TB303VCF.md ← 初期版
│   │   ├── TB303VCF2.md ← 第2版
│   │   ├── TB303_VCF_ERRATA.md ← 正誤
│   │   └── S0_OPTIMIZATION.md ← 最適化
│   └── vco/ ← VCO Waveshaper（3ファイル、相互参照）
│       ├── TB303_WAVESHAPER_DOC.md ← 主文書
│       ├── 2SA733.md ← トランジスタ特性
│       ├── WAVESHAPER_EVALUATION.md ← 評価
│       └── test/README.md
└── vafilter/ ← VA フィルター設計（独立）
    └── VAFILTER_DESIGN.md

docs/hw_io/ ← HW I/O 処理設計（統一フォーマット）
├── README.md ← 共通方針（ISR分離、周期設計）
├── button.md
├── encoder.md
├── potentiometer.md
└── midi_uart.md

関連するが別カテゴリの文書:
├── docs/refs/API_DSP.md (CAT_A) ← DSP API 定義
├── docs/refs/UMIDSP_GUIDE.md (CAT_A) ← DSP 実装ガイド
└── docs/dev/GUIDELINE.md (CAT_D) ← Data/State 分離パターン
```

---

## 5. 統廃合アクション

### 変更不要（全ファイル保持）

**本カテゴリは統廃合が完全に不要。** 全15ファイルがそのまま保持される。

理由:
1. 各ファイルが独立した技術資料として完結
2. 重複が一切ない
3. 回路解析文書は再現不可能な貴重な研究記録
4. hw_io/ は統一フォーマットで整理済み

---

## 6. 品質評価

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★★ | VCF/VCO/VA フィルター + 4種 HW I/O を完全カバー |
| 一貫性 | ★★★★★ | hw_io/ は統一フォーマット。dsp/ は技術文書として統一 |
| 更新頻度 | ★★★☆☆ | dsp/ は 2026-01 作成後安定。hw_io/ は 2026-02 に整備 |
| 読みやすさ | ★★★★★ | 回路図、数式、コード例が豊富 |
| コードとの整合 | ★★★★★ | 実装コードと直結。検証済み |
| 独立性 | ★★★★★ | 他カテゴリとの依存なし |

---

## 7. 推奨事項

1. **本カテゴリは変更不要** — 全ファイルが高品質で独立した技術資料
2. **docs/dsp/ は貴重な研究記録** — 回路解析、数学的導出、SPICE 検証は再現困難。長期保存が重要
3. **hw_io/ のフォーマットは模範** — 他の HW I/O（LED 出力、ディスプレイ等）を追加する際のテンプレートとして活用
4. **将来の拡張** — 新しい DSP モジュール（エンベロープ、オシレーター等）の設計文書を docs/dsp/ に追加する場合、TB-303 解析のフォーマットを参考に
