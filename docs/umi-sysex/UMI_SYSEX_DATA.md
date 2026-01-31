# UMI-DATA 概要（ドラフト）

バージョン: 0.8.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-29

---

## 1. 目的と範囲

UMI-DATA はプリセット、パターン、ソング、サンプル等の**ユーザーデータ交換**を標準化する。
搬送は [UMI_SYSEX_TRANSPORT.md](UMI_SYSEX_TRANSPORT.md) の共通規約に従う。

---

## 2. 位置づけ

- **内容仕様**: UMI_SYSEX_DATA_SPEC.md に集約
- **搬送仕様**: UMI_SYSEX_TRANSPORT.md に集約
- 本書は内容仕様の目次と運用方針の概要のみを扱う

---

## 3. データカテゴリ

| Category | ID | 説明 |
|---|---|---|
| Preset | 0x01 | シンセパッチ |
| Pattern | 0x02 | シーケンスパターン |
| Song | 0x03 | ソングデータ |
| Sample | 0x04 | オーディオサンプル |
| Wavetable | 0x05 | ウェーブテーブル |
| Project | 0x06 | プロジェクト全体 |
| Config | 0x07 | 設定データ |
| Custom | 0x10-0x7F | ベンダー固有 |

---

## 4. UMI-DATA と SysEx

- UMI-DATA のバイナリは 7-bit エンコードして SysEx Payload に格納する。
- チャンク転送・ACK は UMI-DATA 仕様で定義し、トランスポートのフロー制御と併用する。

---

## 5. 参照

- 詳細仕様: [UMI_SYSEX_DATA_SPEC.md](UMI_SYSEX_DATA_SPEC.md)
