# umifs テストレポート

最終更新: 2026-02-02
ツールチェーン: Apple clang (ホスト), clang-arm 19.1.5 (Renode/ARM)

## テスト一覧

| ターゲット | 種別 | テスト数 | 内容 |
|-----------|------|---------|------|
| `test_fs_lfs` | ホスト | 113 | littlefs 単体テスト |
| `test_fs_fat` | ホスト | 96 | FATfs 単体テスト |
| `test_fs_lfs_compare` | ホスト | 33 | littlefs vs 参照C 等価性+ベンチマーク |
| `test_fs_fat_compare` | ホスト | 27 | FATfs vs 参照C 等価性+ベンチマーク |
| `renode_fs_test` | Renode | 28 | ARM Cortex-M4 統合テスト+DWTベンチマーク |
| `renode_fs_test_ref` | Renode | 28 | 参照C ARM Cortex-M4 統合テスト+DWTベンチマーク |

## 実行結果

### ホストテスト

```
test_fs_lfs:          113/113 passed
test_fs_fat:           96/96 passed
test_fs_lfs_compare:   33/33 passed
test_fs_fat_compare:   27/27 passed
```

### Renodeテスト (STM32F407VG, Cortex-M4)

```
renode_fs_test:      28/28 passed
renode_fs_test_ref:  28/28 passed
```

## 実行方法

```bash
# ホストテスト
xmake build test_fs_lfs test_fs_fat
xmake run test_fs_lfs
xmake run test_fs_fat

# 比較テスト（リファレンスC実装との等価性検証）
xmake build test_fs_lfs_compare test_fs_fat_compare
xmake run test_fs_lfs_compare
xmake run test_fs_fat_compare

# Renodeテスト
xmake build renode_fs_test
renode --console --disable-xwt -e "include @lib/umifs/test/fs_test.resc"
robot lib/umifs/test/fs_tests.robot

# 参照C Renodeテスト
xmake build renode_fs_test_ref
renode --console --disable-xwt -e "include @lib/umifs/test/fs_test_ref.resc"
```

## ホストベンチマーク (クリーンルーム vs 参照C)

### littlefs

| 操作 | クリーンルーム | 参照C | 比率 |
|------|-------------|-------|------|
| write 1KB | 3.1 us | 3.2 us | 0.98x |
| read 1KB | 0.3 us | 0.3 us | 1.04x |
| write 32KB random | 46.1 us | 43.7 us | 1.05x |
| read 32KB random | 14.6 us | 14.3 us | 1.02x |
| format+mount | 2.4 us | 2.5 us | 0.97x |
| mkdir+stat x5 | 21.8 us | 22.0 us | 0.99x |

### FATfs

| 操作 | クリーンルーム | 参照C | 比率 |
|------|-------------|-------|------|
| write 4KB | 0.3 us | 0.2 us | 1.23x |
| read 4KB | 0.1 us | 0.1 us | 1.17x |
| write 64KB random | 0.6 us | 2.2 us | 0.25x |
| read 64KB random | 0.5 us | 1.9 us | 0.25x |
| mount | 0.0 us | 0.0 us | 0.98x |

## Renodeベンチマーク (DWT_CYCCNT, Cortex-M4)

### Flashサイズ

| | クリーンルーム | 参照C | 差分 |
|---|---|---|---|
| Flash | 71,028 B | 69,768 B | +1.8% |
| RAM | 123,704 B | 123,720 B | 同等 |

### littlefs

| 操作 | クリーンルーム | 参照C | 比率 |
|------|-------------|-------|------|
| format+mount | 89,520 cyc | 88,896 cyc | 1.007x |
| write 1KB | 218,420 cyc | 217,486 cyc | 1.004x |
| read 1KB | 40,889 cyc | 40,574 cyc | 1.008x |
| mkdir+stat x5 | 911,810 cyc | 903,503 cyc | 1.009x |

### FATfs

| 操作 | クリーンルーム | 参照C | 比率 |
|------|-------------|-------|------|
| write 4KB | 57,225 cyc | 91,688 cyc | 0.624x |
| read 4KB | 25,723 cyc | 57,337 cyc | 0.449x |
| mkdir+stat x3 | 189,162 cyc | 162,691 cyc | 1.163x |

## テストカバレッジ

### littlefs (113テスト)

- フォーマット・マウント・リマウント
- ファイル CRUD、空ファイル、大ファイル（マルチブロック）
- seek / tell / truncate / sync
- ディレクトリ操作（mkdir / readdir / seek / rewind）
- rename / remove（ファイル・ディレクトリ）
- カスタムアトリビュート
- FS統計 / traverse / GC / mkconsistent
- オープンフラグ（RDONLY, WRONLY, CREAT, EXCL, APPEND, TRUNC）
- エッジケース: フルディスク、境界書き込み、上書き、フラグメンテーション、最大ファイル名長

### FATfs (96テスト)

- マウント・アンマウント
- ファイル CRUD、オープンモード（CREATE_NEW, OPEN_ALWAYS, APPEND）
- lseek / truncate / sync
- ディレクトリ操作（mkdir / readdir / stat）
- unlink / rename / getfree
- LFN（ロングファイルネーム）
- 大ファイル（マルチクラスタ）、多数ファイル
- リードオンリーアクセス拒否
- エッジケース: フルディスク、境界書き込み、上書き、フラグメンテーション、ネストディレクトリ

### 比較テスト (60テスト)

- 機能等価性: 同一操作の結果がクリーンルームと参照C実装で一致
- クロスリード: 一方で書いたデータを他方で読めること（バイナリ互換性）
- パフォーマンス比較: 各操作のホスト実行時間比
