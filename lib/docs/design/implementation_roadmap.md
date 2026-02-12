# 実装ロードマップ

**ステータス:** 設計中  **策定日:** 2026-02-10
**根拠:** 全設計文書の統合

**関連文書:**
- [foundations/architecture.md](foundations/architecture.md) — パッケージ構成
- [build_system.md](build_system.md) — ビルドシステム設計
- [codegen_pipeline.md](codegen_pipeline.md) — コード生成パイプライン
- [testing_hw.md](testing_hw.md) — テスト戦略
- [init_sequence.md](init_sequence.md) — 初期化シーケンス
- [interrupt_model.md](interrupt_model.md) — 割り込みモデル

---

## 1. 設計方針

### 1.1 垂直スライス優先

水平に「全 MCU のレジスタ定義を完成させる」のではなく、
**1 つの MCU (STM32F407VG) で完全な垂直スライスを動かすこと** を最優先する。

```
水平展開 (後回し)
──────────────────────────────────────────────►
STM32F4 ──── RP2040 ──── ESP32 ──── i.MX RT

│  垂直スライス (最優先)
│
│  PAL レジスタ定義
│  ↓
│  HAL ドライバ実装
│  ↓
│  Board 統合
│  ↓
│  アプリケーション動作
│  ↓
│  テスト自動化
▼
```

### 1.2 最小動作単位

Phase 1 の完了定義: **STM32F4-Discovery で LED 点滅 + UART 出力 + RTT モニタが動く**

これにより以下が検証される:
- ビルドシステム (xmake) のクロスコンパイルフロー
- PAL: GPIO, RCC, UART の最小レジスタ定義
- HAL: OutputDevice, GpioOutput の Concept 充足
- Board: platform.hh による統合
- テスト: RTT 経由の自動検証

---

## 2. フェーズ定義

### Phase 0: 基盤整備 (ビルドシステム + 既存コード整理)

**目標:** 既存コードを新アーキテクチャに合わせて再配置し、ビルドが通る状態にする

| タスク | 成果物 | 完了条件 |
|--------|--------|---------|
| xmake ルール整理 | board/resolve ルール | `xmake f --board=stm32f4-disco` で設定解決 |
| ディレクトリ再配置 | 新ディレクトリ構造 | [architecture.md](foundations/architecture.md) §3 に準拠 |
| MCU DB 移行 | database/mcu/stm32f4/ | 既存 JSON → Lua 形式 |
| リンカスクリプト生成 | linker/memory_ld ルール | MCU DB からの自動生成 |
| ホストテスト維持 | `xmake test` 全パス | 既存 8/8 テストが壊れない |

**依存関係:**
```
ディレクトリ再配置 ──► xmake ルール整理 ──► MCU DB 移行
                                          ──► リンカスクリプト生成
ホストテスト維持 (並行)
```

---

### Phase 1: 最小垂直スライス (STM32F4 LED + UART)

**目標:** STM32F4-Discovery で LED 点滅 + UART "Hello" 出力

| タスク | 成果物 | 完了条件 |
|--------|--------|---------|
| umimmio 型テンプレート確定 | umimmio/register.hh 等 | static_assert でアドレス・サイズ検証 |
| PAL: GPIO レジスタ定義 | pal/stm32f4/periph/gpio.hh | umimmio 型によるGPIO 操作 |
| PAL: RCC レジスタ定義 | pal/stm32f4/periph/rcc.hh | クロック有効化 |
| PAL: UART レジスタ定義 | pal/stm32f4/periph/uart.hh | UART 送受信 |
| HAL ドライバ: GPIO | umiport/mcu/stm32f4/gpio.hh | GpioOutput concept 充足 |
| HAL ドライバ: UART | umiport/mcu/stm32f4/uart.hh | OutputDevice concept 充足 |
| スタートアップ | src/stm32f4/startup.cc | Reset_Handler → main() |
| board 統合 | board/stm32f4-disco/platform.hh | Platform concept 充足 |
| LED 点滅デモ | examples/stm32f4_blink/ | 実機で LED が点滅 |
| UART 出力デモ | examples/stm32f4_hello/ | UART で "Hello UMI" 出力 |
| フラッシュルール | xmake flash-kernel | pyOCD でフラッシュ成功 |

**依存関係:**
```
umimmio 確定 ──► PAL: GPIO ──► HAL: GPIO ──► LED 点滅デモ
             ──► PAL: RCC ──┤
             ──► PAL: UART ──► HAL: UART ──► UART 出力デモ
スタートアップ ──► board 統合 ──► デモ統合
                              ──► フラッシュルール
```

---

### Phase 2: テスト自動化 + RTT 統合

**目標:** テストを自動化し、CI パイプラインに組み込める状態にする

| タスク | 成果物 | 完了条件 |
|--------|--------|---------|
| umirtm RTT 統合 | RTT 経由の printf 出力 | RTT Viewer でログ確認 |
| Concept 準拠テスト | test/concept_compliance/ | static_assert 全パス |
| Mock テスト基盤 | test/mock/ | MockI2C, MockUart 等 |
| Renode 基本環境 | renode/stm32f4-disco.resc | Renode で ELF ロード・実行 |
| CI 設定 | .github/workflows/ | Layer 0-2 テスト自動実行 |

