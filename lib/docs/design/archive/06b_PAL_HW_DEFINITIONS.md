# 06b. PAL ハードウェア定義の分類と要件

> **分割・移動済み**: 本ドキュメントの内容は `pal/` ディレクトリに分割・拡充されました。
>
> - [../pal/00_OVERVIEW.md](../pal/00_OVERVIEW.md) — PAL ドキュメント全体マップ
> - [../pal/01_LAYER_MODEL.md](../pal/01_LAYER_MODEL.md) — 4 層モデル
> - [../pal/02_CATEGORY_INDEX.md](../pal/02_CATEGORY_INDEX.md) — C1–C14 カテゴリ一覧 + 完全性チェックリスト
> - [../pal/categories/](../pal/categories/) — 各カテゴリの詳細 + 生成ヘッダのコード例

**ステータス:** pal/ に移行済み
**関連文書:**
- [../pal/03_ARCHITECTURE.md](../pal/03_ARCHITECTURE.md) — PAL アーキテクチャ提案
- [../pal/04_ANALYSIS.md](../pal/04_ANALYSIS.md) — 既存 PAL アプローチの横断分析
- [../pipeline/hw_data_pipeline.md](../pipeline/hw_data_pipeline.md) — HW 定義データ統合パイプライン

> **命名変更**: RAL (Register Access Layer) → **PAL (Peripheral Access Layer)** に統一。

---

## 1. 本ドキュメントの目的

PAL が生成・提供すべきハードウェア定義の **完全な分類** を確立する。

データソースの調達方法や生成パイプラインの設計（→ ../pipeline/hw_data_pipeline.md の役割）ではなく、
**「何が必要か」の全体像** を明確にすることが目的である。

対象プラットフォーム:
1. **STM32** (Cortex-M) — 最優先。最も広範なペリフェラルバリエーション
2. **RP2040 / RP2350** — Raspberry Pi Pico。PIO, 3 層 GPIO, デュアルコア
3. **ESP32-S3 / ESP32-P4** — Espressif。GPIO Matrix, HP/LP サブシステム
4. **i.MX RT** (Cortex-M7) — NXP。IOMUXC, FlexRAM, 高性能リアルタイム

---

## 2. ハードウェア定義の 4 層モデル

ハードウェア定義は、その適用範囲（スコープ）に基づいて 4 層に分類できる。

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: アーキテクチャ共通 (Architecture-Universal)         │
│   全 Cortex-M / 全 RISC-V / 全 Xtensa に共通               │
├─────────────────────────────────────────────────────────────┤
│ Layer 2: コアプロファイル固有 (Core-Profile-Specific)        │
│   Cortex-M4F / Cortex-M33 / RISC-V RV32IMAC 等に固有       │
├─────────────────────────────────────────────────────────────┤
│ Layer 3: MCU ファミリ固有 (MCU-Family-Specific)             │
│   STM32F4xx / RP2040 / ESP32-S3 等のシリーズ全体に共通      │
├─────────────────────────────────────────────────────────────┤
│ Layer 4: デバイスバリアント固有 (Device-Variant-Specific)    │
│   STM32F407VGT6 / RP2040-B2 等の個別チップ固有              │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 定義カテゴリ一覧

### 3.1 全体マトリクス

| # | カテゴリ | 概要 | 所属レイヤ | 詳細セクション |
|---|---------|------|-----------|---------------|
| C1 | コアペリフェラルレジスタ | NVIC, SCB, SysTick 等 | L1–L2 | §4.1 |
| C2 | コアシステム定義 | コアタイプ, FPU/MPU/TrustZone 有無 | L2 | §4.2 |
| C3 | 割り込み / 例外ベクター | IRQ 番号, ハンドラ名, コア例外 | L1 + L3 | §4.3 |
| C4 | メモリマップ | Flash, SRAM, CCM 等の base / size | L3–L4 | §4.4 |
| C5 | ペリフェラルレジスタ | GPIO, UART, SPI, DMA 等のレジスタ定義 | L3 | §4.5 |
| C6 | GPIO ピンマルチプレクシング | AF / FUNCSEL / GPIO Matrix マッピング | L3–L4 | §4.6 |
| C7 | クロックツリー | PLL, バスプリスケーラ, ペリフェラルイネーブル | L3 | §4.7 |
| C8 | DMA マッピング | ストリーム/チャネル × ペリフェラルリクエスト | L3–L4 | §4.8 |
| C9 | 電力管理 | 電源ドメイン, スリープモード, LP ペリフェラル | L3 | §4.9 |
| C10 | セキュリティ / 保護 | TrustZone SAU/IDAU, Flash 保護 | L2–L3 | §4.10 |
| C11 | デバイスメタデータ | パッケージ, 温度範囲, シリコンリビジョン | L4 | §4.11 |
| C12 | リンカ / スタートアップ | メモリレイアウト, スタック初期化, C ランタイム | L1–L4 | §4.12 |
| C13 | デバッグ / トレース | SWD/JTAG, ITM, DWT, ETM | L1–L2 | §4.13 |

