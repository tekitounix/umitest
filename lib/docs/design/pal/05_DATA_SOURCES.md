# PAL データソース完全リファレンス

**ステータス:** 調査完了
**関連文書:**
- [02_CATEGORY_INDEX.md](02_CATEGORY_INDEX.md) — PAL カテゴリ一覧 (C1–C13)
- [03_ARCHITECTURE.md](03_ARCHITECTURE.md) — PAL アーキテクチャ提案
- [04_ANALYSIS.md](04_ANALYSIS.md) — 既存 PAL アプローチの横断分析
- [../07_HW_DATA_PIPELINE.md](../07_HW_DATA_PIPELINE.md) — HW データ統合パイプライン

---

## 1. 本ドキュメントの目的

各 PAL カテゴリ (C1–C13) の定義を、対象プラットフォームごとに **どこから取得・抽出できるか**、**どのようなツールが必要か**、**何が手動作業になるか** を網羅的に整理する。

既存の事例（modm, embassy, stm32-rs, Kvasir 等）と、UMI 独自に必要なツール開発の両面から調査した結果をまとめる。

---

## 2. 対象プラットフォーム

| 略称 | プラットフォーム | アーキテクチャ | 優先度 |
|------|----------------|---------------|--------|
| **STM32** | STM32 (F4/H7/L4/G4 等) | Cortex-M | 最優先 |
| **RP** | RP2040 / RP2350 | Cortex-M0+ / M33 / RISC-V (Hazard3) | 次点 |
| **ESP** | ESP32-S3 / ESP32-P4 | Xtensa LX7 / RISC-V | 次点 |
| **iMXRT** | i.MX RT (1060 等) | Cortex-M7 | 将来 |

---

## 3. データソース一覧

本セクションでは、PAL 定義に利用可能な全データソースを概説する。

### 3.1 ベンダー公式マシン可読データソース

| # | データソース | フォーマット | 提供元 | ライセンス | 対応プラットフォーム |
|---|------------|-----------|--------|-----------|-------------------|
| D1 | **CMSIS-SVD** | XML | ARM 仕様 / ベンダー実装 | ベンダー依存 (ST: Apache-2.0) | STM32, RP, iMXRT, Nordic |
| D2 | **CMSIS ヘッダ** (`stm32f407xx.h`, `core_cm4.h`) | C/C++ | ARM + ベンダー | BSD-3 / Apache-2.0 | STM32, iMXRT, Nordic |
| D3 | **CMSIS-Pack (.pdsc)** | XML | ARM 仕様 / ベンダー | ベンダー依存 | 全 Cortex-M |
| D4 | **STM32CubeMX DB** (`.mcu` / `.ip` / `GPIO-*_Modes.xml`) | XML | ST | **ST SLA (再配布不可)** | STM32 のみ |
| D5 | **STM32_open_pin_data** | XML | ST | **BSD-3-Clause** | STM32 のみ |
| D6 | **Pico SDK** (`hardware_regs/`, SVD) | C ヘッダ + SVD | Raspberry Pi | BSD-3-Clause | RP のみ |
| D7 | **ESP-IDF SoC コンポーネント** (`soc_caps.h`, レジスタヘッダ, LL 関数) | C/C++ | Espressif | Apache-2.0 | ESP のみ |
| D8 | **MCUXpresso SDK** (デバイスヘッダ, SVD, クロック実装) | C/C++ + SVD | NXP | BSD-3-Clause | iMXRT のみ |
| D9 | **ARM Architecture Reference Manual** | PDF | ARM | 閲覧自由 / 再配布制限 | 全 ARM |
| D10 | **ベンダーリファレンスマニュアル** (RM0090 等) | PDF | 各ベンダー | 閲覧自由 / 再配布制限 | 全プラットフォーム |
| D11 | **ベンダーデータシート** | PDF | 各ベンダー | 閲覧自由 / 再配布制限 | 全プラットフォーム |

### 3.2 コミュニティ維持のデータソース

| # | データソース | メンテナ | 内容 | ライセンス |
|---|------------|---------|------|-----------|
| C1 | **stm32-rs SVD パッチ** | Rust Embedded コミュニティ | SVD エラー修正 YAML パッチ (数百ファイル) | MIT/Apache-2.0 |
| C2 | **stm32-data** (embassy) | Embassy-rs | CubeMX + SVD + HAL ヘッダの統合 JSON DB | MIT/Apache-2.0 |
| C3 | **modm-devices** | modm-io | CubeMX + SVD + CMSIS ヘッダ + PDF の統合 DeviceTree | MPL-2.0 |
| C4 | **cmsis-svd-data** | cmsis-svd | 全ベンダー SVD の集約リポジトリ | ベンダー依存 |
| C5 | **cmsis-svd-stm32** (modm) | modm-io | ST 公式 SVD の Apache-2.0 ミラー | Apache-2.0 |
| C6 | **cmsis-header-stm32** (modm) | modm-io | CMSIS ヘッダの集約 | BSD-3-Clause |
| C7 | **esp-rs SVD** | esp-rs コミュニティ | idf2svd による ESP32 系 SVD 生成 | MIT/Apache-2.0 |
| C8 | **probe-rs target DB** | probe-rs | CMSIS-Pack 由来のデバッグターゲット記述 (YAML) | MIT/Apache-2.0 |
| C9 | **Zephyr Devicetree** | Zephyr Project | DTS 形式のハードウェア記述 | Apache-2.0 |

### 3.3 umi_mmio 既存資産

| # | ツール / データ | パス | 状態 |
|---|---------------|------|------|
| U1 | **svd2ral v1** (Jinja2) | `.archive/tools/svd2ral/` | 動作済み — umimmio API 適合要 |
| U2 | **svd2ral v2** (直接生成) | `.archive/tools/svd2ral_v2/` | 動作済み — umimmio API 適合要 |
| U3 | **cmsis2svd** | `.archive/tools/cmsis2svd.py` | 部分動作 — typedef struct パース困難で中断 |
| U4 | **cmsis-dev-extractor** | `.archive/tools/cmsis-dev-extractor.py` | **動作確認済み** — メモリ/デバイス構成 |
| U5 | **svd2cpp_vec** | `.archive/tools/svd2cpp_vec.py` | **動作確認済み** — ベクターテーブル + IRQn |
| U6 | **patch_svd** | `.archive/tools/patch_svd.py` | 動作済み — GPIO 列挙値パッチ |
| U7 | **validate_svd** | `.archive/tools/validate_svd.py` | 動作済み — XML Schema バリデーション |

---

## 4. カテゴリ × プラットフォーム データソースマトリクス

### 凡例

- **◎** 完全自動抽出可能 (ツール実装 or 実装容易)
- **○** 自動抽出可能だが品質パッチ/調整が必要
- **△** 半自動 (構造化データ + 手動キュレーション)
- **✎** 手動作業が主体 (PDF/データシートから)
- **—** 該当なし

### 4.1 総合マトリクス