**依存関係:**
```
Phase 1 完了 ──► RTT 統合 ──► Renode 環境
              ──► Concept テスト (並行)
              ──► Mock テスト基盤 (並行)
              ──► CI 設定 (全テスト整備後)
```

---

### Phase 3: PAL コード生成パイプライン

**目標:** SVD からペリフェラルレジスタ定義を自動生成できる状態にする

| タスク | 成果物 | 完了条件 |
|--------|--------|---------|
| SVD パーサー | gen/parsers/svd_parser.py | STM32F407 SVD をパース |
| 中間表現定義 | gen/model.py | DeviceModel スキーマ |
| C++ テンプレート | gen/templates/*.hh.j2 | umimmio 型定義を生成 |
| 生成 → コンパイル検証 | gen/tests/ | 生成物がコンパイルを通る |
| Phase 1 の手書き PAL を生成物で置換 | pal/stm32f4/ | 動作が同一 |
| xmake pal-gen タスク | xmake pal-gen | ワンコマンドで再生成 |

**依存関係:**
```
SVD パーサー ──► 中間表現 ──► C++ テンプレート ──► 生成物コンパイル検証
                                              ──► 手書き PAL 置換
                                              ──► xmake タスク
```

---

### Phase 4: ドライバ拡充 (I2C, SPI, DMA, Timer)

**目標:** 実用的なペリフェラルドライバセットを提供する

| タスク | 成果物 | 完了条件 |
|--------|--------|---------|
| PAL: I2C レジスタ | pal/stm32f4/periph/i2c.hh | 生成パイプラインで生成 |
| PAL: SPI レジスタ | pal/stm32f4/periph/spi.hh | 生成パイプラインで生成 |
| PAL: DMA レジスタ | pal/stm32f4/periph/dma.hh | 生成パイプラインで生成 |
| PAL: Timer レジスタ | pal/stm32f4/periph/timer.hh | 生成パイプラインで生成 |
| HAL ドライバ: I2C | umiport/mcu/stm32f4/i2c.hh | I2cTransport concept 充足 |
| HAL ドライバ: SPI | umiport/mcu/stm32f4/spi.hh | SpiTransport concept 充足 |
| CS43L22 ドライバ統合 | umidevice/audio/cs43l22/ | 実機で音声出力 |
| 割り込みディスパッチ | umiport/arm/cortex-m/nvic.hh | [interrupt_model.md](interrupt_model.md) に準拠 |
| DMA 転送 | umiport/mcu/stm32f4/dma.hh | DMA 完了コールバック動作 |

---

### Phase 5: マルチプラットフォーム展開

**目標:** STM32F4 以外のプラットフォームで動作させる

| タスク | 成果物 | 完了条件 |
|--------|--------|---------|
| WASM ビルド | examples/headless_webhost/ | ブラウザで音声出力 |
| ホストビルド | examples/host_synth/ | macOS/Linux で実行 |
| RP2040 PAL | pal/rp2040/ | RP2040 の最小垂直スライス |
| ESP32 PAL | pal/esp32/ | ESP32 の最小垂直スライス |
| ボード追加ガイド | docs/guides/ADD_BOARD.md | 新ボード追加の手順書 |

---

## 3. 完了定義サマリー

| Phase | 完了の証拠 |
|-------|-----------|
| Phase 0 | `xmake test` 全パス + `xmake f --board=stm32f4-disco` 成功 |
| Phase 1 | STM32F4-Discovery で LED 点滅 + UART 出力 (実機で確認) |
| Phase 2 | CI で Layer 0-2 テスト全パス + Renode テスト成功 |
| Phase 3 | `xmake pal-gen` で STM32F4 PAL を再生成 → ビルド・テスト全パス |
| Phase 4 | CS43L22 から音声出力 (実機) + I2C/SPI/DMA テスト全パス |
| Phase 5 | 同一アプリが STM32F4 / WASM / Host で動作 |

---

## 4. リスクと緩和策

| リスク | 影響 | 緩和策 |
|--------|------|--------|
| umimmio 型設計の手戻り | Phase 1-3 に波及 | Phase 1 で最小 PAL を手書きし、型設計を早期検証 |
| SVD 品質問題 | Phase 3 遅延 | modm-devices のパッチ済みデータ活用 |
| Renode の STM32F4 ペリフェラルカバレッジ不足 | Phase 2 テスト限定的 | 不足ペリフェラルは実機テストにフォールバック |
| xmake カスタムルールの複雑化 | メンテナンスコスト増 | ルールの単体テスト + dev-sync フロー厳守 |
| C++23 コンパイラ対応差異 | クロスコンパイル問題 | arm-none-eabi-gcc のバージョン固定 |

---

## 5. 未解決の設計課題

| # | 課題 | 影響フェーズ | 備考 |
|---|------|------------|------|
| 1 | Phase 0 と Phase 1 の境界 | 0-1 | 既存コードの再利用範囲次第で統合可能 |
| 2 | RTOS 統合のタイミング | 4-5 | ベアメタル優先か RTOS 並行か |
| 3 | オーディオパイプライン設計 | 4 | process() のリアルタイム制約との整合 |
| 4 | パッケージマネージャ公開 | 5 | synthernet を外部公開するかプライベートのままか |
