# Release Policy

## Current Release Line

| Version | Status | Date |
|---------|--------|------|
| 0.1.0   | beta   | 2026-02-07 |

## Versioning

Semantic Versioning (SemVer). 0.x はベータ: マイナーバージョンで破壊的変更あり。

## Release Checklist

1. VERSION 更新
2. CHANGELOG.md 更新
3. `xmake test` 全テスト通過
4. `xmake release --ver=X.Y.Z --libs=umitest` 実行
5. git tag `umitest-vX.Y.Z`
