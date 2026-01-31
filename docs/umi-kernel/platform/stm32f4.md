# UMI Kernel 実装仕様（STM32F4）

**規範レベル:** MUST/SHALL/REQUIRED, SHOULD/RECOMMENDED, MAY/NOTE/EXAMPLE
**対象読者:** Porting / Kernel Dev
**適用範囲:** STM32F407VG 実装

---

## 1. 目的・スコープ
本書は STM32F4 固有の起動シーケンス、IRQ 優先度、DMA/Audio/MIDI フローの規範実装を定義する。

---

## 2. 起動シーケンス
- Reset から `main()` までに以下を実施する:
  - FPU 有効化
  - `.data` コピーと `.bss` クリア
  - SRAM ベクタテーブル構築（VTOR切替）
  - IRQ ハンドラ登録と例外優先度設定
  - C++ グローバルコンストラクタの実行

- `main()` の初期化フェーズ:
  1. クロック/GPIO 初期化
  2. 共有メモリ・ローダ初期化
  3. MPU 設定
  4. USB コールバック設定
  5. 周辺初期化（USB/SysTick/DWT/I2S/PDM）
  6. RTOS 起動（4タスク生成）

### 2.1 ベクタテーブルの配置と切替
- Flash には **最小ベクタ（SP と Reset のみ）** を配置する。
- 起動直後に SRAM 上のベクタテーブルを初期化し、**VTOR を SRAM に切替**する。
- 以降の例外/IRQ ハンドラは SRAM テーブルに動的登録する。

**例: 最小ベクタ（Flash, .isr_vector）**
```cpp
__attribute__((section(".isr_vector"), used))
const void* const g_boot_vectors[2] = {
  &_estack,
  Reset_Handler,
};
```

**例: SRAM ベクタ初期化（概念コード）**
```cpp
void Reset_Handler() {
  init_data_bss();
  umi::irq::init();          // SRAMテーブル初期化 + VTOR切替
  umi::irq::set_handler(...);
  main();
}
```

---

## 3. 例外/IRQ 優先度
- Audio DMA は最優先、USB はその次に高優先度とする。
- `SysTick` は低優先度、`PendSV` は最下位であること。

**例: 優先度値（ターゲット依存の一例）**
```
Audio DMA: 5
USB OTG FS: 6
SysTick: 0xF0
PendSV: 0xFF
```

---

## 4. DMA/Audio/MIDI フロー
- I2S Audio DMA はダブルバッファで動作する。
- DMA 完了 ISR は AudioTask を起床し、AudioTask が `process()` を呼ぶ。
- USB MIDI は ISR → SPSC → AudioTask でイベント化する。
- USB SysEx は ISR → SystemTask でシェル処理する。

**例: オーディオDMAの流れ（概念）**
```
DMA TC IRQ → enqueue(buffer) → notify(AudioReady)
AudioTask: wait_block(AudioReady) → process_audio_frame()
```

---

## 5. 既知制約
- サンプルレートは PLLI2S 制約により目標 48kHz に対し実測がずれる。
- 96kHz 以上では PDM マイクが無効となる。

**注:** 数値は実機依存のため、仕様本文では「例」として扱う。
