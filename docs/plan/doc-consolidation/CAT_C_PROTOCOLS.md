# CAT_C: プロトコル — 統合内容要約

**カテゴリ:** C. プロトコル
**配置先:** `docs/umi-sysex/`（SysExプロトコル）+ `docs/umi-usb/`（USB Audio → Stage A で archive 移動）
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. カテゴリ概要

UMI デバイス間の通信プロトコル仕様群。MIDI SysEx ベースの独自プロトコル（UMI-SysEx）と USB Audio/MIDI の設計ガイドラインを含む。

**対象読者:** カーネル開発者、プロトコル実装者、USB ドライバ開発者
**主要な問題:** SysEx 内に概要版と詳細版の重複あり。USB Audio 文書は lib/umi/usb/docs/ にもあり分散。

---

## 2. 所属ドキュメント一覧

### 2.1 docs/umi-sysex/ — SysEx プロトコル（7ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 1 | UMI_SYSEX_OVERVIEW.md | ~53 | ★ | **保持** — 全プロトコルの目次・概要 |
| 2 | UMI_SYSEX_CONCEPT_MODEL.md | ~200 | ★ | **保持** — 概念モデル（Step/Pattern/Song） |
| 3 | UMI_SYSEX_DATA.md | ~49 | ◆ | **削除** → DATA_SPEC.md に統合 |
| 4 | UMI_SYSEX_DATA_SPEC.md | ~1916 | ★ | **保持（正本）** — 詳細仕様の集約先 |
| 5 | UMI_SYSEX_STATUS.md | ~100 | ◆ | **保持** — IMPL_NOTES を吸収して更新 |
| 6 | UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md | ~80 | ◆ | **削除** → STATUS.md に統合 |
| 7 | UMI_SYSEX_TRANSPORT.md | ~150 | ★ | **保持（正本）** — トランスポート共通仕様 |

### 2.2 docs/umi-usb/ — USB Audio（2ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 8 | USB_AUDIO.md | ~200 | ◆ | **archive移動** — lib/umi/usb/docs/ が最新 |
| 9 | USB_AUDIO_REDESIGN_PLAN.md | ~150 | ◆ | **archive移動** — lib/umi/usb/docs/design/ が最新 |

### 2.3 docs/archive/ — 旧プロトコル文書（削除対象）

| # | ファイル | 有用性 | 統廃合アクション |
|---|---------|--------|-----------------|
| 10 | archive/UMI_SYSEX_PROTOCOL.md | ▽ | **削除** — umi-sysex/ に統合済み |
| 11 | archive/UXMP提案書.md | ✗ | **削除** — 冒頭に「移行完了」記載 |
| 12 | archive/UXMP_SPECIFICATION.md | ✗ | **削除** — SysEx に統合済み |
| 13 | archive/UXMP_DATA_SPECIFICATION.md | ✗ | **削除** — DATA_SPEC.md に統合済み |
| 14 | archive/UMI_STATUS_PROTOCOL.md | ✗ | **削除** — 現行実装と不一致 |

---

## 3. ドキュメント別内容要約

### 3.1 UMI_SYSEX_OVERVIEW.md — プロトコル群概要

**バージョン:** 0.1.0 (Draft)

UMI-SysEx プロトコル群の目次。

**主要内容:**
- **プロトコル一覧** — UMI-STDIO, UMI-DFU, UMI-SHELL, UMI-TEST, UMI-STATUS, UMI-DATA（全て「仕様移行中」）
- **ドキュメント構成** — 各ファイルの役割
- **命名規則** — `UMI-<機能>`
- **UMI-DATA の位置づけ** — 内容仕様と搬送仕様の分離

---

### 3.2 UMI_SYSEX_CONCEPT_MODEL.md — 概念モデル

**バージョン:** 0.1.0 (Draft)

UMI-DATA のデータ構造を概念的に整理。

**主要内容:**
- **Step** — 純粋な値の配列（IDなし）。Dense/Sparse の2形式
- **Pattern** — ステップの集合。フレーズ交換単位。magic="UXPT"
- **Song** — パターンの集合。曲データの交換単位
- **ファイル交換ラッパー** — StepFile, PatternFile, SongFile の構造体定義
- **ParamDef** — パラメータ型・範囲・デフォルト値の10バイト構造体

---

### 3.3 UMI_SYSEX_DATA_SPEC.md — データ交換詳細仕様（正本）

