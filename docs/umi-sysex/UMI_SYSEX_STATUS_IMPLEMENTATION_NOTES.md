# UMI-STATUS 実装ノート（ドラフト）

バージョン: 0.2.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-29

---

## 1. リアルタイム非干渉

SysEx は低優先度で処理し、AudioTask への影響を避ける。

- 演奏データ (Note/CC/Clock) は最優先
- SysEx 組み立て・プロトコル処理はタスクコンテキストで実行

---

## 2. 現状の実装と改善点

現状:
- SysEx 組み立てが ISR 内
- 単一バッファで取りこぼしリスク

改善方針:
- ISR は raw_rx_queue への push のみ
- ServerTask 内で CIN 解析と SysEx 組み立て
- 完結 SysEx をプロトコル処理へ渡す

---

## 3. SysEx 処理アーキテクチャ

### 3.1 設計原則

- ServerTask 内イベントドリブン
- タスク追加は避ける（スタック/複雑性の抑制）

### 3.2 典型フロー

1. ISR: raw_rx_queue に push、ServerTask に notify
2. ServerTask: パケット解析、SysEx 完結を検出
3. 完結 SysEx: STATUS/DFU/STDIO などへ分配

### 3.3 SysEx ストリーム解析 API 指針

- 1バイトずつ入力し、**タイムアウトを越えたら破棄**
- 完結フレーム以外は部分状態を保持（再入可能）
- バッファ上限超過時は即破棄し、状態を初期化

想定API:

- `parse_sysex(byte, timeout_ms)` -> `Ok(complete)` / `Partial` / `Error`
- `Partial` は状態保持、`Error` は状態初期化

---

## 4. DFU の非同期処理

- FW_DATA 受信ごとに StorageService へ非同期書き込み
- FS_COMPLETE で ACK を送信
- wait_event で CPU を解放

---

## 5. 処理優先度ガイドライン

| コマンドグループ | 処理場所 | 備考 |
|---|---|---|
| Note/CC | AudioTask | midi_queue 経由 |
| STDIN/STDOUT/STDERR | ServerTask | プロトコル処理 |
| FW_* | ServerTask | StorageService へ非同期委譲 |
| STATUS/IDENTITY | ServerTask | 即完了 |
| AUDIO_STATUS | ServerTask | 即完了 |
| METER_* | ServerTask | 定期タイマー |
| PARAM_* | ServerTask | 即完了 |

---

## 6. フロー制御メモ

- 受信側バッファが逼迫したら XOFF
- 回復時に XON
- サブスクリプションは Rate Limit 前提
