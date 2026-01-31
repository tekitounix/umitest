# umi

# 定義
- OS: KernelとDriverによって実装されたファームウェアとしてバイナリ配布されるもの
- Kernel: OSの中核機能ライブラリ
- Kernel-Core: KernelのうちHW非依存のロジック部分
- Kernel-Port: HW依存部分

例えばI2Cペリフェラルにおいて
- BSP: ピン番号やクロックなどの設定値の集合、ボード依存
- MMIO: レジスタ操作そのもの、HW依存
- HAL: レジスタ操作の抽象レイヤ、ここで使い方は統一されない　APIはHW非依存、PortはHW依存
- Driver: 実際の手続きと設定、ここで使い方を統一する。APIはHW非依存、PortはHW依存。単体呼び出しでは成り立たないことが多くOS実装依存。
- Device Server Task: Driverを呼び出して操作する

マイコンでベアメタルプログラミングする場合はタイミングまで直接制御することが多いのでHALでそのまま書くのが一般的。一方OSではタイミング管理をOSに実装するのでドライバは単純呼び出しで動作する。

# UMI-OS Spec

- syscall & shared abi
- MPU Layout
- Event/IPC
- .umia Format
    - Processor/Controller

# UMI-OS Bootloader

- umiboot

# UMIOS Library

UMIOSを構成するためのライブラリ群

- umios_kernel_core
    - scheduler
    - syscall
    - loader
- umios_kernel_port
    - cm4
    - esp32
    - wasm
- umia: 非特権タスクとして動作する機能
    - umiupdater
    - umishell
- umihal
    - umiusb
- umimmio
- bsp: 設定+初期化手続き


# UMI-APP SDK

OSやHW非依存の独立ライブラリ

- umidi
- umidsp
- umigui
- umiui
- umicoro: coroutine runtime library
- umism: state machine template library
- umiusb: usb class descriptor template

# UMIM

umi-moduleフォーマット

単体機能するヘッドレスアプリケーション

umip+umic+

# UMIU

umi-unitフォーマット

umip+umicで構成される

# UMI-Driver
- audio_device.hh
- usb_audio_device.hh

# UMI-HAL

OS非依存のレジスタ操作抽象化レイヤ。
- gpio_hal.hh
- usb_hal.hh

# UMI-Port

- mcu
  - stm32
      - hal_mmio
        - gpio.hh
        - ...
      - driver
        - gpio.hh
        - ...
      - kernel
        - port.cc
- device
  - audio_codec
    - cs43l22.hh

目次
application
  .umia
    header
  abi
  entry
  
kernel
  service
    loader
    updater
      relocator
    
  memory
  port