---

## 4. 各カテゴリの詳細

### 4.1 C1: コアペリフェラルレジスタ

コア自体に統合されたペリフェラルのレジスタ定義。ISA とコアプロファイルで決まる。

#### ARM Cortex-M 共通

| ペリフェラル | 概要 | 所属レイヤ |
|-------------|------|-----------|
| **NVIC** | Nested Vectored Interrupt Controller — ISER/ICER/ISPR/ICPR/IABR/IP + STIR | L1 (全 Cortex-M) |
| **SCB** | System Control Block — CPUID, ICSR, VTOR, AIRCR, SCR, CCR, SHCSR, CFSR | L1 |
| **SysTick** | System Timer — CTRL, LOAD, VAL, CALIB | L1 |
| **MPU** | Memory Protection Unit — TYPE, CTRL, RNR, RBAR, RASR | L2 (MPU 搭載コアのみ) |
| **FPU** | Floating Point Unit — FPCCR, FPCAR, FPDSCR, CPACR (CP10/CP11) | L2 (M4F/M7/M33 等) |
| **DWT** | Data Watchpoint and Trace — CTRL (CYCCNTENA), CYCCNT, COMPn, MASKn, FUNCTIONn | L2 (DWT 搭載コアのみ) |
| **ITM** | Instrumentation Trace Macrocell — STIM[0..31], TER, TPR, TCR, LAR | L2 |
| **TPI** | Trace Port Interface — SSPSR, CSPSR, ACPR, SPPR, FFSR, FFCR | L2 |
| **CoreDebug** | Debug Halting Control — DHCSR, DCRSR, DCRDR, DEMCR | L2 |
| **SAU** | Security Attribution Unit — CTRL, TYPE, RNR, RBAR, RLAR | L2 (ARMv8-M TrustZone) |

#### RISC-V (RP2350 Hazard3, ESP32-C3/C6, ESP32-P4)

| 定義 | 概要 | 所属レイヤ |
|------|------|-----------|
| **CSR** | Control and Status Registers — mstatus, mie, mtvec, mepc, mcause, mip 等 | L1 (全 RISC-V) |
| **PLIC / CLIC** | Platform-Level / Core-Local Interrupt Controller | L2 (実装依存) |
| **mtime / mtimecmp** | Machine Timer | L1 |
| **PMP** | Physical Memory Protection — pmpcfgN, pmpaddrN | L2 (PMP 搭載コアのみ) |

#### Xtensa (ESP32-S3)

| 定義 | 概要 | 所属レイヤ |
|------|------|-----------|
| **Special Registers** | PS, EPC1-7, EXCSAVE1-7, INTENABLE, INTERRUPT, CCOUNT, CCOMPARE | L1 |
| **Window Registers** | WindowBase, WindowStart（レジスタウィンドウ制御） | L1 |
| **Interrupt Matrix** | レベル / エッジ割り込み、NMI — 専用コントローラ | L3 (ESP 固有) |

---

### 4.2 C2: コアシステム定義

ビルド条件分岐に使われるコア属性。

| 定義項目 | 例 | プラットフォーム差異 |
|---------|-----|-------------------|
| コアアーキテクチャ | Cortex-M4, Cortex-M7, RISC-V RV32IMAC, Xtensa LX7 | 全プラットフォーム |
| FPU タイプ | なし / FPv4-SP-D16 / FPv5-DP | Cortex-M。RP2350 ARM も FPU 搭載 |
| MPU タイプ | なし / ARMv7-M MPU / ARMv8-M MPU / RISC-V PMP | アーキテクチャ依存 |
| TrustZone 有無 | あり (Cortex-M33, M23) / なし | ARMv8-M のみ |
| DSP 命令 | あり (M4/M7/M33) / なし | Cortex-M |
| NVIC 優先度ビット数 | 3 (8 レベル) ～ 8 (256 レベル) | チップ実装依存 |
| キャッシュ | I-Cache / D-Cache の有無とサイズ | M7, i.MX RT, ESP32-P4 |
| コア数 | シングル / デュアル / マルチ | RP2040(2), ESP32-S3(2), i.MX RT(1), RP2350(2) |
| ISA バリアント | ARM / RISC-V / デュアル | RP2350: ARM + RISC-V 選択可 |

