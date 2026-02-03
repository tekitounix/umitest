# 00 — システム全体像

## UMI-OS とは

UMI-OS はオーディオ/MIDI 処理のためのアプリケーションホスト、ランタイム環境である。
同一の Processor コードが以下のターゲットで動作する:

- **組み込み** — STM32F4 等の MCU 上でリアルタイム OS として動作（.umia 形式）
- **WASM** — Web Audio API 上で動作（.umim 形式）
- **プラグイン** — VST3 / AU / CLAP ホスト内で動作

## アーキテクチャ概要

```
┌──────────────────────────────────────────────────────┐
│                  Application                          │
│                                                       │
│  ┌─────────────────┐     ┌──────────────────────┐    │
│  │  Processor       │     │  Controller (main)   │    │
│  │  process()       │     │  wait_event() ループ  │    │
│  │  リアルタイム    │     │  or コルーチン        │    │
│  └─────────────────┘     └──────────────────────┘    │
│          ↑                         ↑                  │
│          │ AudioContext            │ ControlEvent      │
│          │                         │                   │
├──────────┴─────────────────────────┴──────────────────┤
│                  Runtime (OS / Backend)                │
│                                                       │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ Audio      │  │ Event        │  │ Param        │  │
│  │ Engine     │  │ Router       │  │ State        │  │
│  └────────────┘  └──────────────┘  └──────────────┘  │
│          ↑                ↑               ↑           │
├──────────┴────────────────┴───────────────┴───────────┤
│                  Backend Adapter                       │
│                                                       │
│  組み込み: DMA + ISR + SVC                             │
│  WASM:     Web Audio AudioWorklet                     │
│  Plugin:   DAW processBlock()                         │
└──────────────────────────────────────────────────────┘
```

## 3 層モデル

### Application 層

アプリ開発者が書くコード。ターゲットに依存しない。

- **Processor** — `process(AudioContext&)` を実装する構造体。オーディオレートで呼ばれる。ヒープ割当・ブロッキング・I/O 禁止。
- **Controller** — `main()` で実行される制御ループ。UI 更新、レイヤー切り替え、設定変更を担う。

Processor と Controller の詳細は [02-processor-controller.md](02-processor-controller.md) を参照。

### Runtime 層

AudioContext の構築、イベントルーティング、パラメータ状態管理を行う。
組み込みでは RTOS カーネル + System Service 群として実装される。WASM / Plugin では軽量なランタイムライブラリとして実装される。

中核となる **EventRouter**（System Service の一つ）の責務:
- MIDI / ハードウェア入力の受信と分類（RouteTable）
- AudioEventQueue / ControlEventQueue へのイベント分配
- SharedParamState の管理（CC→パラメータ自動変換）
- output_events の逆方向処理
- process() の呼び出しタイミング制御

System Service は OS が提供するサービス群の総称で、EventRouter の他に Shell、Driver 等を含む。

### Backend Adapter 層

ターゲット固有のハードウェア/ホスト API とのブリッジ。

| ターゲット | AudioContext 構築 | MIDI 入力 | パラメータ同期 |
|-----------|------------------|-----------|---------------|
| 組み込み | DMA ISR → バッファ切り替え | USB/UART → RawInputQueue | syscall 経由 |
| WASM | AudioWorklet process() | Web MIDI API | JS ↔ WASM メモリ |
| VST3 | processData → AudioContext | processData.inputEvents | DAW パラメータ自動化 |
| AU | renderBlock → AudioContext | MIDIEventList | AUParameter |
| CLAP | process → AudioContext | input_events | clap_param |

詳細は [08-backend-adapters.md](../08-backend-adapters.md) を参照。

## タスクモデル（組み込みターゲット）

組み込みターゲットでは 4 優先度のプリエンプティブスケジューラが動作する。

| 優先度 | タスク | 役割 |
|--------|--------|------|
| 0 (Realtime) | Audio Task | DMA 通知 → process() 呼び出し |
| 1 (Server) | System Task | MIDI ルーティング、SysEx、シェル |
| 2 (User) | Control Task | アプリ main()、syscall 処理 |
| 3 (Idle) | Idle Task | WFI スリープ |

FPU コンテキスト退避ポリシーの詳細は [11-scheduler.md](../02-kernel/11-scheduler.md) を参照。

- Audio Task は DMA 割り込みで起床し、ハードリアルタイムのデッドラインを持つ
- System Task は Server 優先度で MIDI ルーティングと SysEx 処理を行う
- Control Task はアプリの `main()` を非特権モード（MPU 隔離）で実行する
- Idle Task は全タスク Blocked 時に WFI で省電力

WASM / Plugin ターゲットではこのタスクモデルは不要。ホストが直接 process() を呼ぶ。

## OS / アプリ間通信（組み込み）

| 手段 | 特性 | 用途 |
|------|------|------|
| 共有メモリ | ゼロオーバーヘッド、直接読み書き | AudioContext、SharedParamState |
| Syscall | SVC 例外経由、OS 特権で実行 | RegisterProc、WaitEvent、set_app_config 等 |

原則: 共有メモリで済むものは syscall にしない。

Syscall の詳細は [03-port/06-syscall.md](../03-port/06-syscall.md) を参照。

## 設計原則

1. **Processor はターゲット非依存** — `process(AudioContext&)` だけが契約。I/O・OS・ホスト API に触れない
2. **Controller はターゲットほぼ非依存** — `umi::wait_event()` / `umi::set_app_config()` 等の抽象 API を使う。内部実装はバックエンドが差し替える
3. **リアルタイム安全** — process() 内でヒープ割当・ブロッキング・例外・stdio 禁止
4. **ロックフリー通信** — OS ↔ アプリ間は SPSC キューと atomic で通信。mutex 不使用
5. **データ駆動ルーティング** — MIDI メッセージの経路はデータの性質（RouteTable）で決まる。トランスポート種別に依存しない
