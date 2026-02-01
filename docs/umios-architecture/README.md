# UMI-OS アーキテクチャ仕様

本ディレクトリは UMI-OS の**目標設計仕様**である。
各ドキュメント内の「状態」欄（実装済み / 新設計 / 将来）が実装状況を示す。
「新設計」「将来」の項目は仕様として確定済みだが、実装は未着手または進行中である。

## 設計原則

- **One Source, Multi Target** — 同一の Processor コードが組み込み・WASM・プラグイン（VST3/AU/CLAP）で動作する
- **ターゲット非依存の API** — `process(AudioContext&)` を中心としたポータブルなインターフェース
- **ターゲット固有部分はアダプタに隔離** — AudioContext の構築・イベント変換・パラメータ同期はバックエンドが担う

## ドキュメント一覧

| ファイル | 内容 |
|---------|------|
| [00-overview](00-overview.md) | システム全体像・タスクモデル |
| [01-audio-context](01-audio-context.md) | AudioContext 統一仕様 |
| [02-processor-controller](02-processor-controller.md) | Processor / Controller モデル |
| [03-event-system](03-event-system.md) | イベントシステム（RouteTable, キュー, 経路分類） |
| [04-param-system](04-param-system.md) | パラメータシステム（SharedParamState, AppConfig） |
| [05-midi](05-midi.md) | MIDI 統合（UMP, トランスポート, SysEx, ジッター補正） |
| [06-syscall](06-syscall.md) | Syscall 仕様（番号体系） |
| [07-memory](07-memory.md) | メモリレイアウト（組み込みターゲット） |
| [08-backend-adapters](08-backend-adapters.md) | バックエンド別アダプタ（組み込み / WASM / Plugin） |
| [09-app-binary](09-app-binary.md) | アプリバイナリ仕様（.umia / .umim） |
| [10-shared-memory](10-shared-memory.md) | SharedMemory 構造体の完全定義 |
| [11-scheduler](11-scheduler.md) | スケジューラ・FPU コンテキスト退避 |

### 推奨読み順

```
00-overview ─→ 01-audio-context ─→ 02-processor-controller
                    │                      │
                    ▼                      ▼
              10-shared-memory ←── 04-param-system
                    │                      ▲
                    ▼                      │
              07-memory            03-event-system ←── 05-midi
                    │
                    ▼
              09-app-binary        06-syscall
                                         │
              08-backend-adapters ←──────┘
                    │
                    ▼
              11-scheduler
```

基礎概念（00→01→02）を先に読み、その後は興味に応じて分岐してよい。
10-shared-memory は 01/04/07 から参照されるため、パラメータやメモリの詳細に入る前に目を通しておくとよい。

## 既存ドキュメントとの関係

本仕様は以下の既存ドキュメントの内容を統合・整理したものである。
既存ドキュメントは参考資料として維持するが、矛盾がある場合は**本仕様を正とする**。

| 新 | 旧（主な参照元） |
|---|---|
| 00-overview | umi-kernel/OVERVIEW.md, umi-kernel/ARCHITECTURE.md |
| 01-audio-context | umi-api/API_CONTEXT.md, umidi/EVENT_SYSTEM_DESIGN.md |
| 02-processor-controller | umi-api/APPLICATION.md, umi-api/API_CONTEXT.md |
| 03-event-system | umidi/EVENT_SYSTEM_DESIGN.md, umidi/event_state.md |
| 04-param-system | umidi/EVENT_SYSTEM_DESIGN.md, umi-api/EVENT_STATE_GUIDE.md |
| 05-midi | umidi/MIDI_TRANSPORT_DESIGN.md, umidi/SYSEX_ROUTING.md, umidi/JITTER_COMPENSATION.md |
| 06-syscall | umi-kernel/ARCHITECTURE.md, umi-kernel/spec/*, umidi/EVENT_SYSTEM_DESIGN.md |
| 07-memory | umi-kernel/MEMORY.md, umi-kernel/platform/stm32f4.md |
| 08-backend-adapters | （新規） |
| 09-app-binary | umi-kernel/ARCHITECTURE.md, umi-kernel/spec/application.md |
| 10-shared-memory | 01-audio-context, 04-param-system, 07-memory から構造体定義を集約 |
| 11-scheduler | 00-overview の FPU 列を移設（他セクションは TODO） |
