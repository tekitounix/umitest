# UMI-SysEx プロトコル群 概要

バージョン: 0.1.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-29

---

## 1. 目的

UMI-SysEx は MIDI 1.0 の SysEx を用いて、UMI/互換デバイス間の通信を標準化する軽量プロトコル群である。
実装負荷を抑えつつ、ファーム更新・状態監視・ユーザーデータ交換などを共通化する。

---

## 2. プロトコル一覧

| 略称 | 用途 | 状態 |
|---|---|---|
| UMI-STDIO | 標準I/O転送 | 仕様移行中 |
| UMI-DFU | ファーム更新 | 仕様移行中 |
| UMI-SHELL | 対話シェル | 仕様移行中 |
| UMI-TEST | 自動テスト | 仕様移行中 |
| UMI-STATUS | 状態/ログ/メーター | 仕様移行中 |
| UMI-DATA | ユーザーデータ交換 | 仕様移行中 |

---

## 3. ドキュメント構成

| ドキュメント | 内容 |
|---|---|
| UMI_SYSEX_TRANSPORT.md | SysExトランスポート共通仕様 |
| UMI_SYSEX_STATUS.md | UMI-STATUS 仕様 + 実装ガイドライン |
| UMI_SYSEX_DATA_SPEC.md | UMI-DATA 詳細仕様（概要・カテゴリ定義を含む） |
| UMI_SYSEX_CONCEPT_MODEL.md | 概念モデル整理 |

---

## 4. 命名規則

- プロトコル名: UMI-<機能>
- 例: UMI-STATUS, UMI-DATA

---

## 5. UMI-DATA の位置づけ

UMI-DATA は**内容仕様**であり、搬送は UMI_SYSEX_TRANSPORT.md の規約に従う。
内容仕様の詳細は UMI_SYSEX_DATA_SPEC.md に集約する。