**RP2350 固有**: デュアルアーキテクチャ (Cortex-M33 × 2 **または** Hazard3 RISC-V × 2)。
ビルド時にどちらの ISA でコンパイルするか選択が必要。

---

### 4.3 C3: 割り込み / 例外ベクター

#### 構成要素

| 要素 | 概要 | 所属レイヤ |
|------|------|-----------|
| コア例外 | Reset, NMI, HardFault, MemManage, BusFault, UsageFault, SVCall, PendSV, SysTick | L1 (Cortex-M 共通) |
| デバイス固有 IRQ | IRQ 番号 ↔ ハンドラ名の対応表 | L3 |
| ベクターテーブル構造 | テーブルサイズ、配列配置、`.isr_vector` セクション | L1 + L4 |

#### プラットフォーム差異

| プラットフォーム | 割り込みモデル | IRQ 数 (典型) | 特記 |
|----------------|--------------|--------------|------|
| **STM32F4** | NVIC | 82 | ベクターテーブルは Flash 先頭 |
| **STM32H7** | NVIC | 150+ | デュアルコア版は独立ベクター |
| **RP2040** | NVIC | 26 | 26 IRQ + 6 コア例外。各コアに独立 NVIC |
| **RP2350** | NVIC / PLIC | 52 | ARM モード: NVIC, RISC-V モード: PLIC。ISA で切替 |
| **ESP32-S3** | Interrupt Matrix | 内部 99 + 外部 | ソフトウェアルーティング — 任意の割り込みソースを任意の CPU 割り込みラインに接続 |
| **ESP32-P4** | CLIC | HP 128 + LP 64 | HP/LP サブシステム独立。RISC-V コア |
| **i.MX RT1060** | NVIC | 160 | Cortex-M7 標準 NVIC |

**ESP32 固有**: Interrupt Matrix は SVD のレジスタ定義に加え、
ソース→ライン**ルーティングテーブル**が必要（SVD からは得られない）。

---

### 4.4 C4: メモリマップ

#### 構成要素

| 要素 | 概要 |
|------|------|
| Flash リージョン | ベースアドレス, サイズ, セクタ構成 |
| SRAM リージョン | ベースアドレス, サイズ（複数リージョン可） |
| 特殊メモリ | CCM, TCM, バックアップ SRAM, OTP |
| ペリフェラル空間 | AHB/APB バス別のベースアドレス範囲 |
| ビットバンド | エイリアス領域のベースアドレス（Cortex-M3/M4 のみ） |
| 外部メモリ | QSPI Flash, SDRAM, HyperRAM のアドレスマッピング |

#### プラットフォーム差異

| プラットフォーム | メモリ構成の特徴 | バリアント差異 |
|----------------|-----------------|---------------|
| **STM32F4** | Flash + SRAM1 + SRAM2 + CCM + BKPSRAM + OTP + Bitband | Flash: 256K–2M, SRAM: 64K–256K |
| **STM32H7** | Flash + DTCM + ITCM + AXI-SRAM + SRAM1-4 + BKPSRAM | バリアントごとに大幅に異なる |
| **RP2040** | 内蔵 SRAM (264KB, 6 バンク) + XIP (外部 QSPI Flash) | SRAM バンク構成固定。Flash はボード依存 |
| **RP2350** | 内蔵 SRAM (520KB) + XIP + OTP (8KB) | SRAM 増量、OTP 追加 |
| **ESP32-S3** | 内蔵 SRAM (512KB) + RTC SRAM (16KB) + SPI Flash (外部) + PSRAM (外部, 最大 32MB) | SRAM は HP/LP で分割 |
| **ESP32-P4** | HP SRAM (768KB) + LP SRAM (32KB) + Flash (外部) + PSRAM (外部) | HP/LP サブシステム分離 |
| **i.MX RT1060** | FlexRAM (512KB: ITCM + DTCM + OCRAM に動的分割) + QSPI Flash (外部) + SDRAM (外部) | FlexRAM 構成はフューズまたはレジスタで変更可能 |

**i.MX RT 固有**: FlexRAM は ITCM / DTCM / OCRAM の比率をフューズまたはレジスタで
動的に構成可能。リンカスクリプトは構成に応じて変更が必要。

**ESP32 固有**: HP/LP 間のメモリ境界は SoC 内でハードウェア分離されており、
アクセス権限が異なる。LP メモリは Deep Sleep でも保持される。

---