| カテゴリ | STM32 | RP2040/RP2350 | ESP32-S3/P4 | i.MX RT |
|---------|-------|--------------|-------------|---------|
| **C1 コアペリフェラル** | ○ D2+D9 | ○ D6+D9 | △ D7+D9 | ○ D8+D9 |
| **C2 コアシステム** | ◎ D3/D2 | ◎ D6 | ◎ D7 | ◎ D8/D3 |
| **C3 ベクター** | ◎ D2 | ◎ D6 | △ D7 | ◎ D8 |
| **C4 メモリマップ** | ◎ D3/D1 | ◎ D6 | ○ D7 | ◎ D8/D3 |
| **C5 ペリフェラルレジスタ** | ○ D1+C1 | ◎ D6 | △ D7 | ◎ D8 |
| **C6 GPIO MUX** | ◎ D5/D4 | ◎ D6 | ◎ D7 | ○ D8 |
| **C7 クロックツリー** | △ D4/D10 | △ D6/D10 | △ D7/D10 | △ D8/D10 |
| **C8 DMA マッピング** | △ D4/D10 | ◎ D6 | △ D7/D10 | ○ D8 |
| **C9 電力管理** | ✎ D10 | ✎ D10 | ✎ D10 | ✎ D10 |
| **C10 セキュリティ** | ✎ D10 | ✎ D10 | ✎ D10 | ✎ D10 |
| **C11 デバイスメタ** | ◎ D3/D4 | ◎ D6 | ◎ D7 | ◎ D8/D3 |
| **C12 リンカ/スタートアップ** | ◎ D3/D2 | ○ D6 | △ D7 | ○ D8 |
| **C13 デバッグ/トレース** | ○ D3/D9 | ○ D6 | △ D7 | ○ D3/D9 |

---

## 5. カテゴリ別データソース詳細

### 5.1 C1: コアペリフェラルレジスタ (NVIC, SCB, SysTick, DWT, ITM, MPU, FPU)

コアペリフェラルは **SVD に含まれない**。アーキテクチャごとに固定であり、数は限定的。

#### STM32 (Cortex-M)

| データ | 最適ソース | 抽出方法 | 自動化 | 備考 |
|--------|----------|---------|--------|------|
| NVIC レジスタ | D2 `core_cm4.h` | ヘッダ構造体パース or modm 方式コンパイル実行 | ○ | `NVIC_Type` 構造体 |
| SCB レジスタ | D2 `core_cm4.h` | 同上 | ○ | `SCB_Type` 構造体 |
| SysTick レジスタ | D2 `core_cm4.h` | 同上 | ○ | `SysTick_Type` 構造体 |
| DWT レジスタ | D2 `core_cm4.h` | 同上 | ○ | `DWT_Type` 構造体 |
| ITM レジスタ | D2 `core_cm4.h` | 同上 | ○ | `ITM_Type` — PORT union 配列が複雑 |
| MPU レジスタ | D2 `core_cm4.h` | 同上 | ○ | `MPU_Type` 構造体 |
| FPU レジスタ | D2 `core_cm4.h` | 同上 | ○ | `FPU_Type` 構造体 |
| CoreDebug レジスタ | D2 `core_cm4.h` | 同上 | ○ | `CoreDebug_Type` 構造体 |

**既存事例:**
- **Rust cortex-m クレート**: 手書き Rust 構造体。ARM Architecture Reference Manual 準拠。
- **modm**: CMSIS ヘッダを直接ラップ (独自レジスタ定義は生成しない)。
- **umi_mmio**: `cortex_m4_core.hh` を生成済み (U3 cmsis2svd 経由、部分的)。

**UMI 戦略:**
1. **Phase 1**: Cortex-M4/M7 を ARM Architecture Reference Manual (D9) + CMSIS ヘッダ (D2) から手書き。`cortex_m4_core.hh` (U3 生成済み) を参考に umimmio 型で再実装。
2. **Phase 2**: 全 Cortex-M アーキテクチャ (M0/M0+/M3/M4/M7/M23/M33/M55/M85) をカバー。

**必要ツール:**
- CMSIS ヘッダからのコアペリフェラル定義抽出器 (modm の `stm_header.py` 方式: コンパイル実行)
- または: ARM Architecture Reference Manual からの手書き (アーキテクチャ数 ≤ 8 なので現実的)

#### RP2040/RP2350

| データ | 最適ソース | 抽出方法 | 自動化 |
|--------|----------|---------|--------|
| Cortex-M0+ コアペリフェラル | D6 Pico SDK `hardware_regs/` | SVD パース | ◎ |
| Cortex-M33 コアペリフェラル (RP2350) | D6 Pico SDK + D9 ARM Ref | SVD パース + 手動補完 | ○ |
| Hazard3 RISC-V CSR (RP2350) | D6 Pico SDK + RISC-V ISA Manual | **CSR は MMIO ではない** — 専用アクセス命令が必要 | △ |

**特記:** RP2350 のデュアル ISA (Cortex-M33 + RISC-V Hazard3) は、同一チップで 2 つの全く異なるコアペリフェラル体系を持つ。

#### ESP32-S3/P4

| データ | 最適ソース | 抽出方法 | 自動化 |
|--------|----------|---------|--------|
| Xtensa LX7 Special Registers | D7 ESP-IDF LL ヘッダ | ヘッダパース | △ |
| Interrupt Matrix レジスタ | D7 `interrupt_reg.h` | ヘッダパース | ○ |
| RISC-V CSR (ESP32-P4) | D7 ESP-IDF + RISC-V ISA Manual | CSR 専用 | △ |
| CLIC レジスタ (ESP32-P4) | D7 ESP-IDF | ヘッダパース | △ |

**特記:** Xtensa の Special Registers は MMIO ではなく RSR/WSR 命令でアクセスする。umimmio の Transport 抽象では直接表現できず、専用のアクセスラッパーが必要。

#### i.MX RT

| データ | 最適ソース | 抽出方法 | 自動化 |
|--------|----------|---------|--------|
| Cortex-M7 コアペリフェラル | D8 MCUXpresso SDK + D2 `core_cm7.h` | CMSIS ヘッダパース | ○ |
| L1 キャッシュ制御 | D9 ARM Ref + D2 `core_cm7.h` | 手動 + ヘッダ参照 | △ |

---

### 5.2 C2: コアシステム定義

constexpr 定数として表現される、コンパイル時に確定するシステム特性。

#### 全プラットフォーム共通

