# UMI プロジェクト命名体系

**バージョン:** 0.10.0
**更新日:** 2025-01-25

本ドキュメントはUMIプロジェクトにおける用語・命名の定義を規定します。

---

## 概要図

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         UMI Project                                      │
│                  (プロジェクト全体の総称)                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                    UMI-OS Specification                             │ │
│  │              (仕様・インターフェース定義)                             │ │
│  │                                                                      │ │
│  │  • Syscall ABI (番号、引数、戻り値)                                  │ │
│  │  • .umia バイナリ形式                                              │ │
│  │  • メモリレイアウト (MPUリージョン定義)                               │ │
│  │  • Event/IPC プロトコル                                              │ │
│  │  • ProcessorLike Concept                                            │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                              │                                           │
│                              ▼                                           │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                     UMI-OS Kernel                                   │ │
│  │            (仕様に基づくカーネル実装 = OS本体)                        │ │
│  │                                                                      │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │ │
│  │  │  RTOS Core   │  │ Syscall      │  │ App Loader   │              │ │
│  │  │              │  │ Handler      │  │              │              │ │
│  │  │ • Scheduler  │  │              │  │ • .umia    │              │ │
│  │  │ • Context SW │  │ • SVC Handler│  │ • 署名検証    │              │ │
│  │  │ • Task Mgmt  │  │ • Dispatch   │  │ • MPU設定    │              │ │
│  │  └──────────────┘  └──────────────┘  └──────────────┘              │ │
│  │                                                                      │ │
│  │  ┌──────────────┐  ┌──────────────┐                                │ │
│  │  │ Device       │  │ Shell        │                                │ │
│  │  │ Drivers      │  │              │                                │ │
│  │  │              │  │ • CLI        │                                │ │
│  │  │ • USB Audio  │  │ • SysEx I/O  │                                │ │
│  │  │ • USB MIDI   │  │ • Commands   │                                │ │
│  │  │ • GPIO       │  │              │                                │ │
│  │  └──────────────┘  └──────────────┘                                │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                              │                                           │
│                              ▼                                           │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                        UMI HAL                                      │ │
│  │               (Hardware Abstraction Layer)                          │ │
│  │                                                                      │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │ │
│  │  │ STM32F4      │  │ Simulator    │  │ RP2040       │              │ │
│  │  │ Backend      │  │ Backend      │  │ Backend ◇    │              │ │
│  │  └──────────────┘  └──────────────┘  └──────────────┘              │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                      UMI Libraries                                  │ │
│  │               (OS非依存の再利用可能ライブラリ)                        │ │
│  │                                                                      │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐               │ │
│  │  │ umidsp  │  │ umidi   │  │ umiusb  │  │ umicoro │               │ │
│  │  │         │  │         │  │         │  │         │               │ │
│  │  │ DSP処理  │  │ MIDI    │  │ USB     │  │ Coro    │               │ │
│  │  └─────────┘  └─────────┘  └─────────┘  └─────────┘               │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                      UMI App SDK                                    │ │
│  │                 (アプリケーション開発キット)                          │ │
│  │                                                                      │ │
│  │  • umi/app.hh        - register_processor, wait_event              │ │
│  │  • umi/coro.hh       - コルーチンサポート                            │ │
│  │  • syscall.hh        - カーネルへのsyscallインターフェース            │ │
│  │  • make_umia.py    - ビルドツール                                  │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 用語定義

### プロジェクト階層

| 用語 | 定義 | 該当ファイル/ディレクトリ |
|------|------|-------------------------|
| **UMI Project** | プロジェクト全体の総称。OS、ライブラリ、ツール群すべてを含む | `/` (ルート) |
| **UMI-OS Specification** | syscall ABI、メモリレイアウト、バイナリ形式等の**仕様定義** | `docs/specs/`, `lib/umios/app/syscall.hh` (定義部分) |
| **UMI-OS Kernel** | 仕様を実装したカーネル本体。アプリをホストする**ファームウェア全体** | `lib/umios/kernel/`, `examples/stm32f4_kernel/` |
| **UMI HAL** | MCU固有のハードウェアドライバ群 | `lib/umios/backend/` |
| **UMI Libraries** | OS非依存の再利用可能ライブラリ | `lib/umidsp/`, `lib/umidi/`, `lib/umiusb/` |
| **UMI App SDK** | アプリケーション開発者向けAPI・ツール | `lib/umios/app/`, `tools/make_umia.py` |

### カーネル内部コンポーネント

| 用語 | 定義 | 該当ファイル |
|------|------|-------------|
| **RTOS Core** | タスクスケジューリング機構。4優先度プリエンプティブスケジューラ、コンテキストスイッチ | `lib/umios/kernel/scheduler.hh`, `lib/umios/kernel/task.hh` |
| **Syscall Handler** | SVC例外ハンドラ。アプリからのsyscallをディスパッチ | `lib/umios/kernel/syscall_handler.hh` |
| **App Loader** | `.umia`バイナリのロード、署名検証、MPU設定 | `lib/umios/kernel/loader.cc` |
| **Device Drivers** | ハードウェアデバイスドライバ群 | `lib/umiusb/`, `lib/umios/backend/` |
| **Shell** | 管理用CLIインターフェース | `lib/umios/kernel/shell_commands.hh` |