### 4.5 C5: ペリフェラルレジスタ

SVD の主要なカバー範囲。最大規模のカテゴリ。

#### 構成要素

| 要素 | 概要 |
|------|------|
| ペリフェラルインスタンス | 名前、ベースアドレス（例: GPIOA = 0x40020000） |
| レジスタ | 名前、オフセット、サイズ（8/16/32 bit）、リセット値 |
| ビットフィールド | 名前、ビット位置、幅、アクセス属性（RW/RO/WO/W1C/RC_W1 等） |
| 列挙値 | フィールドの有効値と説明（例: MODE=0b01 → 出力） |
| レジスタクラスタ | 構造化された繰り返しパターン（例: NVIC_ISERn[8]） |

#### プラットフォーム差異 — ペリフェラルバリエーション

| ペリフェラル | STM32 | RP2040 | ESP32-S3 | i.MX RT |
|------------|-------|--------|----------|---------|
| **GPIO** | MODER/ODR/IDR/BSRR/AFR 方式 | SIO + IO_BANK0 + PADS 3 層 | GPIO Matrix 経由 | GPIO1-5 (標準 Cortex-M 方式) |
| **UART** | USART (CR1/CR2/CR3/BRR/SR/DR) | UART (PL011 ベース) | UART (FIFO ベース) | LPUART (低電力拡張) |
| **SPI** | SPI (CR1/CR2/SR/DR) | SSP (PL022 ベース) | SPI / GPSPI | LPSPI (低電力拡張) |
| **I2C** | I2C (CR1/CR2/OAR/SR/DR) | I2C (DW_apb_i2c ベース) | I2C (独自) | LPI2C (低電力拡張) |
| **Timer** | TIM1-14 (GP/Advanced/Basic) | Timer (alarm ベース) | Timer Group | GPT / PIT / QTMR |
| **ADC** | ADC1-3 (12bit, マルチチャネル) | ADC (4ch, 500ksps) | ADC (DMA 統合) | ADC_ETC + ADC |
| **DMA** | DMA1/2 (Stream × 8, Channel × 8) | 12ch DMA (チェーン可) | GDMA (動的割当) | eDMA (TCD ベース) |
| **USB** | OTG_FS / OTG_HS (DWC2) | USB (デバイスのみ) | USB-OTG + USB-Serial-JTAG | USB (EHCI) |

**特記**: 同じ機能名でもレジスタ構造は完全に異なる。PAL はペリフェラル単位で
vendor 固有のレジスタ定義を提供する（統一抽象は HAL の役割）。

---

### 4.6 C6: GPIO ピンマルチプレクシング

**最もプラットフォーム間差異が大きいカテゴリ。**

#### STM32: Alternate Function (AF) 方式

```
GPIOA Pin 9 → AF7 → USART1_TX
              AF5 → SPI2_MISO
              AF1 → TIM1_CH2
```

- ピンごとに AF0–AF15 の 16 スロット
- AF 番号はチップファミリ内でもバリアントごとに異なる
- データソース: CubeMX DB / STM32_open_pin_data

**必要な定義:**
| 定義 | 例 |
|------|-----|
| Pin → AF → Signal マッピング | PA9, AF7, USART1_TX |
| 入出力方向 | Push-Pull / Open-Drain |
| プルアップ / プルダウン | Pull-Up / Pull-Down / No-Pull |
| 出力スピード | Low / Medium / High / Very High |
| アナログ機能 | ADC_IN0, DAC_OUT1 |

#### RP2040 / RP2350: 3 層 GPIO + FUNCSEL 方式

```
GPIO 0 → FUNCSEL (5bit) → SPI0_RX (F1) / UART0_TX (F2) / I2C0_SDA (F3) / ...
```

- 3 つのハードウェア層:
  1. **PADS_BANK0** — 電気特性（ドライブ強度、スルーレート、プルアップ/ダウン）
  2. **IO_BANK0** — 機能選択（FUNCSEL）+ 割り込み設定
  3. **SIO** — 高速 GPIO 直接制御（シングルサイクル）
- FUNCSEL 値はピンごとに固定（RP2040: F1–F9, RP2350: 拡張あり）

**必要な定義:**
| 定義 | 例 |
|------|-----|
| Pin → FUNCSEL → Signal マッピング | GPIO0, F2, UART0_TX |
| PADS 設定 | Drive strength (2/4/8/12 mA), Slew rate, PUE/PDE |
| SIO 設定 | OE, OUT, IN |
| 特殊機能 | PIO0/PIO1 (任意ピンに割当可能) |

#### ESP32-S3 / ESP32-P4: GPIO Matrix 方式