| データ | STM32 ソース | RP ソース | ESP ソース | iMXRT ソース | 自動化 |
|--------|------------|---------|----------|------------|--------|
| コアタイプ | D3 `.pdsc` `<processor Dcore="">` | D6 SDK 定義 | D7 `soc_caps.h` | D3/D8 | ◎ |
| FPU 有無 | D3 `.pdsc` `<processor Dfpu="">` | D6 SDK 定義 | D7 `soc_caps.h` | D3/D8 | ◎ |
| MPU 有無 | D3 `.pdsc` `<processor Dmpu="">` | D6 SDK 定義 | D7 `soc_caps.h` | D3/D8 | ◎ |
| NVIC 優先度ビット数 | D2 `__NVIC_PRIO_BITS` | D6 | — | D2/D8 | ◎ |
| TrustZone 有無 | D3 `.pdsc` `<processor Dtz="">` | D6 (RP2350 M33) | — | D3/D8 | ◎ |
| DSP 拡張 | D2 `__DSP_PRESENT` | D6 | — | D2/D8 | ◎ |
| キャッシュ構成 | D2/D10 | — | D7 `soc_caps.h` | D2/D10 | △ |
| コア数 | D3/D10 | D6 (2 コア) | D7 `soc_caps.h` | D3/D10 | ◎ |
| 最大動作周波数 | D4/D11 | D6/D11 | D7/D11 | D8/D11 | ◎ |

**既存事例:**
- **modm**: `stm_device_tree.py` で CubeMX DB + CMSIS-Pack から抽出。
- **embassy stm32-data**: `build/data/chips/*.json` の `"cores"` セクション。
- **probe-rs**: CMSIS-Pack から YAML 形式で抽出。

**必要ツール:**
- CMSIS-Pack パーサ (Cortex-M 全般) — `cmsis-pack-manager` (Python) で取得自動化
- ESP-IDF `soc_caps.h` パーサ — `#define SOC_CPU_CORES_NUM 2` 形式の簡易パース
- Pico SDK 定義抽出 — ヘッダ `#define` パース

---

### 5.3 C3: 割り込み / 例外ベクター

#### STM32

| データ | 最適ソース | 補完ソース | 自動化 |
|--------|----------|----------|--------|
| IRQ 番号 + ハンドラ名 | **D2** `stm32f407xx.h` の `IRQn_Type` enum | D1 SVD `<interrupt>` | ◎ |
| コア例外 (HardFault, SVC 等) | **D2** `core_cm4.h` | ARM Ref (D9) | ◎ — アーキテクチャ固定 |
| 割り込み優先度グルーピング | D2 `__NVIC_PRIO_BITS` | D3 `.pdsc` | ◎ |

**既存事例:**
- **umi_mmio**: `svd2cpp_vec.py` (U5) で SVD から IRQ 抽出 + コア例外ハードコード。**動作確認済み。**
- **modm**: `stm_header.py` で CMSIS ヘッダの `IRQn_Type` を**コンパイル実行**で抽出。
- **embassy stm32-data**: HAL ヘッダから IRQ 定義を抽出。
- **Rust cortex-m-rt**: PAC の `Interrupt` enum をベクターテーブルに展開。

**UMI 戦略:**
1. U5 `svd2cpp_vec.py` のロジックを再利用 (SVD の `<interrupt>` セクション)
2. CMSIS ヘッダの `IRQn_Type` でクロスバリデーション (ヘッダの方が正確)
3. コア例外はアーキテクチャごとにテンプレート化 (Cortex-M は共通)

#### RP2040/RP2350

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| M0+: NVIC 26 IRQ | D6 SVD `<interrupt>` | ◎ |
| M33: NVIC 52 IRQ (RP2350) | D6 SVD | ◎ |
| Hazard3: 外部割り込みテーブル (RP2350) | D6 SDK ヘッダ | ○ |

#### ESP32-S3/P4

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| Interrupt Matrix ソース (99+) | D7 `soc/interrupts.h` | ○ |
| CLIC 128+64 (ESP32-P4) | D7 ESP-IDF | △ |

**特記:** ESP32 の Interrupt Matrix は any-to-any マッピングであり、IRQ 番号が固定ではない。マッピングテーブルの定義が必要。

#### i.MX RT

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| NVIC 160 IRQ | D8 SVD / CMSIS ヘッダ | ◎ |

---

### 5.4 C4: メモリマップ

#### STM32

| データ | 最適ソース | 補完ソース | 自動化 |
|--------|----------|----------|--------|
| Flash base/size | **D3** `.pdsc` `<memory>` | D2 CMSIS ヘッダ `#define` | ◎ |
| SRAM base/size | **D3** `.pdsc` | D2 / D4 CubeMX | ◎ |
| CCM/DTCM/ITCM | D3 / D2 | D10 リファレンスマニュアル | ◎ |
| バックアップ SRAM | D2 `BKPSRAM_BASE` | D10 | ◎ |
| ペリフェラルバスリージョン | D1 SVD `<addressBlock>` | D2 | ◎ |
| ビットバンドリージョン | D2 `SRAM_BB_BASE` 等 | D9 ARM Ref | ◎ |

**既存事例:**
- **umi_mmio**: `cmsis-dev-extractor.py` (U4) でメモリレイアウト抽出。**動作確認済み。**
- **modm**: `stm_header.py` で `FLASH_BASE`, `SRAM1_BASE` 等を抽出。
- **probe-rs**: CMSIS-Pack からメモリリージョンを YAML に変換。

**必要ツール:**
- CMSIS-Pack パーサ (Phase 2)
- U4 の拡張 (現行は動作済み、MCU バリアント展開が必要)

#### RP2040/RP2350

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| SRAM (6 バンク: 4×64KB + 2×4KB) | D6 SVD / SDK `addressmap.h` | ◎ |
| XIP Flash (外部 QSPI) | D6 SDK | ◎ |
| ROM (16KB ブートローダ) | D6 SDK | ◎ |
| RP2350 追加リージョン (PSRAM, OTP) | D6 SDK | ◎ |

#### ESP32-S3/P4

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| Internal SRAM | D7 `soc/soc.h` | ○ |
| RTC SRAM | D7 `soc_caps.h` | ○ |
| External PSRAM/Flash (SPI) | D7 リンカスクリプト + `soc_caps.h` | ○ |
| DROM/IROM (キャッシュ) | D7 リンカスクリプト | △ |

**特記:** ESP32 のメモリマップは仮想アドレス空間 (キャッシュ経由) と物理アドレス空間の 2 層構造。リンカスクリプト (`memory.ld`) が最も正確なソース。

#### i.MX RT

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| FlexRAM (DTCM/ITCM/OCRAM 動的分割) | D8 SDK + D10 | △ |
| QSPI Flash (外部) | D8 SDK | ○ |
| SEMC SDRAM (外部) | D8 SDK | ○ |

**特記:** i.MX RT の FlexRAM は起動時に DTCM/ITCM/OCRAM の比率を動的に構成できる。メモリマップがアプリケーション設定に依存する唯一のプラットフォーム。

---

### 5.5 C5: ペリフェラルレジスタ (GPIO, UART, SPI, I2C, Timer, ADC, DMA 等)

**最大のカテゴリ。** SVD の主要対象領域だが、品質問題がある。

#### STM32

| データ | 最適ソース | 品質 | パッチ戦略 | 自動化 |
|--------|----------|------|----------|--------|
| ペリフェラルレジスタ全体 | **D1** SVD | ⚠️ 要パッチ | C1 stm32-rs YAML パッチ | ○ |
| 列挙値 (enumeratedValues) | D1 SVD (大半が欠落) | ❌ 不足 | C1 パッチ / 手動追加 | △ |
| IP バージョン管理 | **D4** CubeMX `<IP>` | ◎ | — | ◎ |

