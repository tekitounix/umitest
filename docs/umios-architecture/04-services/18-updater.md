# 18 — Updater (DFU over SysEx)

## 概要

SysEx 経由でのファームウェア/アプリバイナリ更新（DFU: Device Firmware Update）。
SystemTask 上で動作し、ホスト PC との通信で Flash の書き込み・検証・ロールバックを行う。

| 項目 | 状態 |
|------|------|
| DFU SysEx プロトコル | 実装済み |
| CRC32 完全性検証 | 実装済み |
| Ed25519 署名検証 | 実装済み |
| ロールバック | 実装済み |
| SystemMode 切替 | 実装済み |

---

## BootProfile（S/L/X）

更新先とロールバック戦略は Flash 構成に依存するため、以下の 3 つに分類して共通化する。

| Profile | 概要 | 代表ターゲット | 更新先の基本方針 |
|--------|------|----------------|------------------|
| S (Small‑Sector) | 内蔵Flashの消去単位が小さく、A/B が現実的 | STM32L4/G4 | 内蔵Flashで A/B |
| L (Large‑Sector) | 内蔵Flashの消去単位が大きく、A/B が非現実的になりやすい | STM32F4/F7/H7 | 内蔵単一 or 外部Flash |
| X (eXternal‑Flash) | 外部Flash前提（内蔵は最小ブートのみ） | STM32H750, RP2040/2350, ESP32‑S3/P4 | 外部Flashで A/B or staging |

BootProfile はボード定義で固定し、Updater はそのプロファイルに従って更新先を決定する。

---

## 更新対象と配置方針

| 対象 | 更新先 | 備考 |
|------|--------|------|
| Kernel | BootProfile に応じた OS 領域 | Profile S では A/B、L では単一 or 外部、X では外部 |
| App (.umia) | App 領域 | OS と同様に BootProfile に従う |
| Bootloader | 原則更新しない | 例外的にサポート専用で実施（後述） |

**補足**: 内蔵DFU など「ハードウェア制御を失う更新経路」は使用しない。

---

## アーキテクチャ

```
Host PC (umi-tools / Web UI)
  │
  │  USB MIDI SysEx
  ▼
OTG_FS ISR → SysEx バッファ → notify(SystemTask)
  │
  ▼
SystemTask → Updater
  ├─ FW_QUERY / FW_INFO: デバイス情報問い合わせ
  ├─ FW_BEGIN:  バッファ確保、転送開始
  ├─ FW_DATA:   データチャンク受信 → Flash 書き込み
  ├─ FW_VERIFY: CRC32 + Ed25519 署名検証
  ├─ FW_COMMIT: メタデータ更新、確定
  ├─ FW_ROLLBACK: 前バージョンへのロールバック
  └─ FW_REBOOT: システム再起動
```

---

## DFU プロトコル

### コマンド一覧

| コマンド | 方向 | 説明 |
|---------|------|------|
| FW_QUERY | Host → Device | デバイス情報問い合わせ |
| FW_INFO | Device → Host | ファームウェア情報応答 |
| FW_BEGIN | Host → Device | 更新開始（対象、サイズ、CRC） |
| FW_ERASE_DONE | Device → Host | 消去完了通知（非同期） |
| FW_DATA | Host → Device | データチャンク転送 |
| FW_ACK | Device → Host | 応答（成功/エラー） |
| FW_PROGRESS | Device → Host | 進捗通知（非同期） |
| FW_VERIFY | Host → Device | 転送完了後の検証要求 |
| FW_COMMIT | Host → Device | 検証成功後の確定 |
| FW_ROLLBACK | Host → Device | 前バージョンへのロールバック |
| FW_REBOOT | Host → Device | システム再起動 |

### 更新フロー（非同期設計）

FW_BEGIN 後の Flash 消去は**非同期で実行**され、完了時に `FW_ERASE_DONE` が**デバイス側から自発的に**送信される。ホストはポーリングやタイムアウトではなく、この通知を待つ。

