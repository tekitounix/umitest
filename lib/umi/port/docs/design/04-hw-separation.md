# 04: HW 分離原則

## 問題: カーネルやドライバへのHW漏出

現在、カーネル (`kernel.cc`) やUSBスタック (`lib/umiusb/`) にHW固有の実装が混入している:

| 箇所 | 漏出内容 |
|------|---------|
| `kernel.cc` | `mcu::usb_audio()`, `mcu::usb_hal()` を直接呼び出し |
| `kernel.cc` | DMAバッファ (`mcu::audio_buf0()`) を直接操作 |
| `kernel.cc` | `mcu::configure_plli2s()`, `mcu::disable_i2s_irq()` 等MCU固有関数 |
| `kernel.cc` | DWTサイクルカウンタのアドレス `0xE0001004` をハードコード |
| `fault_handler.hh` | `backend::cm::FaultInfo` (ARM CFSR bitfields) を直接参照 |
| `loader.cc` | `#ifdef __ARM_ARCH` で ARM privilege.hh を条件インクルード |
| `stm32_otg.hh` | STM32レジスタアドレス `0x50000000` をテンプレート引数に |
| `stm32_otg.hh` | FIFOアクセスモデルがSTM32固有 |

## 原則: インターフェース/プロトコル層にHW詳細を入れない

```
✗ 現在: カーネルが mcu:: を直接呼ぶ
┌──────────┐     ┌──────────────┐
│ kernel   │────→│ mcu::usb_audio() │  カーネルがHW実装に依存
│          │────→│ mcu::i2s_disable()│
└──────────┘     └──────────────┘

✓ 新設計: カーネルはConceptで定義されたインターフェースのみ使用
┌──────────┐     ┌────────────────┐     ┌──────────────┐
│ kernel   │────→│ AudioDriver    │←────│ board/       │
│          │     │ (Concept)      │     │ audio.hh     │
│          │────→│ FaultHandler   │←────│ arch/        │
│          │     │ (Concept)      │     │ fault.hh     │
└──────────┘     └────────────────┘     └──────────────┘
                  インターフェース         HW固有実装
                  (lib/umi/kernel/)     (lib/umiport/)
```

## HW分離のConcept定義

```cpp
// lib/umi/kernel/ に配置（HW非依存）

/// オーディオHWドライバ — カーネルから見たオーディオI/Oの抽象
template<typename T>
concept AudioDriver = requires(T& drv, const T& cdrv,
                               int32_t* buf, uint32_t frames) {
    { drv.start() } -> std::same_as<void>;
    { drv.stop() } -> std::same_as<void>;
    { drv.configure_sample_rate(uint32_t{}) } -> std::convertible_to<uint32_t>;
    { cdrv.sample_rate() } -> std::convertible_to<uint32_t>;
    { cdrv.buffer_frames() } -> std::convertible_to<uint32_t>;
};

/// フォールト情報 — アーキテクチャ非依存の障害表現
template<typename T>
concept FaultReport = requires(const T& fault) {
    { fault.is_stack_overflow() } -> std::convertible_to<bool>;
    { fault.is_access_violation() } -> std::convertible_to<bool>;
    { fault.is_bus_fault() } -> std::convertible_to<bool>;
    { fault.faulting_address() } -> std::convertible_to<uint32_t>;
};

/// USB HALもConceptで抽象化済み（既存の umiusb::Hal）
/// → stm32_otg.hh の実装は port/mcu/ に移動
```

## HW実装の配置先

| インターフェース | 定義場所（HW非依存） | 実装場所（HW固有） |
|-----------------|---------------------|-------------------|
| `AudioDriver` | `lib/umi/kernel/` | `port/board/<name>/board/audio_driver.hh` |
| `FaultReport` | `lib/umi/kernel/` | `port/arch/<name>/arch/fault.hh` |
| `umiusb::Hal` | `lib/umiusb/include/core/hal.hh` | `port/mcu/<name>/mcu/usb_otg.hh` |
| `AudioBridge` | `lib/umiusb/include/audio/` | `port/board/<name>/board/audio_bridge.hh` |

## stm32_otg.hh の分離

現在 `lib/umiusb/include/hal/stm32_otg.hh` にあるSTM32固有のUSB OTG実装:

```
現在:
  lib/umiusb/include/hal/stm32_otg.hh    ← ライブラリ内にHW固有コード

移行後:
  lib/umiusb/include/core/hal.hh          ← Hal Concept定義（既存、変更なし）
  port/mcu/stm32f4/mcu/usb_otg.hh         ← STM32F4 OTG FS実装
  port/mcu/stm32h7/mcu/usb_otg.hh         ← STM32H7 OTG HS実装（新規）
```

USBスタック (`lib/umiusb/`) はプロトコル処理とディスクリプタ管理に専念し、
HWレジスタ操作は `port/mcu/` に押し出す。テンプレートパラメータとして
`Hal` Conceptを受け取るため、コード変更は最小限。

## kernel.cc のHW依存排除

```cpp
// ✗ 現在
mcu::usb_audio().read_audio(buf, frames);
mcu::configure_plli2s(new_rate);
uint32_t cycles = *reinterpret_cast<volatile uint32_t*>(0xE0001004);

// ✓ 新設計 — board層が提供するAudioDriverをテンプレートで受け取る
template<AudioDriver Driver>
void audio_task(Driver& driver) {
    driver.start();
    // ...
}
```

カーネルの audio_task, system_task 等はテンプレート関数とし、
board層がConcept実装を注入する。カーネル自身はHWを知らない。
