# UMI-OS Kernel 概要

## 1. UMI-OSとは

UMI-OSは組み込みオーディオ/MIDI処理のためのリアルタイムオペレーティングシステムです。

主な特徴:
- OS/アプリの完全バイナリ分離（.umia形式）
- 4優先度プリエンプティブスケジューラ（O(1)ビットマップ方式）
- MPUによるメモリ保護とFault隔離
- ProcessorLike Concept（C++20、vtable不要）
- サンプル精度のイベント処理
- SysEx経由のシェル/DFU/stdio

## 2. システム構成図

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           Web Browser                                     │
│  ┌─────────────┐  ┌──────────────────────────────────────────────────┐  │
│  │ Shell UI    │  │ HardwareBackend (Web MIDI API)                   │  │
│  │ (custom)    │  │ - SysEx encode/decode (protocol.js)              │  │
│  └─────────────┘  └──────────────────────────────────────────────────┘  │
└────────────────────────────────┬─────────────────────────────────────────┘
                                 │ USB MIDI (SysEx)
                                 ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                    STM32F4-Discovery Kernel                               │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    Interrupt Handlers                               │  │
│  │  DMA1_Stream3 (PDM)  │  DMA1_Stream5 (I2S)  │  OTG_FS (USB)        │  │
│  │  SysTick (1ms)       │  SVC (Syscall)       │  PendSV (Switch)     │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                    │                                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    RTOS Scheduler (4-Priority)                      │  │
│  ├────────────┬────────────┬──────────────────┬───────────────────────┤  │
│  │ Priority 0 │ Priority 1 │    Priority 2    │      Priority 3       │  │
│  │ (Realtime) │  (Server)  │      (User)      │        (Idle)         │  │
│  ├────────────┼────────────┼──────────────────┼───────────────────────┤  │
│  │ Audio Task │System Task │  Control Task    │     Idle Task         │  │
│  │            │            │                  │                       │  │
│  │ - DMA通知  │ - SysEx    │  - App main()    │  - WFI (sleep)        │  │
│  │ - Audio処理│ - Shell    │  - Syscall処理   │                       │  │
│  │ - USB IN   │            │                  │                       │  │
│  └────────────┴────────────┴──────────────────┴───────────────────────┘  │
│                                    │                                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    Shared Memory (16KB)                             │  │
│  │  sample_rate  │  dt  │  sample_position  │  event_queue  │  params │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                    │                                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    Application (.umia)                            │  │
│  │  - Unprivileged mode (Thread/PSP)                                   │  │
│  │  - Processor登録 → オーディオコールバック                           │  │
│  └────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

## 3. タスク構成

| タスク | 優先度 | スタック | FPUポリシー | 役割 |
|--------|--------|----------|-------------|------|
| Audio Task | 0 (Realtime) | 4KB (CCM) | Exclusive | DMA割り込み処理、オーディオバッファ処理、USB Audio IN送信 |
| System Task | 1 (Server) | 2KB (CCM) | Forbidden | SysEx受信、シェルコマンド処理、USB MIDIハンドリング |
| Control Task | 2 (User) | 8KB (CCM) | LazyStack | アプリケーション実行、Syscall処理 |
| Idle Task | 3 (Idle) | 256B (CCM) | Forbidden | WFI（Wait For Interrupt）によるスリープ |

### タスク分離の根拠

| 境界 | 理由 |
|------|------|
| Audio ↔ System | デッドライン保証。オーディオはハードリアルタイム（数百μs）。System処理がブロックしてもオーディオは中断しない |
| System ↔ Control | 特権/MPU分離。SystemはOS特権。ControlはアプリMPU隔離。Fault時にOS生存 |
| Idle ↔ 全タスク | 電力管理。全タスクBlocked時のみIdleが実行されWFI。専用タスクでないと判定不可 |

## 4. メモリレイアウト