**SVD 品質問題の具体例 (stm32-rs 調査):**

| 問題 | 影響 | 対処 |
|------|------|------|
| 列挙値の欠落 | 大半のフィールドで `enumeratedValues` 未定義 | stm32-rs パッチで補完 |
| フィールド境界の不正確さ | ビット位置・幅がリファレンスマニュアルと不一致 | 個別パッチ |
| タイマーレジスタのバグ | 全 STM32 ファミリで共通のタイマー定義エラー | stm32-rs 共通パッチ |
| derivedFrom の不正確さ | ペリフェラルグループ内で実際には異なるレジスタ | 個別オーバーライド |
| 粒度の粗さ | 1 SVD が複数デバイスをカバー → 存在しないペリフェラルを含む | IP バージョンで検証 |

**既存事例:**
- **stm32-rs**: svdtools YAML パッチ → パッチ済み SVD → svd2rust。
- **embassy stm32-data**: SVD からレジスタブロック YAML を抽出 → 手動キュレーション → chiptool でコード生成。IP バージョン単位で管理 (SVD の MCU ファミリ単位より精密)。
- **modm**: SVD から独自レジスタ定義は生成 **しない** (CMSIS ヘッダを直接利用)。

**UMI 戦略:**
1. stm32-rs パッチ互換 (svdtools) でパッチ済み SVD を生成
2. U1/U2 svd2ral を umimmio API 向けにリファクタリングして C++ ヘッダ生成
3. 長期的には embassy 式の IP バージョン単位管理に移行

#### RP2040/RP2350

| データ | 最適ソース | 品質 | 自動化 |
|--------|----------|------|--------|
| 全ペリフェラルレジスタ | **D6** SVD (RP2040.svd / RP2350.svd) | ✅ 良好 | ◎ |
| PIO レジスタ | D6 SVD | ✅ | ◎ |

**特記:** RP の SVD は品質が高く、パッチなしで利用可能。Pico SDK のヘッダは SVD から自動生成されており、整合性が保証されている。

#### ESP32-S3/P4

| データ | 最適ソース | 品質 | 自動化 |
|--------|----------|------|--------|
| ペリフェラルレジスタ | D7 `soc/<periph>_reg.h` | ○ 概ね良好 | △ |
| SVD | D7 (最近追加) / C7 idf2svd 生成 | △ 不完全 | △ |

**特記:** ESP-IDF の SoC コンポーネントヘッダは手動 + 一部自動生成の混在。SVD は長らく存在せず、`idf2svd` でリファレンスマニュアルから生成されたものがコミュニティで使われてきた。最近 Espressif が公式 SVD をデバッグ用に追加。

**必要ツール:**
- ESP-IDF レジスタヘッダパーサ (`*_reg.h` の `#define` → umimmio 型変換)
- または idf2svd 生成 SVD → svd2ral パイプライン

#### i.MX RT

| データ | 最適ソース | 品質 | 自動化 |
|--------|----------|------|--------|
| 全ペリフェラルレジスタ | **D8** MCUXpresso SVD | ✅ 比較的良好 | ◎ |

**特記:** NXP の SVD は CMSIS 準拠度が高く、品質は比較的良好。

---

### 5.6 C6: GPIO ピンマルチプレクシング

**カテゴリ中で最もプラットフォーム差異が大きい。**

#### STM32 (AF テーブル方式)

| データ | 最適ソース | 補完ソース | 自動化 |
|--------|----------|----------|--------|
| ピン × AF 番号 × シグナル | **D5** STM32_open_pin_data (BSD-3) | D4 CubeMX DB (ST SLA) | ◎ |

**既存事例:**
- **modm**: CubeMX DB の `GPIO-*_Modes.xml` から XPath で抽出。
- **embassy stm32-data**: CubeMX DB から GPIO AF を JSON に変換。
- **cube-parse** (Rust): CubeMX DB パーサ。

**ライセンス上の注意:** CubeMX DB (D4) は ST SLA で再配布不可。`STM32_open_pin_data` (D5) は BSD-3-Clause だが、GPIO AF データのサブセットのみ。

**必要ツール:**
- D5 パーサ (XML → GPIO AF 中間表現) — 新規だが構造が単純
- D4 パーサ (全データが必要な場合) — cube-parse を参考

#### RP2040/RP2350 (FUNCSEL 方式)

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| GPIO → FUNCSEL マッピング | **D6** SDK `hardware_regs/io_bank0_regs.h` / SVD | ◎ |

**特記:** RP の GPIO は 3 層 (IO_BANK0 → PADS_BANK0 → SIO) で構成。各ピンの FUNCSEL (5-bit) でマルチプレクシングを制御する。SVD に完全に記述されている。

#### ESP32-S3/P4 (GPIO Matrix 方式)

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| GPIO Matrix (any-to-any) | **D7** `soc/gpio_sig_map.h` | ◎ |
| IO MUX 高速パス | D7 `soc/io_mux_reg.h` | ◎ |

**特記:** ESP32 の GPIO Matrix は任意のペリフェラル信号を任意の GPIO ピンにルーティングできる。`gpio_sig_map.h` にシグナル番号マッピングが定義されている。

#### i.MX RT (IOMUXC 方式)

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| IOMUXC ALT0-7 マッピング | **D8** MCUXpresso SDK ヘッダ | ○ |
| Daisy Chain (入力選択) | D8 SDK + D10 | △ |

**特記:** i.MX RT の IOMUXC は ALT0–ALT7 のマルチプレクシング + Daisy Chain (入力パス選択) の 2 段階構成。

---

### 5.7 C7: クロックツリー

**全カテゴリ中、自動化が最も困難。**

#### STM32

| データ | 最適ソース | 補完ソース | 自動化 |
|--------|----------|----------|--------|
| クロックソース (HSI/HSE/LSI/LSE) | D4 CubeMX / D1 SVD (RCC レジスタ) | D10 RM | △ |
| PLL 構成 (M/N/P/Q/R) | D4 CubeMX | D10 RM | △ |
| バスプリスケーラ (AHB/APB1/APB2) | D4 CubeMX | D1 SVD (RCC) | △ |
| ペリフェラルクロックイネーブル | D1 SVD (RCC_AHB1ENR 等) | D4 CubeMX | ○ |
| ペリフェラルリセット | D1 SVD (RCC_AHB1RSTR 等) | D4 CubeMX | ○ |
| クロック制約 (最大周波数等) | **D10** RM のみ | D11 データシート | ✎ |

**既存事例:**
- **modm**: クロックツリー抽出は「概念実証段階」。CubeMX 内部データにクロックツリー XML があるが、完全自動化に至っていない。
- **embassy stm32-data**: `PeripheralRccRegister` 構造体で RCC イネーブル/リセットビットのレジスタオフセット＋ビット位置を管理。`ClockGen` で多段マルチプレクサを自動生成。**最も成熟した実装。**
- **STM32CubeMX**: GUI でクロックツリーを構成し、初期化コードを生成 (C 関数)。

