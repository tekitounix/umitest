# UMI Library Development Guide

このディレクトリは、UMI エコシステムのライブラリに共通する標準・ガイド・リファレンスを集約しています。

---

## For New Contributors

1. [Getting Started](guides/GETTING_STARTED.md) — 新規ライブラリ段階的作成ガイド
2. [Coding Style](standards/CODING_STYLE.md) — コードの書き方
3. [Doxygen Style](standards/DOXYGEN_STYLE.md) — コメントの書き方

## Standards & Rules

- [Library Standard](standards/LIBRARY_STANDARD.md) — 全ライブラリ共通の構造・規約・著作権表記
- [Coding Style](standards/CODING_STYLE.md) — C++23スタイルガイド
- [Doxygen Style](standards/DOXYGEN_STYLE.md) — APIコメント規約

## Development Guides

- [Getting Started](guides/GETTING_STARTED.md) — 段階的ライブラリ作成（Phase 1-4）
- [Doxygen](guides/DOXYGEN.md) — Doxygen 運用・ローカル生成・CI設定
- [Testing](guides/TESTING.md) — テスト戦略
- [xmake](guides/XMAKE.md) — ビルドシステム
- [Release](guides/RELEASE.md) — リリース手順・CI・設定
- [Clang Tooling](guides/CLANG_TOOLING.md) — 静的解析・フォーマッタ
- [Debugging](guides/DEBUG_GUIDE.md) — デバッグ手法 (pyOCD, GDB, RTT)

## Archive

- [Implementation Plan](archive/IMPLEMENTATION_PLAN.md) — 移行計画（完了済み）
- [Quality Gap Report](archive/QUALITY_GAP_REPORT.md) — 品質ギャップ分析（完了済み）

## Reference Implementation

- [umibench](../umibench/) — 完全準拠リファレンス（すべての標準を満たすモデル実装）

## Standard-Compliant Libraries

| Library | Status | Description |
|---------|--------|-------------|
| [umibench](../umibench/) | Complete | Cross-target microbenchmark |
| [umitest](../umitest/) | Complete | Zero-macro test framework |
| [umimmio](../umimmio/) | Complete | MMIO register abstractions |
| [umirtm](../umirtm/) | Complete | RTT-compatible debug monitor |
| [umiport](../umiport/) | Complete | Shared platform infrastructure (STM32F4 startup, linker, UART) |