```
任意の GPIO → GPIO Matrix → 任意のペリフェラル入出力シグナル
```

- **any-to-any ルーティング**: 任意の GPIO ピンに任意のペリフェラルシグナルを接続可能
- ただし一部は「専用ピン」（USB, JTAG, 高速 SPI Flash 等）
- GPIO Matrix はソフトウェア設定テーブルで制御

**必要な定義:**
| 定義 | 例 |
|------|-----|
| シグナル番号 → ペリフェラル機能 | Signal 6 → U0TXD (UART0 TX) |
| 入力 / 出力シグナルリスト | 入力 128, 出力 128 (ESP32-S3) |
| 専用ピン制約 | GPIO19/20 = USB D-/D+ (専用) |
| パッド属性 | Drive strength, Pull-up/down |
| RTC GPIO マッピング | GPIO ↔ RTC GPIO の対応 (Deep Sleep での使用) |

**ESP32-P4 固有**: HP GPIO (0–54) と LP GPIO (0–15) が独立。
LP GPIO は LP コアからのみアクセス可能。

#### i.MX RT: IOMUXC 方式

```
PAD (GPIO_AD_B0_09) → IOMUXC MUX → ALT2 → LPUART1_TXD
                      IOMUXC PAD → Drive strength / Pull / Speed
                      IOMUXC SELECT → Daisy chain input select
```

- IOMUXC は **独立したペリフェラル** として存在（GPIO とは別モジュール）
- 3 つの設定レジスタ:
  1. **SW_MUX_CTL_PAD** — ALT0–ALT7 の機能選択
  2. **SW_PAD_CTL_PAD** — 電気特性（DSE, SPEED, ODE, PKE, PUE, PUS, HYS）
  3. **SELECT_INPUT** — 入力パスの Daisy Chain 選択（複数パッドが同一機能を持つ場合の選択）
- パッド名が機能名を含む（例: GPIO_AD_B0_09 → ALT0 で GPIO1_IO09）

**必要な定義:**
| 定義 | 例 |
|------|-----|
| Pad → ALT → Signal マッピング | GPIO_AD_B0_09, ALT2, LPUART1_TXD |
| PAD 設定 | DSE (Drive Strength), SPEED, ODE, PKE/PUE/PUS, HYS |
| SELECT_INPUT レジスタ | LPUART1_TXD_SELECT_INPUT → daisy 値 |
| GPIO ポート対応 | GPIO1_IO09 ↔ PAD 名の対応 |

#### GPIO マルチプレクシング比較まとめ

| 特性 | STM32 AF | RP2040 FUNCSEL | ESP32 GPIO Matrix | i.MX IOMUXC |
|------|---------|---------------|-------------------|-------------|
| ルーティング | 固定 AF テーブル | 固定 FUNCSEL テーブル | **任意ルーティング** | 固定 ALT テーブル |
| 選択肢数 / ピン | 最大 16 (AF0–15) | 最大 9 (F1–F9) | 理論上無制限 | 最大 8 (ALT0–7) |
| 電気設定 | GPIO レジスタ内 | PADS レジスタ (別層) | GPIO レジスタ内 | IOMUXC PAD (別モジュール) |
| 入力選択 | なし | なし | なし | Daisy Chain SELECT_INPUT |
| 特殊ピン | アナログ専用ピンあり | PIO 割当可能 | USB/JTAG 専用あり | なし（全ピンが IOMUXC 経由） |
| データ規模 / チップ | 数百エントリ | 30 GPIO × 9 FUNC | 128 + 128 シグナル | 数百パッド × 8 ALT |

---

### 4.7 C7: クロックツリー

#### 構成要素

| 要素 | 概要 |
|------|------|
| クロックソース | HSI/HSE/LSI/LSE (STM32), XOSC/ROSC (RP2040), XTAL (ESP32) |
| PLL パラメータ | 入力分周、逓倍、出力分周の制約と有効範囲 |
| バスプリスケーラ | AHB/APB1/APB2 分周比 (STM32), clk_sys/clk_peri/clk_ref (RP2040) |
| ペリフェラルイネーブル | RCC_AHB1ENR.GPIOAEN (STM32), RESETS (RP2040), PCR (ESP32) |
| クロックゲーティング | スリープ時のクロック供給制御 |

#### プラットフォーム差異