**UMI 戦略:**
1. **Phase 1**: ペリフェラルクロックイネーブル/リセットビットのみ (SVD の RCC レジスタから自動抽出)
2. **Phase 2**: PLL + バスプリスケーラの宣言的定義 (constexpr 制約モデル)
3. **Phase 3**: embassy 式の ClockGen 自動生成

**必要ツール:**
- RCC レジスタの自動分類器 (イネーブル/リセット/選択レジスタの判別)
- クロック制約モデルの宣言的記述フォーマット定義

#### RP2040/RP2350

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| XOSC/ROSC 構成 | D6 SDK `hardware_clocks` | △ |
| PLL_SYS / PLL_USB 構成 | D6 SVD (PLL レジスタ) | ○ |
| CLK_REF / CLK_SYS / CLK_PERI 等 | D6 SDK | △ |
| RESETS レジスタ | D6 SVD | ◎ |

**特記:** RP のクロックツリーは STM32 より単純。PLL 2 個 + クロックマルチプレクサの 2 段構成。

#### ESP32-S3/P4

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| XTAL → PLL → 個別分周 | D7 `soc/clk_tree_defs.h` | △ |
| ペリフェラル個別クロック設定 | D7 `soc_caps.h` + LL 関数 | △ |
| LP (Low Power) クロック | D7 | △ |

#### i.MX RT

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| CCM (Clock Controller Module) | D8 SVD (CCM レジスタ) | ○ |
| ARM PLL / SYS PLL / USB PLL | D8 SVD + D10 | △ |
| 多段マルチプレクサ (CBCDR/CBCMR/CSCMR 等) | D10 RM | ✎ |

---

### 5.8 C8: DMA マッピング

#### STM32

| データ | 最適ソース | 補完ソース | 自動化 |
|--------|----------|----------|--------|
| 固定テーブル (F4: DMA1/2 × Stream × Channel) | **D4** CubeMX DB | D10 RM 表 | △ |
| DMAMUX (H7/G4/L4+) | D1 SVD (DMAMUX レジスタ) | D10 RM | ○ |
| リクエストマッピング (DMAMUX) | D4 CubeMX | D10 RM | △ |
| バースト/FIFO 構成 | D1 SVD (DMA レジスタ) | D10 RM | ◎ |

**既存事例:**
- **modm**: CubeMX DB の DMA 定義から DeviceTree に抽出。
- **embassy stm32-data**: DMA チャネル割り当てを JSON で管理。

**必要ツール:**
- DMA マッピングテーブルパーサ (CubeMX DB or リファレンスマニュアルから)
- DMAMUX リクエスト番号の自動抽出

#### RP2040/RP2350

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| 12ch DREQ マッピング | **D6** SVD / データシート Table 120 | ◎ |

#### ESP32-S3/P4

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| GDMA ダイナミックマッピング | D7 `gdma_channel.h` | △ |

#### i.MX RT

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| eDMA + DMAMUX | D8 SVD (eDMA/DMAMUX レジスタ) | ○ |
| リクエストソース | D8 SDK `MIMXRT1062_dmamux.h` | ◎ |

---

### 5.9 C9: 電力管理

**主にリファレンスマニュアルからの手動キュレーションが必要。**

#### 全プラットフォーム

| データ | ソース | 自動化 |
|--------|--------|--------|
| スリープモード定義 | D10 RM | ✎ |
| ウェイクアップソース | D10 RM | ✎ |
| 電源ドメイン構成 | D10 RM | ✎ |
| 電圧レギュレータモード | D10 RM | ✎ |
| ウェイクアップレイテンシ | D10 RM / D11 データシート | ✎ |
| 消費電流値 | D11 データシート | ✎ |
| PWR/PMU レジスタ定義 | D1 SVD (PWR ペリフェラル) | ○ |
| LP ペリフェラル一覧 | D10 RM | ✎ |

**既存事例:**
- **Zephyr**: Devicetree の `power-domain` バインディングで電源ドメインを記述。
- **modm**: 電力管理のデータ自動抽出は実装していない。
- **ESP-IDF**: `soc_caps.h` に `SOC_PM_SUPPORT_*` で対応モードを記述。

**UMI 戦略:**
1. PWR/PMU レジスタ定義は SVD から自動生成 (C5 の一部として)
2. スリープモード遷移・ウェイクアップソースは手動で constexpr テーブルとして定義
3. Zephyr の Devicetree power-domain バインディングを参考にしたデータフォーマット検討

**必要ツール:** 特になし (手動定義が主体)。長期的にはリファレンスマニュアル PDF からの AI 支援抽出。

---

### 5.10 C10: セキュリティ / 保護

**リファレンスマニュアルからの手動キュレーションが主体。レジスタ定義部分のみ自動化可能。**

| データ | ソース | 自動化 |
|--------|--------|--------|
| フラッシュ保護レジスタ (RDP/WRP/PCROP) | D1 SVD (FLASH ペリフェラル) | ○ |
| TrustZone SAU/IDAU 構成 | D2 `partition_<device>.h` + D9 | △ |
| OTP/eFuse レイアウト | D10 RM / D7 ESP-IDF | ✎ |
| セキュアブートシーケンス | D10 RM | ✎ |
| 暗号エンジンレジスタ (AES/SHA/RNG) | D1 SVD | ○ |
| JTAG/SWD 保護設定 | D10 RM | ✎ |
| HAB (i.MX RT) | D10 RM + NXP AppNote | ✎ |

**不可逆操作の警告:**
- STM32 RDP Level 2 (永久ロック)
- ESP32 eFuse 書き込み (一度のみ)
- i.MX RT HAB Close (永久ロック)

**UMI 戦略:**
1. 暗号エンジン等のレジスタ定義は SVD から自動生成 (C5 の一部)
2. 保護レベル定義・OTP レイアウトは手動で constexpr として定義
3. 不可逆操作には `@warning` Doxygen コメントと `[[deprecated("IRREVERSIBLE")]]` 属性を付与

**必要ツール:** 特になし。

---

### 5.11 C11: デバイスメタデータ

#### STM32

| データ | 最適ソース | 補完ソース | 自動化 |
|--------|----------|----------|--------|
| パーツナンバー | **D3** `.pdsc` / **D4** CubeMX `families.xml` | — | ◎ |
| ファミリ / サブファミリ | D3/D4 | — | ◎ |
| パッケージ (LQFP64 等) | D4 CubeMX | D11 データシート | ◎ |
| ピン数 | D4 CubeMX | D11 | ◎ |
| Flash / SRAM サイズ | D3 `.pdsc` / D2 CMSIS ヘッダ | D4 | ◎ |
| 温度範囲 | D4 CubeMX | D11 | ◎ |
| 最大周波数 | D4 CubeMX | D11 | ◎ |
| ペリフェラルインスタンス有無 | D4 CubeMX `<IP>` | D1 SVD | ◎ |
| シリコンリビジョン (DBGMCU) | D1 SVD / D2 ヘッダ | D10 Errata | ○ |