**バージョン:** 0.8.0 (Draft) | **行数:** ~1916行

UMI-SysEx 最大の仕様文書。旧 UXMP-DATA からの移行版。

**主要内容:**
- **変更履歴** — 0.1.0〜0.8.0 の詳細な変更記録
- **バージョン互換ポリシー** — メジャー（後方互換なし）/ マイナー（後方互換あり）/ パッチ（完全互換）
- **DataBlock** — 自己完結型コンテナ。DataBlockHeader(28 bytes) + ParamDef[] + データ
- **ParamDef** — 型定義（NOTE, CC, GATE, VELOCITY, PITCH_BEND, PERCENTAGE 等）
- **StandardDefId** — 標準パラメータセット（MonoSeq, PolySeq, DrumSeq 等）
- **ベンダー拡張** — VendorParamDef、has_vendor_ext フラグ
- **JSON 表現（UXMP-JSON）** — Web アプリケーション向けの JSON フォーマット
- **MIDIマッピング** — パラメータ型から MIDI CC/Note/Velocity への自動マッピング
- **CRC32 計算規則** — 検証要件

**統廃合アクション:** UMI_SYSEX_DATA.md の内容（目次+運用方針の概要）を吸収して、DATA.md を削除

---

### 3.4 UMI_SYSEX_STATUS.md — 状態取得プロトコル

**バージョン:** 0.2.0 (Draft)

デバイスの状態取得・設定・ログ/メーター通知。

**主要内容:**
- **コマンド体系:**
  - System (0x20-0x2F): PING/PONG, RESET, VERSION, STATUS, IDENTITY
  - Audio Status (0x30-0x3F): AUDIO_STATUS, METER
  - Parameter (0x40-0x4F): PARAM_LIST, PARAM_GET/SET, PARAM_SUBSCRIBE/NOTIFY
- **IDENTITY_RES** — デバイス識別応答

**統廃合アクション:** IMPLEMENTATION_NOTES の実装メモ（ISR非干渉設計、SysEx処理アーキテクチャ、ストリーム解析API）を吸収

---

### 3.5 UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md — 実装ノート

**バージョン:** 0.2.0 (Draft)

STATUS プロトコルの実装ガイド。

**主要内容:**
- **リアルタイム非干渉** — SysEx は低優先度処理、演奏データ最優先
- **現状の問題と改善方針** — ISR内のSysEx組み立てをServerTaskに移行
- **SysEx処理アーキテクチャ** — ISR → raw_rx_queue → ServerTask → プロトコル分配
- **ストリーム解析API** — 1バイトずつ入力、タイムアウト破棄、バッファ上限超過破棄

**統廃合アクション:** STATUS.md に統合して削除

---

### 3.6 UMI_SYSEX_DATA.md — データ概要

**バージョン:** 0.8.0 (Draft) | **行数:** ~49行

UMI-DATA の目次的文書。

**主要内容:**
- DATA_SPEC.md への誘導
- データカテゴリ一覧（Preset, Pattern, Song, Sample, Wavetable, Project, Config, Custom）
- 7-bit エンコードとチャンク転送の概要

**統廃合アクション:** DATA_SPEC.md に統合して削除（目次的な49行は DATA_SPEC.md の冒頭に吸収可能）

---

### 3.7 UMI_SYSEX_TRANSPORT.md — トランスポート共通仕様

**バージョン:** 0.1.0 (Draft)

全 UMI-SysEx プロトコルの共通搬送層。

**主要内容:**
- **メッセージフレーム** — F0 + ManufacturerID + ProtocolID + Command + Sequence + Payload + Checksum + F7
- **Protocol ID** — 0x01(STDIO)〜0x06(DATA)、0x10-0x1F(予約)、0x20-0x7F(ベンダー)
- **入力バリデーション** — WebMIDI/USB 外部入力の必須検証項目
- **Manufacturer ID** — 3バイト形式の扱い
- **7-bit エンコード** — バイナリデータのMIDI SysEx互換エンコード
- **チェックサム** — XOR ベース

---

### 3.8 USB_AUDIO.md — USB Audio & MIDI 設計ガイドライン

USB Audio (UAC1/UAC2) と MIDI の実装戦略文書。