```
Host                          Device (SystemTask)
  │                              │
  │  FW_QUERY ──────────────→   │
  │  ←────────────── FW_INFO    │  (デバイス ID、現在の FW バージョン)
  │                              │
  │  FW_BEGIN ──────────────→   │  (対象: kernel|app, サイズ, CRC)
  │  ←──────────────── FW_ACK   │  (受付確認、消去開始)
  │                              │
  │      〜 消去中（非同期）〜      │  (Device 側で消去実行、CPU は yield)
  │                              │
  │  ←─────────── FW_ERASE_DONE │  ★ 消去完了を Device が自発送信
  │                              │
  │  FW_DATA ───────────────→  │  (256B チャンク)
  │  ←──────────────── FW_ACK   │  (書き込み完了)
  │        ...                   │
  │  ←─────────── FW_PROGRESS   │  (オプション: 進捗 %)
  │        ...                   │
  │                              │
  │  FW_VERIFY ─────────────→  │
  │  ←──────────────── FW_ACK   │  (CRC32 + Ed25519 署名検証)
  │                              │
  │  FW_COMMIT ─────────────→  │  (メタデータ更新、確定)
  │  ←──────────────── FW_ACK   │
  │                              │
  │  FW_REBOOT ─────────────→  │  (NVIC_SystemReset)
```

### 非同期通知の原則

| 通知 | タイミング | 内容 |
|------|-----------|------|
| `FW_ERASE_DONE` | 消去完了時 | `{status, erased_sectors, elapsed_ms}` |
| `FW_PROGRESS` | 任意（大サイズ更新時） | `{percent, bytes_written}` |

ホストは `FW_BEGIN` の ACK を受信した後、`FW_ERASE_DONE` を待ってから `FW_DATA` を送信する。
`FW_ERASE_DONE` 受信前に `FW_DATA` を送信した場合、デバイスは `EBUSY` を返す。

**タイムアウト**: ホストは `FW_ERASE_DONE` を 30 秒以内に受信できない場合、転送をキャンセルする。

### 更新対象

| 対象 | Flash 領域 | 検証 |
|------|-----------|------|
| Kernel | Flash Bank 0 | CRC32 + 署名（Release） |
| App (.umia) | Flash Bank 1（アプリ領域） | CRC32 + 署名（Release） |

---

## BootProfile による更新先決定

Updater は BootProfile を参照して更新先を決定する。

- Profile S: 内蔵Flashの **非アクティブ側スロット**へ書き込み → 検証 → COMMIT
- Profile L: 内蔵単一領域への更新（容量が許すなら A/B も可）、または外部Flashへ更新
- Profile X: 外部Flashへ更新（A/B または staging）

BootConfig の更新と再起動後のスロット切替は Bootloader が担当する。

---

## OS 更新フロー（Updater の役割）

Updater は **「書く側」**、Bootloader は **「起動スロットを選ぶ側」**に分離する。

```
1. FW_BEGIN / FW_DATA で新 OS イメージを受信
2. CRC32 + 署名検証（Release のみ）
3. BootConfig を PENDING に更新
4. 再起動
5. Bootloader が新スロットを起動
6. OS 起動後に mark_boot_successful() を呼び、VALID に確定
```

**A/B が入らない場合（単一領域）**:
- 更新は「単一領域へ直接上書き」になる
- ロールバックは不可（失敗時はサポート復旧前提）
- 更新は **危険モード**でのみ許可し、ログを必須とする

---

## 単一領域向けの Rescue アップデータ方式

A/B が入らないターゲットでは、OS 更新の直前に **一時的な最小アップデータ（Rescue）**
をアプリ領域へ配置し、Bootloader からの復旧起動先として利用する。

### 流れ（ホスト主導）

```
1. Host が Rescue イメージを送信
2. Updater が App 領域に Rescue を書き込み
3. Host が OS イメージを送信
4. Updater が OS 領域を上書き更新
5. 再起動
6. Bootloader が OS 検証失敗時に Rescue を起動
```

### 前提と注意