**既存事例:**
- **modm-devices**: 1,171 STM32 デバイスを 106 ファイルにロスレスマージ。CubeMX DB から全メタデータを抽出。
- **embassy stm32-data**: `build/data/chips/*.json` に 1,400+ チップの完全メタデータ。
- **PlatformIO**: `boards/*.json` に MCU、フレームワーク、アップロード設定を記述。

**必要ツール:**
- CMSIS-Pack パーサ (デバイスリスト + メモリ + プロセッサ特性)
- CubeMX DB パーサ (全デバイスバリアントのメタデータ)

#### RP2040/RP2350

少数バリアント (RP2040, RP2350A, RP2350B)。手動定義で十分。

#### ESP32-S3/P4

少数バリアント。`soc_caps.h` (D7) から自動抽出可能。

#### i.MX RT

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| バリアント情報 | D8 MCUXpresso SDK | ◎ |
| FlexRAM 構成オプション | D10 RM | △ |

---

### 5.12 C12: リンカ / スタートアップ

#### STM32

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| メモリリージョン (FLASH/SRAM/CCM) | C4 メモリマップから派生 | ◎ |
| セクション配置 | CMSIS テンプレート | ◎ — テンプレート化可能 |
| スタック / ヒープサイズ | アプリケーション設定 | — (ユーザー指定) |
| ベクターテーブル初期配置 | C3 ベクターから派生 | ◎ |
| C ランタイム初期化 (.data コピー, .bss ゼロ) | CMSIS 標準 | ◎ — アーキテクチャ共通 |

**既存事例:**
- **modm**: DeviceTree → Jinja2 テンプレート → リンカスクリプト + スタートアップ .S を生成。
- **embassy**: stm32-metapac の `memory.x` を生成。
- **umi_mmio**: `device-yaml2cpp.py` (U6) で YAML → C++ メモリレイアウト。

**UMI 戦略:**
C4 メモリマップ + C3 ベクター定義から、Jinja2 テンプレートでリンカスクリプトとスタートアップコードを自動生成。

#### RP2040/RP2350

| データ | 特記事項 | 自動化 |
|--------|---------|--------|
| Boot2 (XIP 初期化) | Flash チップ依存の 256B ブートローダ | △ — Flash チップごとにバイナリ提供 |
| メモリリージョン | C4 から派生 | ◎ |

**特記:** RP2040 は Boot2 (256B の XIP 初期化コード) が必要。Pico SDK にはいくつかの Flash チップ向けの Boot2 バイナリが含まれる。

#### ESP32-S3/P4

| データ | 特記事項 | 自動化 |
|--------|---------|--------|
| 2 段階ブート (ROM bootloader → 2nd stage) | Espressif 固有のブートシーケンス | △ |
| パーティションテーブル | アプリケーション設定 | — |
| リンカスクリプト | ESP-IDF 提供のテンプレートベース | △ |

**特記:** ESP32 は独自のブートローダシーケンスを持ち、CMSIS 標準とは全く異なる。ESP-IDF のリンカスクリプトテンプレートを参考にする必要がある。

#### i.MX RT

| データ | 特記事項 | 自動化 |
|--------|---------|--------|
| IVT (Image Vector Table) | NXP 独自の起動構造 | △ |
| FlexRAM 構成 | アプリケーション設定に依存 | — |
| DCD (Device Configuration Data) | SDRAM 初期化シーケンス等 | ✎ |

---

### 5.13 C13: デバッグ / トレース

#### STM32

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| SWD/JTAG ピン割り当て | D5 / D4 / D10 | ◎ |
| DWT レジスタ定義 | C1 コアペリフェラルから派生 | ◎ |
| ITM レジスタ定義 | C1 コアペリフェラルから派生 | ◎ |
| TPIU レジスタ定義 | C1 コアペリフェラルから派生 | ◎ |
| ETM レジスタ定義 | D9 ARM Ref + D3 `.pdsc` CoreSight | △ |
| SWO ピン/クロック設定 | D10 RM | △ |
| CoreSight コンポーネント構成 | **D3** `.pdsc` デバッグシーケンス | ○ |
| DBGMCU レジスタ | D1 SVD | ◎ |

**既存事例:**
- **probe-rs**: CMSIS-Pack からデバッグシーケンス、CoreSight 構成、メモリマップを YAML に変換。
- **OpenOCD**: TCL 形式のターゲット設定にメモリマップ、Flash アルゴリズム、デバッグインターフェースを記述。
- **pyOCD**: CMSIS-Pack を直接利用してデバッグ/フラッシュ書き込み。

**UMI 戦略:**
1. DWT/ITM/TPIU レジスタは C1 コアペリフェラルの一部として umimmio 型で定義
2. SWO/ETM の構成はデバッグツール (pyOCD/OpenOCD) に委譲
3. CoreSight トポロジーは CMSIS-Pack から参照

#### RP2040/RP2350

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| デュアルコア SWD (Rescue DP) | D6 SDK + D10 | △ |
| DWT (M33 のみ) | D6 SVD | ◎ |

#### ESP32-S3/P4

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| USB-Serial-JTAG | D7 ESP-IDF | △ |
| JTAG ピン構成 | D7 `soc_caps.h` | ○ |
| Trace Memory (ESP32-P4) | D7 ESP-IDF + D10 | ✎ |

#### i.MX RT

| データ | 最適ソース | 自動化 |
|--------|----------|--------|
| SWD + ETM (高帯域トレース) | D3 `.pdsc` + D9 | ○ |
| CoreSight 構成 (SoC Integrator) | D3 `.pdsc` | ○ |

---

## 6. 必要なツール / パーサの全体像

### 6.1 ツール一覧

| # | ツール名 | 入力 | 出力 | Phase | 既存資産 |
|---|---------|------|------|-------|---------|
| T1 | **SVD パーサ + パッチ適用** | SVD + YAML パッチ | 中間表現 JSON | 1 | U1/U2 svd2ral リファクタリング + svdtools 連携 |
| T2 | **CMSIS ヘッダ抽出器** | `stm32f407xx.h` + `core_cm4.h` | 中間表現 JSON | 1 | U3/U4 の発展形。**modm 方式 (コンパイル実行) を採用** |
| T3 | **CMSIS-Pack パーサ** | `.pdsc` XML | メモリマップ / プロセッサ特性 JSON | 2 | 新規。`cmsis-pack-manager` (Python) 活用 |
| T4 | **Open Pin Data パーサ** | STM32_open_pin_data XML | GPIO AF JSON | 2 | 新規。構造が単純で実装容易 |
| T5 | **umimmio コード生成器** | 中間表現 JSON | C++ ヘッダ (umimmio 型) | 1 | U1 Jinja2 テンプレート方式を踏襲 |
| T6 | **ベクターテーブル生成器** | 中間表現 JSON | C++ ベクターヘッダ + IRQn enum | 1 | U5 svd2cpp_vec ベース |
| T7 | **リンカスクリプト生成器** | メモリマップ JSON | `.ld` ファイル | 2 | 新規。Jinja2 テンプレート |
| T8 | **ESP-IDF SoC パーサ** | `soc_caps.h` + `*_reg.h` | 中間表現 JSON | 3 | 新規 |
| T9 | **Pico SDK パーサ** | SVD + SDK ヘッダ | 中間表現 JSON | 3 | T1 SVD パーサで対応可能 |
| T10 | **MCUXpresso パーサ** | SVD + SDK ヘッダ | 中間表現 JSON | 4 | T1 SVD パーサで対応可能 |