| プラットフォーム | クロックツリーの特徴 | 複雑度 |
|----------------|-------------------|--------|
| **STM32F4** | HSI/HSE → PLL → SYSCLK → AHB/APB1/APB2。RCC レジスタ | 中 |
| **STM32H7** | 3 PLL + 多段分周 + 独立ペリフェラルクロック選択 | 高 |
| **RP2040** | XOSC/ROSC → PLL_SYS/PLL_USB → clk_sys/clk_ref/clk_peri/clk_adc/clk_rtc | 低–中 |
| **RP2350** | RP2040 と類似 + HSTX クロック追加 | 低–中 |
| **ESP32-S3** | XTAL(40MHz) → PLL (480/320MHz) → 各ペリフェラル個別分周 + RTC クロック | 中 |
| **ESP32-P4** | HP PLL + LP PLL の 2 系統 | 中–高 |
| **i.MX RT1060** | 24MHz XTAL → ARM PLL (1.056GHz) → 多段分周。CCM + CCM_ANALOG | 高 |

**必要な定義:**
| 定義 | 概要 |
|------|------|
| クロックソース一覧 | 名前、周波数範囲、起動時間 |
| PLL 構成制約 | 入力範囲、VCO 範囲、分周比制約 |
| バスクロック接続 | ペリフェラル → バス → クロックソースの接続グラフ |
| ペリフェラルクロックイネーブル | レジスタ + ビット位置 |
| 最大周波数制約 | バス / ペリフェラルの最大動作周波数 |

---

### 4.8 C8: DMA マッピング

#### 構成要素

| 要素 | 概要 |
|------|------|
| DMA コントローラ | インスタンス数、ストリーム/チャネル数 |
| リクエストマッピング | ペリフェラル → DMA ストリーム/チャネル → リクエスト番号 |
| 転送設定 | バースト長、データ幅、アドレスインクリメント |
| FIFO 設定 | FIFO 有無、閾値 |

#### プラットフォーム差異

| プラットフォーム | DMA モデル | マッピング方式 |
|----------------|-----------|--------------|
| **STM32F4** | DMA1/2 × 8 ストリーム × 8 チャネル | **固定テーブル** (ストリーム, チャネル, リクエスト) |
| **STM32H7** | MDMA + DMA1/2 + BDMA | 固定テーブル + DMAMUX (一部動的) |
| **RP2040** | 12 チャネル DMA | **DREQ 番号** による固定マッピング |
| **RP2350** | 16 チャネル DMA | DREQ 番号 (RP2040 拡張) |
| **ESP32-S3** | GDMA (5ch) | **動的割当** — 任意チャネルに任意ペリフェラルを接続 |
| **ESP32-P4** | AHB-DMA + AXI-DMA | HP/LP で独立 DMA サブシステム |
| **i.MX RT1060** | eDMA (32ch) + DMAMUX | **DMAMUX** — ソースをチャネルに動的接続 |

**STM32F4 の固定テーブルが最もデータ量が多い** (16 ストリーム × 8 チャネル = 128 エントリ)。
ESP32 の GDMA は動的ルーティングのため、ルーティングテーブルではなく DREQ 番号リストのみ必要。

---

### 4.9 C9: 電力管理

#### 構成要素

| 要素 | 概要 |
|------|------|
| 電源ドメイン | コア / IO / アナログ / バックアップ等のドメイン |
| スリープモード | Sleep / Stop / Standby (STM32), DORMANT (RP2040), Light Sleep / Deep Sleep (ESP32) |
| ウェイクアップソース | ピン、RTC、割り込み等の復帰トリガ |
| 電圧レギュレータ | LDO / SMPS モード選択 (STM32H7) |
| LP ペリフェラル | Deep Sleep でも動作するペリフェラル |

#### プラットフォーム差異

| プラットフォーム | 電力構成の特徴 |
|----------------|---------------|
| **STM32F4** | Sleep / Stop / Standby。バックアップドメイン (RTC + BKPSRAM) |
| **RP2040** | Sleep / Dormant。ROSC でウェイクアップ |
| **ESP32-S3** | Active / Modem Sleep / Light Sleep / Deep Sleep。ULP コプロセッサ常時動作可 |
| **ESP32-P4** | HP / LP サブシステム独立電力管理。LP コアが PM 制御 |
| **i.MX RT** | Run / Wait / Stop。SNVS ドメイン (常時オン) |

---

### 4.10 C10: セキュリティ / 保護

#### 構成要素

| 要素 | 概要 |
|------|------|
| TrustZone | SAU/IDAU 構成、Secure/Non-Secure パーティション |
| Flash 保護 | Read Protection (RDP), Write Protection (WRP) |
| JTAG 保護 | デバッグポート無効化 |
| セキュアブート | 署名検証、暗号エンジン |
| OTP / eFuse | ワンタイムプログラマブル領域 |

