# 13 — システムサービス概要

## 概要

SystemTask 上で動作する OS サービス群のアーキテクチャ。
各サービスの詳細設計は個別ドキュメントを参照。

| サービス | 詳細 | 状態 |
|---------|------|------|
| Boot Sequence | [15-boot-sequence.md](15-boot-sequence.md) | 実装済み |
| App Loader | [16-app-loader.md](16-app-loader.md) | 実装済み |
| Shell | [17-shell.md](17-shell.md) | 実装済み |
| Updater (DFU over SysEx) | [18-updater.md](18-updater.md) | 実装済み |
| StorageService | [19-storage-service.md](19-storage-service.md) | 新設計 |
| Diagnostics | [20-diagnostics.md](20-diagnostics.md) | 実装済み |

---

## サービスアーキテクチャ

### SystemTask の役割

SystemTask（優先度 1 / Server）は OS サービスのホストタスクである。
AudioTask（優先度 0 / Realtime）より低く、ControlTask（優先度 2 / User）より高い。

```
SystemTask (Server, prio 1)
  ├─ App Loader       .umia 検証・ロード
  ├─ Shell            SysEx stdio 経由の対話シェル
  ├─ Updater          DFU over SysEx
  ├─ StorageService   ファイルシステム要求の逐次処理
  └─ Diagnostics      CPU/メモリ統計、エラーログ
```

### イベント駆動モデル

サービスはイベント駆動で動作する。SystemTask は `wait_block()` でイベントを待ち、対応するサービスにディスパッチする。

```
SystemTask ループ:
  events = wait_block(SysTick | SysEx | FS | Fault)
  ├─ SysTick  → Diagnostics 定期収集
  ├─ SysEx    → Shell / Updater にディスパッチ
  ├─ FS       → StorageService 要求処理
  └─ Fault    → process_pending_fault() → エラー表示 + Shell 有効化
```

### サービスのライフサイクル

| フェーズ | 処理 | 参照 |
|---------|------|------|
| Reset → main() | ハードウェア初期化、.umia ロード | [15-boot-sequence.md](15-boot-sequence.md) |
| main() → RTOS 開始 | タスク作成、スケジューラ起動 | [11-scheduler.md](11-scheduler.md) |
| 正常動作 | Shell / Diagnostics / StorageService | 各サービスドキュメント |
| アプリ Fault | OS 生存、Shell 有効化 | [12-memory-protection.md](12-memory-protection.md) |
| DFU モード | Updater がフラッシュ書き込み | [18-updater.md](18-updater.md) |

### タスク間通信

```
AudioTask (prio 0)        SystemTask (prio 1)       ControlTask (prio 2)
  │                          │                          │
  │◄── DMA notify ──────────│                          │
  │                          │◄── SysTick resume ──────│
  │                          │                          │◄── app entry
  │                          │◄── SysEx (USB ISR) ─────│
  │                          │                          │
  │── overrun/underrun ─────►│ (Diagnostics)           │
  │                          │── notify(FS) ──────────►│
  │                          │                          │
```

---

## 関連ドキュメント

- [06-syscall.md](06-syscall.md) — Syscall ABI 定義（番号体系）
- [11-scheduler.md](11-scheduler.md) — タスクモデル、優先度、コンテキストスイッチ
- [12-memory-protection.md](12-memory-protection.md) — MPU 設定、Fault 処理
- [14-security.md](14-security.md) — 署名検証、暗号プリミティブ
- [15-boot-sequence.md](15-boot-sequence.md) — 起動フロー
- [16-app-loader.md](16-app-loader.md) — アプリロード
- [17-shell.md](17-shell.md) — 対話シェル
- [18-updater.md](18-updater.md) — DFU
- [19-storage-service.md](19-storage-service.md) — ファイルシステム
- [20-diagnostics.md](20-diagnostics.md) — 統計・Fault ログ