### 6.2 ツール間の依存関係

```
Phase 1 (基盤):
  T1 (SVD パーサ) ──┐
  T2 (CMSIS 抽出) ──┤→ 中間表現 JSON → T5 (コード生成) → umimmio C++ ヘッダ
                    │                 → T6 (ベクター生成) → ベクターヘッダ
                    │
Phase 2 (拡張):     │
  T3 (CMSIS-Pack) ──┤→ 中間表現 JSON → T7 (リンカ生成) → リンカスクリプト
  T4 (Open Pin) ────┘                → T5 (GPIO AF) → GPIO AF ヘッダ

Phase 3 (マルチプラットフォーム):
  T8 (ESP-IDF) ─────┐
  T9 (Pico SDK) ────┤→ 中間表現 JSON → T5/T6/T7
  T10 (MCUXpresso) ─┘
```

---

## 7. 自動化可能性の総括

### 7.1 完全自動化可能 (ツール実装のみ)

| カテゴリ | データ | 条件 |
|---------|--------|------|
| C2 | コアシステム定義 | CMSIS-Pack / SDK からの抽出 |
| C3 | 割り込みベクター | CMSIS ヘッダ / SVD からの抽出 |
| C4 | メモリマップ | CMSIS-Pack / SVD / SDK からの抽出 |
| C5 | ペリフェラルレジスタ (RP/iMXRT) | SVD 品質が良好なプラットフォーム |
| C6 | GPIO MUX (RP/ESP) | SDK / SVD に完全記述 |
| C11 | デバイスメタデータ | CMSIS-Pack / CubeMX DB からの抽出 |
| C12 | リンカ/スタートアップ (Cortex-M 標準) | C3+C4 からの派生生成 |

### 7.2 自動化 + パッチ/キュレーション必要

| カテゴリ | データ | 必要な追加作業 |
|---------|--------|-------------|
| C1 | コアペリフェラルレジスタ | アーキテクチャ数 (≤8) 分の手書き or AI 生成 |
| C5 | ペリフェラルレジスタ (STM32) | stm32-rs パッチ適用が前提 |
| C6 | GPIO MUX (STM32) | Open Pin Data パーサ開発 |
| C7 | クロックツリー (イネーブル/リセット) | SVD の RCC レジスタから自動抽出可能 |
| C8 | DMA マッピング (DMAMUX 搭載) | SVD + CubeMX DB からの複合抽出 |
| C13 | デバッグ/トレース | CMSIS-Pack CoreSight + コアペリフェラル |

### 7.3 手動キュレーション主体

| カテゴリ | データ | 理由 |
|---------|--------|------|
| C7 | クロックツリー (制約/周波数上限) | 複雑な依存関係、アプリケーション要件依存 |
| C8 | DMA マッピング (固定テーブル型) | RM テーブルからの転記が必要 |
| C9 | 電力管理 | スリープモード遷移、ウェイクアップレイテンシ等 |
| C10 | セキュリティ/保護 | OTP レイアウト、セキュアブートシーケンス等 |
| C12 | リンカ/スタートアップ (ESP/iMXRT 固有) | 独自ブートシーケンス |

---

## 8. 既存事例から学ぶべき教訓

### 8.1 成功パターン

| プロジェクト | 成功要因 | UMI への適用 |
|------------|---------|-------------|
| **stm32-rs** | YAML パッチフォーマットの標準化とコミュニティ維持 | svdtools 互換パッチを採用 (判断済み: 03_ARCHITECTURE §3.3) |
| **embassy stm32-data** | 複数ソース (CubeMX + SVD + HAL ヘッダ) の統合と IP バージョン管理 | 中間表現 JSON の設計に反映 (07_HW_DATA_PIPELINE §4.4) |
| **modm** | 「コンパイルして実行」による CMSIS ヘッダ抽出 | T2 ツールの実装方式として採用 |
| **modm** | ロスレスマージによる 1,171 デバイスの 106 ファイル圧縮 | 将来のマルチデバイス展開時に参考 |
| **Rust cortex-m** | コアペリフェラルの手書き (アーキテクチャ数が少ないため現実的) | C1 Phase 1 の戦略として採用 |

### 8.2 失敗パターンと回避策

| 失敗パターン | 該当プロジェクト | 回避策 |
|------------|---------------|--------|
| SVD を無条件に信頼 | 多数 | パッチインフラ必須 + 複数ソースクロスバリデーション |
| CMSIS ヘッダのテキスト処理 | umi_mmio cmsis2svd | modm 方式 (コンパイル実行) に移行 |
| 巨大単一ヘッダ生成 | svd2rust (i.MX RT: 932K LOC) | ペリフェラルごとの個別ヘッダ生成 |
| 完璧なパイプラインの先行構築 | — | 手書き先行 → 段階的に生成移行 |
| CubeMX DB のライセンス問題 | modm | Open Pin Data (BSD-3) で代替 + ローカル実行による抽出 |

---

## 9. プラットフォーム固有のデータソース詳細

### 9.1 STM32 固有

#### CubeMX DB の内部構造

```
STM32CubeMX/db/mcu/
├── families.xml                    ← ファミリ/サブファミリ分類
├── STM32F407V(E-G)Tx.xml          ← MCU 定義 (RAM, Flash, I/O, 周波数, パッケージ)
├── IP/
│   ├── GPIO-STM32F401C_gpio_v2_0_Modes.xml  ← GPIO AF マッピング
│   ├── DMA-STM32F405_dma_v2_0_Modes.xml     ← DMA マッピング
│   ├── RCC-STM32F405_rcc_v1_0_Modes.xml     ← クロック設定
│   └── ...
└── plugins/boardmanager/
    └── ...
```

**格納場所:**
- Windows: `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX\db\mcu`
- Linux: `~/.stm32cubemx/repository/`
- macOS: `~/STM32CubeMX.app/Contents/Resources/db/mcu` (バージョン依存)

**コミュニティミラー:** `github.com/stm32-rs/cube-MX-db`

#### STM32_open_pin_data の構造

```
STM32_open_pin_data/
├── mcu/
│   ├── STM32F407V(E-G)Tx.xml    ← ピン定義 + AF マッピング
│   └── ...
└── plugins/
    └── ...
```