#### プラットフォーム差異

| プラットフォーム | セキュリティ機能 |
|----------------|----------------|
| **STM32F4** | RDP Lv0/1/2, セクタ WRP |
| **STM32H7/L5** | TrustZone (ARMv8-M), OTFDEC, TAMP |
| **RP2040** | なし（セキュリティ機能は最小限） |
| **RP2350** | Secure Boot (SHA-256), ARM TrustZone / RISC-V PMP, OTP |
| **ESP32-S3** | Flash 暗号化, Secure Boot V2, eFuse, HMAC, AES-XTS |
| **ESP32-P4** | HP/LP 分離 + TrustZone 相当の保護 |
| **i.MX RT** | HAB (High Assurance Boot), BEE (Bus Encryption Engine), SNVS |

---

### 4.11 C11: デバイスメタデータ

バリアント固有の情報。データシートやベンダー DB から取得。

| 定義項目 | 例 |
|---------|-----|
| デバイス型番 | STM32F407VGT6, RP2040-B2 |
| パッケージ | LQFP100, QFN56, BGA |
| ピン数 | 使用可能ピン数 |
| 温度範囲 | -40°C ～ +85°C (Industrial) |
| 動作電圧 | 1.8V–3.6V |
| 最大クロック | 168MHz (STM32F407), 133MHz (RP2040) |
| Flash / SRAM サイズ | デバイスバリアント固有のサイズ |
| シリコンリビジョン | Rev A / Rev B / Rev Z |
| ペリフェラルインスタンス | このバリアントに存在するペリフェラル一覧 |
| パッケージ別ピン配置 | ピン番号 → GPIO の対応 |

---

### 4.12 C12: リンカ / スタートアップ

#### 構成要素

| 要素 | 概要 | 所属レイヤ |
|------|------|-----------|
| メモリリージョン定義 | MEMORY { FLASH, RAM, ... } | L4 (サイズはバリアント依存) |
| セクション配置 | .text, .data, .bss, .isr_vector | L1–L2 |
| スタック / ヒープ | 初期 SP, ヒープ領域 | L4 (サイズはプロジェクト依存) |
| C ランタイム初期化 | .data コピー, .bss ゼロ化 | L1 |
| コンストラクタ呼出 | .init_array, __libc_init_array | L1 |
| ベクターテーブル配置 | `.isr_vector` セクション属性 | L1 |

#### プラットフォーム差異

| プラットフォーム | リンカ / スタートアップの特徴 |
|----------------|---------------------------|
| **STM32** | 標準 Cortex-M スタートアップ。Flash→RAM コピー、CCM 初期化 |
| **RP2040** | Boot Stage 2 (XIP 初期化) が必要。Flash は XIP 経由アクセス |
| **RP2350** | Boot Stage 2 + セキュアブートチェーン |
| **ESP32** | ESP-IDF リンカスクリプト (IRAM/DRAM/Flash 分離)。ROM bootloader がスタートアップ |
| **i.MX RT** | FlexRAM 構成に依存。ITCM/DTCM/OCRAM のサイズがリンカスクリプトに反映 |

---

### 4.13 C13: デバッグ / トレース

#### 構成要素

| 要素 | 概要 | 所属レイヤ |
|------|------|-----------|
| デバッグインタフェース | SWD / JTAG / USB-Serial-JTAG | L2–L3 |
| ITM (Instrumentation) | printf 出力用 Stimulus ポート | L2 (Cortex-M3 以上) |
| DWT (Data Watchpoint) | サイクルカウンタ、データウォッチポイント | L2 |
| ETM (Embedded Trace) | 命令トレース | L2 |
| SWO | Serial Wire Output (ITM/DWT のトレース出力) | L2 |
| CoreDebug | ハードウェアブレークポイント、ステップ実行 | L2 |

#### プラットフォーム差異

| プラットフォーム | デバッグの特徴 |
|----------------|---------------|
| **STM32** | SWD + SWO (ITM/DWT)。標準 Cortex-M デバッグ |
| **RP2040** | SWD (各コア独立)。Picoprobe / debugprobe |
| **RP2350** | SWD + RISC-V デバッグモジュール (ISA 依存) |
| **ESP32-S3** | USB-Serial-JTAG (内蔵)。OpenOCD 経由 |
| **ESP32-P4** | JTAG。HP/LP コア独立デバッグ |
| **i.MX RT** | SWD + ETM (命令トレース)。J-Link / DAPLink |

---

## 5. ベンダー固有カテゴリ