### RTOS Core 詳細

| 用語 | 定義 |
|------|------|
| **Task** | スケジューリング単位。優先度、スタック、状態を持つ |
| **Priority** | タスク優先度。Realtime(0) > Server(1) > User(2) > Idle(3) |
| **Context Switch** | PendSV例外によるレジスタ保存/復元 |
| **Scheduler** | 優先度ベースプリエンプティブスケジューラ |
| **Tick** | 1msシステムタイマ割り込み |

### アプリケーション層

| 用語 | 定義 |
|------|------|
| **UMI Application** | `.umia`形式のアプリケーションバイナリ |
| **Processor Task** | オーディオ処理タスク。`process()`を実行。Realtime優先度 |
| **Control Task** | イベント処理タスク。`main()`を実行。User優先度 |
| **ProcessorLike** | `process(AudioContext&)`を持つ型の制約 (C++20 Concept) |

---

## 命名規則

### ファイル/ディレクトリ

```
lib/
├── umios/                    # UMI-OS (Kernel + App SDK)
│   ├── kernel/               # UMI-OS Kernel実装
│   ├── app/                  # UMI App SDK
│   └── backend/              # UMI HAL
├── umidsp/                   # UMI Libraries - DSP
├── umidi/                    # UMI Libraries - MIDI
├── umiusb/                   # UMI Libraries - USB
└── umicoro/                  # UMI Libraries - Coroutine (将来)

examples/
├── stm32f4_kernel/           # UMI-OS Kernel (STM32F4ターゲット)
├── synth_app/                # UMI Application例
└── headless_webhost/         # Web開発環境
```

### 名前空間

| 名前空間 | 用途 |
|---------|------|
| `umi` | アプリケーションAPI (App SDK) |
| `umi::os` | カーネル内部 |
| `umi::syscall` | Syscall定義 |
| `umi::dsp` | DSPライブラリ |
| `umi::midi` | MIDIライブラリ |
| `umi::usb` | USBライブラリ |

### クラス/構造体

| 命名パターン | 例 | 用途 |
|-------------|-----|------|
| `XxxTask` | `ProcessorTask`, `ControlTask` | タスク |
| `XxxDriver` | `UsbAudioDriver` | デバイスドライバ |
| `XxxHandler` | `SyscallHandler` | 例外/イベントハンドラ |
| `XxxProvider` | `StateProvider` | 依存注入インターフェース |
| `XxxConfig` | `ShellConfig` | 設定構造体 |

---

## 用語の使い分け

### 正しい表現

| シーン | 正しい表現 |
|--------|-----------|
| プロジェクト紹介 | 「UMIは組み込みオーディオ向けのフレームワークです」 |
| OS説明 | 「UMI-OS Kernelはアプリケーションをホストするリアルタイムカーネルです」 |
| RTOS説明 | 「RTOS Coreは4優先度のプリエンプティブスケジューラを提供します」 |
| ライブラリ説明 | 「umidspはOS非依存のDSPライブラリです」 |
| アプリ開発説明 | 「UMI App SDKを使ってアプリケーションを開発します」 |

### 避けるべき表現

| 避けるべき | 理由 | 代替 |
|-----------|------|------|
| 「UMI-OS」単独でカーネルを指す | 仕様とカーネルの区別が曖昧 | 「UMI-OS Kernel」 |
| 「UMIカーネル」 | 非公式 | 「UMI-OS Kernel」 |
| 「RTOSカーネル」 | RTOS CoreとKernel全体を混同 | 「RTOS Core」または「UMI-OS Kernel」 |
| 「umi OS」「Umi-OS」 | 表記ゆれ | 「UMI-OS」 |

---

## 関連用語との比較

### 一般的なOSとの対応

| 一般的なOS | UMI-OS |
|-----------|--------|
| Linux Kernel | UMI-OS Kernel |
| CFS Scheduler | RTOS Core (Scheduler) |
| System Call Interface | Syscall Handler |
| ELF Loader | App Loader |
| Device Drivers | Device Drivers |

### 他のRTOSとの比較

| 概念 | FreeRTOS | Zephyr | UMI-OS |
|-----|----------|--------|--------|
| 分離方式 | ライブラリ統合 | ビルド時統合 | バイナリ分離 |
| タスク保護 | なし (同一権限) | MPU (オプション) | MPU (必須) |
| アプリ形式 | 同一バイナリ | 同一バイナリ | `.umia` |
| syscall | なし (直接呼出) | なし (直接呼出) | SVC命令 |

---

## バージョン履歴

| 日付 | 変更内容 |
|------|----------|
| 2025-01-25 | 初版作成 |