```
Flash (1MB):
  0x08000000 ┬─────────────────────┐
             │ Kernel (.text)      │ 384KB
  0x08060000 ├─────────────────────┤
             │ App Image (.umia) │ 128KB
  0x080A0000 ├─────────────────────┤
             │ Reserved            │
  0x08100000 └─────────────────────┘

SRAM (128KB):
  0x20000000 ┬─────────────────────┐
             │ Kernel Data/BSS     │ 48KB
  0x2000C000 ├─────────────────────┤
             │ App RAM             │ 48KB
  0x20018000 ├─────────────────────┤
             │ Shared Memory       │ 16KB
  0x2001C000 ├─────────────────────┤
             │ DMA Buffers         │ 16KB
  0x20020000 └─────────────────────┘

CCM (64KB - DMAアクセス不可):
  0x10000000 ┬─────────────────────┐
             │ Audio Task Stack    │ 4KB
             │ System Task Stack   │ 2KB
             │ Control Task Stack  │ 8KB
             │ Idle Task Stack     │ 256B
             │ Debug Buffers       │ 残り
  0x10010000 └─────────────────────┘
```

## 5. OS/アプリ間通信

| 手段 | 特性 | 用途 |
|------|------|------|
| SharedMemory | ゼロオーバーヘッド。アプリから直接読み書き | オーディオバッファ、メタ情報、パラメータ |
| Syscall | SVC例外経由。OS特権で実行 | 特権操作、スケジューラ状態変更 |

原則: SharedMemoryで済むものはsyscallにしない。

### Syscall一覧

| Nr | 名前 | 説明 |
|----|------|------|
| 0 | Exit | アプリケーション終了 |
| 1 | Yield | CPU制御を自発的に手放す |
| 2 | WaitEvent | イベント待機（ブロッキング） |
| 3 | GetTime | 現在時刻（マイクロ秒）取得 |
| 4 | GetShared | 共有メモリポインタ取得 |
| 5 | RegisterProc | オーディオプロセッサ登録 |
| 6 | UnregisterProc | プロセッサ登録解除（将来） |

## 6. アプリケーション形式（.umia）

128バイトヘッダ + フラットバイナリ。

| フィールド | 説明 |
|-----------|------|
| magic | `0x414D4955` ("UMIA") |
| abi_version | ABIバージョン (1) |
| entry_offset / process_offset | エントリポイント |
| text/rodata/data/bss_size | セクションサイズ |
| crc32 | IEEE 802.3 CRC |
| signature[64] | Ed25519署名（Release版） |

## 7. オーディオシステム概要

2つの独立した系統:

1. **USB Audio OUT → I2S DAC**: ホストPCの再生音をパススルー出力
2. **App + Mic → USB Audio IN**: アプリのシンセ出力とマイク入力をホストPCへ送信

アプリの入力はMIDIのみ（シンセサイザー用途）。

## 8. ビルドとデプロイ

```bash
# カーネルビルド
xmake build stm32f4_kernel

# アプリケーションビルド
xmake build synth_app

# フラッシュ書き込み
xmake flash-kernel
xmake flash-synth-app

# Web UI
xmake webhost-serve
```

## 9. ドキュメントガイド

| ドキュメント | 内容 | 対象読者 |
|-------------|------|---------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | 詳細アーキテクチャ（スケジューラ、Syscall、SharedMemory等） | カーネル開発者 |
| [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) | 設計判断の根拠（ADR集） | カーネル開発者 |
| [STM32F4_IMPLEMENTATION.md](STM32F4_IMPLEMENTATION.md) | STM32F4実装詳細（起動フロー、ISR、DMA） | カーネル移植者 |
| [CONTEXT_API.md](CONTEXT_API.md) | AudioContext/Processor/Controller API | アプリ開発者 |
| [LIBRARY_CONTENTS.md](LIBRARY_CONTENTS.md) | lib/umios/ 分類と依存関係 | コントリビューター |

---

*関連ドキュメント:*
- [NOMENCLATURE.md](../NOMENCLATURE.md) — プロジェクト全体の用語体系
- [UMI_STATUS_PROTOCOL.md](../UMI_STATUS_PROTOCOL.md) — SysExプロトコル設計
- [UMIOS_STORAGE.md](../UMIOS_STORAGE.md) — ストレージ設計（将来）