- Rescue は **常駐しない**。OS 更新時にのみ一時配置する。
- App が一旦消えることは許容し、復旧はホスト側ツールで再投入する。
- Bootloader は **OS 検証失敗時に App 領域の Rescue を起動**できること。
- Rescue は最小機能（受信・検証・書き込み・再起動）に限定する。

### 成立条件（最低条件）

- Flash の消去単位が **3 つ以上**に分割できること  
  (1) Bootloader セクタ  
  (2) OS セクタ  
  (3) Rescue / App セクタ
- Bootloader が **OS 検証失敗時に Rescue を起動**できること
- 更新フローが **Rescue → OS** の順序を守ること

---

## データチャンク転送

### SysEx エンコーディング

MIDI SysEx は 7-bit データのみ送信可能。8-bit バイナリデータは 7-bit エンコーディングで転送する:

```
7 バイト入力 → 8 バイト出力
各 7 バイトの MSB を 1 バイトのヘッダに集約
```

### チャンクサイズ

- デフォルト: 256 バイト（エンコード後約 293 バイト）
- USB Full-Speed の SysEx パケットサイズ制限を考慮
- ACK を待ってから次のチャンクを送信（フロー制御）

---

## 検証

### CRC32

転送データ全体の CRC32 を計算し、FW_BEGIN で指定された期待値と比較する。
破損検出のみ（改ざん検出には不十分）。

### Ed25519 署名

Release ビルドの場合、カーネル内蔵の公開鍵で署名を検証する。
署名検証の詳細は [14-security.md](14-security.md) を参照。

---

## ロールバック

Flash 上に前バージョンのメタデータを保持し、FW_ROLLBACK コマンドで復元する。

```
Flash メタデータ領域:
  ├─ current_version
  ├─ current_crc32
  ├─ previous_version
  ├─ previous_offset
  └─ rollback_available: bool
```

---

## Bootloader 更新ポリシー（最終手段）

Bootloader は原則更新しない。致命的バグ対応など **最終手段**のみ、以下の条件で
OS から直接更新する。

- **サポート環境のみ**で実施（返送後に社内で実施）
- **署名必須**（Release ビルドの公開鍵で検証）
- **危険モード**（特別なコマンド/手順が必要）
- **電源条件を満たす場合のみ**実行（低電圧時は拒否）
- **ログ必須**（失敗時の原因追跡）

**注意**: 失敗した場合、デバイスが復旧不能になる可能性がある。

---

## SystemMode

Shell の `mode` コマンドまたは内部遷移で DFU モードに切り替える:

```cpp
enum class SystemMode : uint8_t {
    Normal     = 0,   // 通常動作
    Dfu        = 1,   // DFU モード（Updater アクティブ）
    Bootloader = 2,   // ブートローダーモード
    Safe       = 3,   // セーフモード（アプリ未ロード）
};
```

DFU モード中:
- アプリの実行を停止
- AudioTask を無効化
- Updater のみがアクティブ
- Shell は引き続き利用可能（進捗確認用）

---

## エラーハンドリング

| エラー | 対応 |
|--------|------|
| 消去中に FW_DATA 受信（EBUSY） | `FW_ERASE_DONE` を待つよう ACK で通知 |
| チャンク CRC 不一致 | 再送要求（ACK にエラーコード） |
| 転送中断 | タイムアウト（30秒）で自動キャンセル |
| 全体 CRC 不一致 | FW_VERIFY 失敗、書き込み済みデータを無効化 |
| 署名検証失敗 | FW_VERIFY 失敗、書き込み済みデータを無効化 |
| Flash 書き込みエラー | ACK にエラーコード、転送中断 |
| 電断 | 前バージョンが有効なまま（COMMITされるまで切り替わらない） |

---

## 関連ドキュメント

- [05-midi.md](05-midi.md) — SysEx トランスポート、7-bit エンコーディング
- [14-security.md](14-security.md) — Ed25519 署名検証、CRC32
- [17-shell.md](17-shell.md) — `mode dfu` コマンド