上記 C1–C13 の汎用カテゴリに加え、ベンダー / チップ固有のカテゴリが存在する。

### 5.1 RP2040 / RP2350 固有

| カテゴリ | 概要 |
|---------|------|
| **PIO (Programmable I/O)** | ステートマシン × 4 (RP2040) / × 12 (RP2350)。命令セット、FIFO、クロック分周 |
| **SIO (Single-cycle IO)** | 高速 GPIO 制御 + コア間通信 (FIFO, スピンロック, 整数除算器) |
| **XIP (Execute In Place)** | 外部 QSPI Flash の実行キャッシュ設定 |
| **Boot Stage 2** | QSPI Flash 初期化コード (256 バイト) |

### 5.2 ESP32-S3 / ESP32-P4 固有

| カテゴリ | 概要 |
|---------|------|
| **GPIO Matrix** | 入力 / 出力シグナルルーティングテーブル |
| **ULP (Ultra Low Power)** コプロセッサ | FSM / RISC-V ベースの LP コプロセッサ命令セット |
| **HP / LP サブシステム分離** (P4) | HP コアと LP コアの独立した制御 |
| **eFuse** | OTP 領域の構成と読出し |
| **RTC** | RTC ペリフェラル + Deep Sleep 制御 + RTC GPIO |

### 5.3 i.MX RT 固有

| カテゴリ | 概要 |
|---------|------|
| **IOMUXC** | 独立した I/O マルチプレクサモジュール (MUX + PAD + SELECT_INPUT) |
| **FlexRAM** | ITCM / DTCM / OCRAM 動的構成 |
| **CCM (Clock Controller Module)** | 複雑なクロックツリー + CCGR ゲーティング |
| **SEMC** | SDRAM / NOR Flash / NAND Flash コントローラ |
| **FlexSPI** | 高性能 QSPI/Octal-SPI コントローラ + AHB 読み出しキャッシュ |

---

## 6. 定義の完全性チェックリスト

あるチップの PAL サポートが「完全」であるかを判定するためのチェックリスト。

### 6.1 必須 (MUST) — これがなければビルドできない

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| M1 | コアシステム定義 (アーキテクチャ, FPU, MPU 有無等) | C2 |
| M2 | メモリマップ (Flash/SRAM ベースアドレスとサイズ) | C4 |
| M3 | 割り込みベクターテーブル (IRQ 番号 + ハンドラ名) | C3 |
| M4 | リンカスクリプト用メモリリージョン | C12 |
| M5 | スタートアップコード (ベクターテーブル配置、C ランタイム初期化) | C12 |

### 6.2 重要 (SHOULD) — ペリフェラル利用に必要

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| S1 | ペリフェラルレジスタ定義 (SVD ベース) | C5 |
| S2 | コアペリフェラルレジスタ (NVIC, SCB, SysTick) | C1 |
| S3 | GPIO ピンマルチプレクシング | C6 |
| S4 | クロックツリー (ペリフェラルイネーブル含む) | C7 |
| S5 | DMA マッピング | C8 |

### 6.3 推奨 (MAY) — 高度な機能に必要

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| O1 | 電力管理定義 | C9 |
| O2 | セキュリティ / 保護 | C10 |
| O3 | デバッグ / トレース (DWT, ITM) | C13 |
| O4 | デバイスメタデータ (パッケージ, 温度範囲) | C11 |
| O5 | ベンダー固有カテゴリ (PIO, GPIO Matrix, IOMUXC 等) | §5 |

---

## 7. PAL 生成スコープの段階的拡大

### Phase 1: 最小実行可能 (LED 点滅)

```
M1 + M2 + M3 + M4 + M5 + S1 (GPIO のみ) + S2 (NVIC, SysTick)
```

→ GPIO 出力 + SysTick 割り込みで LED 点滅が可能な最小セット。

### Phase 2: UART / SPI / I2C

```
Phase 1 + S1 (全ペリフェラル) + S3 (GPIO AF) + S4 (クロック) + S5 (DMA)
```

→ 通信ペリフェラルの完全利用が可能。

### Phase 3: プロダクション品質

```
Phase 2 + O1–O5 + ベンダー固有カテゴリ
```

→ 低消費電力、セキュリティ、デバッグ含む完全サポート。

---

## 8. 次のステップ

1. **../pipeline/hw_data_pipeline.md** — 各カテゴリに対するデータソースの対応表と生成パイプライン設計
2. **06_RAL_ARCHITECTURE.md** → **06_PAL_ARCHITECTURE.md** — PAL のコード構造と API 設計
3. 各プラットフォームの SVD ファイル品質評価とギャップ分析