**主要内容:**
- **推奨構成マトリクス** — UAC1(互換性重視) vs UAC2(高音質)
- **UAC1 詳細** — FS帯域制限計算（96kHz/24bit/Duplexは不可能）、Adaptive推奨
- **UAC2 詳細** — Async推奨、192kHz対応
- **MIDI** — MIDI 1.0/Bulk転送が最も安定
- **物理切り替え** — スイッチでUAC1/UAC2モード切替、PID変更

**統廃合アクション:** lib/umi/usb/docs/ の方が最新のため archive 移動

---

## 4. カテゴリ内の関連性マップ

```
UMI-SysEx プロトコル群
│
├── UMI_SYSEX_OVERVIEW.md ← 目次
│
├── UMI_SYSEX_TRANSPORT.md ← 共通搬送層（全プロトコルが依存）
│     │
│     ├── UMI_SYSEX_STATUS.md ← 状態/メーター/パラメータ
│     │     └── UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md ← 吸収先
│     │
│     └── UMI_SYSEX_DATA.md ← 目次（吸収先 → DATA_SPEC）
│           └── UMI_SYSEX_DATA_SPEC.md ← 詳細仕様（正本）
│                 └── UMI_SYSEX_CONCEPT_MODEL.md ← 概念定義
│
USB Audio/MIDI
│
├── docs/umi-usb/USB_AUDIO.md ← archive移動
└── lib/umi/usb/docs/ ← 正本（CAT_G で管理）

旧プロトコル（全削除）
├── archive/UXMP提案書.md ← 移行完了
├── archive/UXMP_SPECIFICATION.md ← DATA_SPECに統合済み
├── archive/UXMP_DATA_SPECIFICATION.md ← 同上
├── archive/UMI_SYSEX_PROTOCOL.md ← umi-sysex/に統合済み
└── archive/UMI_STATUS_PROTOCOL.md ← 現行不一致
```

---

## 5. 統廃合アクション

### Phase A-3 実行項目（SysEx/プロトコル文書の整理）

| ステップ | アクション | 対象 |
|---------|-----------|------|
| A-3.1 | DATA.md の49行を DATA_SPEC.md 冒頭に統合、DATA.md を削除 | umi-sysex/ |
| A-3.2 | IMPLEMENTATION_NOTES の内容を STATUS.md に統合、NOTES を削除 | umi-sysex/ |
| A-3.3 | archive 内の旧プロトコル文書5件を削除 | archive/ |

### Phase A-5 実行項目（USB/MIDI の住み分け）

| ステップ | アクション | 対象 |
|---------|-----------|------|
| A-5.1 | docs/umi-usb/ の2ファイルを archive に移動 | umi-usb/ |
| A-5.2 | lib/umi/usb/docs/ は Stage B Phase 3 で新構造に移行されるため、現時点では正本の確立のみ | lib/umi/usb/docs/ |

### 統合後の構成

```
docs/umi-sysex/          # 統合後: 5ファイル（現在7 → 2削除）
├── UMI_SYSEX_OVERVIEW.md
├── UMI_SYSEX_CONCEPT_MODEL.md
├── UMI_SYSEX_DATA_SPEC.md   # DATA.md の内容を吸収
├── UMI_SYSEX_STATUS.md      # IMPL_NOTES の内容を吸収
└── UMI_SYSEX_TRANSPORT.md
```

---

## 6. 品質評価

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★☆ | SysEx は体系的。USB Audio は lib 側に分散 |
| 一貫性 | ★★★☆☆ | バージョニング統一。ただし DATA/DATA_SPEC の並存が混乱を招く |
| 更新頻度 | ★★★★☆ | 2026-01 に集中的に更新。活発に改訂中 |
| 読みやすさ | ★★★★☆ | バイナリフォーマットが明確。コマンド表が見やすい |
| コードとの整合 | ★★★☆☆ | 「仕様移行中」が多く、実装との差分確認が必要 |

---

## 7. 推奨事項

1. **DATA.md と STATUS_IMPLEMENTATION_NOTES の即時統合** — 混乱の元。2ファイル削除でスッキリ
2. **UXMP 残骸の即時削除** — 「移行完了」と明記されている。保持する理由がない
3. **USB Audio の正本確定** — lib/umi/usb/docs/ を正本とし、docs/umi-usb/ は archive
4. **「仕様移行中」ステータスの更新** — 実装状況を反映した正確なステータスに
5. **SysEx プロトコルのバージョン統一** — OVERVIEW/TRANSPORT は 0.1.0、STATUS は 0.2.0、DATA_SPEC は 0.8.0 とバラバラ