**ライセンス:** BSD-3-Clause — **再配布可能、リポジトリに含められる**。

### 9.2 RP2040/RP2350 固有

```
pico-sdk/
├── src/rp2040/hardware_regs/
│   ├── include/hardware/regs/
│   │   ├── io_bank0.h          ← GPIO FUNCSEL 定義
│   │   ├── clocks.h            ← クロック制御
│   │   ├── pll.h               ← PLL 設定
│   │   └── ...
│   └── RP2040.svd              ← 完全な SVD
├── src/rp2350/hardware_regs/
│   ├── include/hardware/regs/
│   │   └── ...
│   └── RP2350.svd              ← 完全な SVD
└── src/common/
    └── ...
```

### 9.3 ESP-IDF SoC コンポーネント固有

```
esp-idf/components/soc/
├── esp32s3/
│   ├── include/soc/
│   │   ├── soc_caps.h          ← ハードウェア機能フラグ (#define SOC_*)
│   │   ├── gpio_sig_map.h      ← GPIO Matrix シグナル番号
│   │   ├── io_mux_reg.h        ← IO MUX レジスタ
│   │   ├── uart_reg.h          ← UART レジスタ定義
│   │   ├── interrupts.h        ← 割り込みソース定義
│   │   └── ...
│   └── ld/
│       ├── esp32s3.peripherals.ld  ← ペリフェラルアドレスマップ
│       └── memory.ld               ← メモリリージョン定義
├── esp32p4/
│   └── ...
└── include/soc/
    └── ...                     ← SoC 共通インターフェース
```

### 9.4 MCUXpresso SDK 固有 (i.MX RT)

```
SDK_2.x.x_MIMXRT1060-EVK/
├── devices/MIMXRT1062/
│   ├── MIMXRT1062.h            ← CMSIS 準拠デバイスヘッダ
│   ├── MIMXRT1062.svd          ← SVD ファイル
│   ├── MIMXRT1062_features.h   ← フィーチャーフラグ
│   ├── fsl_device_registers.h  ← レジスタアクセスマクロ
│   ├── drivers/
│   │   ├── fsl_clock.c         ← クロック実装
│   │   ├── fsl_iomuxc.h        ← IOMUXC 定義 (GPIO MUX)
│   │   └── ...
│   └── system_MIMXRT1062.c     ← システム初期化
└── ...
```

---

## 10. 参考プロジェクト・ツール一覧

### 10.1 データソースリポジトリ

| プロジェクト | URL | 用途 |
|-------------|-----|------|
| cmsis-svd-data | `github.com/cmsis-svd/cmsis-svd-data` | 全ベンダー SVD の集約 |
| cmsis-svd-stm32 | `github.com/modm-io/cmsis-svd-stm32` | STM32 SVD (Apache-2.0) |
| cmsis-header-stm32 | `github.com/modm-io/cmsis-header-stm32` | CMSIS ヘッダ集 |
| STM32_open_pin_data | `github.com/STMicroelectronics/STM32_open_pin_data` | GPIO AF (BSD-3) |
| pico-sdk | `github.com/raspberrypi/pico-sdk` | RP2040/RP2350 SVD + SDK |
| esp-idf | `github.com/espressif/esp-idf` | ESP32 SoC コンポーネント |

### 10.2 パッチ / キュレーションプロジェクト

| プロジェクト | URL | 用途 |
|-------------|-----|------|
| stm32-rs | `github.com/stm32-rs/stm32-rs` | SVD パッチ (YAML) |
| svdtools | `github.com/stm32-rs/svdtools` | SVD パッチ適用エンジン |
| stm32-data | `github.com/embassy-rs/stm32-data` | 複数ソース統合 DB |
| modm-devices | `github.com/modm-io/modm-devices` | DFG パイプライン |
| modm-data | `github.com/modm-io/modm-data` | PDF データ抽出 |

### 10.3 コード生成ツール

| ツール | URL | 言語 | 用途 |
|--------|-----|------|------|
| svd2rust | `github.com/rust-embedded/svd2rust` | Rust | SVD → Rust PAC |
| chiptool | `github.com/embassy-rs/chiptool` | Rust | SVD/YAML → Rust PAC (embassy 式) |
| SVDConv | ARM CMSIS Utilities | C++ | SVD → C ヘッダ (公式) |
| cube-parse | `github.com/stm32-rs/cube-parse` | Rust | CubeMX DB パーサ |
| idf2svd | `github.com/espressif/idf2svd` | Rust | ESP-IDF → SVD 変換 |
| atdf2svd | community | Rust | Atmel ATDF → SVD 変換 |
| tixml2svd | `github.com/dhoove/tixml2svd` | Python | TI XML → SVD 変換 |

### 10.4 デバッグ / プログラミングツール (データソースとして)

| ツール | URL | 参照データ |
|--------|-----|----------|
| probe-rs | `probe.rs` | CMSIS-Pack → YAML ターゲット記述 |
| pyOCD | `pyocd.io` | CMSIS-Pack 直接利用 |
| OpenOCD | `openocd.org` | TCL ターゲット設定 (メモリマップ, Flash) |

### 10.5 RTOS / フレームワーク (参考実装)

| プロジェクト | URL | 参考ポイント |
|-------------|-----|-------------|
| Zephyr | `zephyrproject.org` | Devicetree によるハードウェア記述 |
| Tock OS | `tockos.org` | tock-registers (Rust レジスタ抽象化) |
| Embassy | `embassy.dev` | stm32-data 統合パイプライン |
| modm | `modm.io` | DFG + lbuild + Jinja2 HAL 生成 |

---

## 11. umi_mmio 既存資産の詳細

07_HW_DATA_PIPELINE §3.1 で分析済みだが、本ドキュメントのコンテキストで再整理する。

| 資産 | 入力 | 出力 | 再利用可能性 | 再利用方法 |
|------|------|------|-------------|----------|
| **svd2ral v1** (Jinja2) | SVD | C++ ヘッダ (旧 API) | ★★★★ | パーサ/バリデータ/テンプレートエンジンを umimmio API 向けにリファクタリング |
| **svd2ral v2** (直接生成) | SVD | C++ ヘッダ (旧 API) | ★★★ | ペリフェラルグループ検出/配列展開ロジックの流用 |
| **cmsis2svd** | CMSIS ヘッダ | SVD XML | ★★ | テキスト処理方式は廃止。modm 方式に移行 |
| **cmsis-dev-extractor** | CMSIS ヘッダ | C++ constexpr | ★★★★ | メモリレイアウト/デバイス構成抽出は直接再利用可能 |
| **svd2cpp_vec** | SVD | C++ ベクター | ★★★★ | IRQ 抽出ロジックは直接再利用可能 |
| **patch_svd** | SVD | パッチ済み SVD | ★★ | svdtools に移行 |
| **validate_svd** | SVD | 検証結果 | ★★★ | バリデーション機能として流用 |
| **device-yaml2cpp** | YAML | C++ constexpr | ★★★ | 中間表現 JSON → C++ 変換の参考 |
