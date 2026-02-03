# umifs — UMI Filesystem Library

組込みシステム向けファイルシステムライブラリ。
FATfs と littlefs の2つのファイルシステムを C++23 クリーンルーム実装で提供する。

## 特徴

- リファレンスC実装の全欠陥を克服した独自実装
- ヒープ割り当て不要（全バッファはユーザー提供）
- グローバル可変状態なし（複数インスタンス安全）
- 全エラーパスでエラーコード返却（ASSERT不使用）
- C++23（constexpr, enum class, concepts）

## ファイル構成

```
lib/umifs/
├── fat/
│   ├── fat_core.cc        クリーンルーム FAT12/16/32 実装
│   ├── ff.hh              公開API（FatFsクラス）
│   ├── ff_config.hh       設定定義
│   ├── ff_diskio.hh       ブロックデバイスI/Oインターフェース
│   ├── ff_types.hh        型定義、enum class
│   ├── ff_unicode.cc      CP437 ⇔ Unicode変換
│   └── ff_unicode.hh
├── little/
│   ├── lfs_core.cc        クリーンルーム littlefs v2.1 実装
│   ├── lfs.hh             公開API（Lfsクラス）
│   ├── lfs_config.hh      設定・BlockDeviceアダプタ
│   ├── lfs_types.hh       型定義、内部構造体
│   └── lfs_util.hh        ユーティリティ（CRC, endian等）
├── docs/
│   ├── README.md           本ドキュメント
│   ├── AUDIT.md            リファレンス実装の解析レポート
│   └── DESIGN.md           クリーンルーム実装の設計と改善内容
├── test/
│   ├── TEST_REPORT.md      テスト結果・ベンチマーク
│   ├── test_lfs.cc         littlefs 単体テスト (113)
│   ├── test_fat.cc         FATfs 単体テスト (96)
│   ├── test_lfs_compare.cc littlefs 比較テスト (33)
│   ├── test_fat_compare.cc FATfs 比較テスト (27)
│   ├── renode_fs_test.cc   ARM Cortex-M4 統合テスト (28)
│   └── xmake.lua           テストビルド設定
└── xmake.lua               ライブラリビルド設定
```

## API概要

### littlefs (Lfs クラス)

```cpp
#include "little/lfs.hh"
using namespace umi::fs;

Lfs lfs;
LfsConfig cfg = make_lfs_config(block_device, cache_size, lookahead_size,
                                 read_buf, prog_buf, lookahead_buf);

lfs.format(&cfg);         // フォーマット
lfs.mount(&cfg);          // マウント

LfsFile file;
lfs.file_open(&file, "/hello.txt", LFS_O_WRONLY | LFS_O_CREAT);
lfs.file_write(&file, data, size);
lfs.file_close(&file);

lfs.unmount();
```

**主要API:** format, mount, unmount, file_open/close/read/write/seek/truncate/sync,
mkdir, dir_open/close/read, remove, rename, stat, getattr/setattr/removeattr,
fs_stat, fs_size, fs_traverse, fs_gc, fs_grow

### FATfs (FatFs クラス)

```cpp
#include "fat/ff.hh"
using namespace umi::fs;

FatFs fs;
fs.mount(block_device);   // マウント

FatFile file;
fs.open(&file, "/hello.txt", FA_WRITE | FA_CREATE_NEW);
fs.write(&file, data, size, &written);
fs.close(&file);

fs.unmount();
```

**主要API:** mount, unmount, open/close/read/write/lseek/truncate/sync,
mkdir, opendir/closedir/readdir, unlink, rename, stat, getfree

## 安全性

### littlefs

- **電源遮断耐性:** COW + CRC-32 + gstate による3層防御。次回マウントで自動復旧
- **データ検証:** 全メタデータ読み込みでCRC検証
- **ウェアレベリング:** ブロックリロケーションによる動的ウェアレベリング

### FATfs

- **電源遮断耐性:** FAT仕様の構造的限界により保証なし
- **推奨運用:** mutex排他制御 + sync() 頻繁呼び出し + 追記パターン
- **循環チェーン検出:** 全チェーン走査にカウンタ上限を設定

**選定指針:**
- 信頼性最優先 → littlefs
- FAT互換性が必要 → FATfs（排他制御必須）
- いずれもISRからの直接操作は禁止

## ビルド

```bash
# ホストテスト
xmake build test_fs_lfs test_fs_fat
xmake run test_fs_lfs       # 113/113
xmake run test_fs_fat       # 96/96

# 比較テスト（リファレンスC実装との等価性検証）
xmake build test_fs_lfs_compare test_fs_fat_compare
xmake run test_fs_lfs_compare   # 33/33
xmake run test_fs_fat_compare   # 27/27

# ARM Cortex-M4 統合テスト（Renode）
xmake build renode_fs_test
xmake run renode_fs_test        # 28/28
```

## ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [AUDIT.md](AUDIT.md) | リファレンス実装（FATfs R0.16, littlefs v2.11）の解析レポート |
| [DESIGN.md](DESIGN.md) | クリーンルーム実装の設計方針・改善内容・実装詳細 |
| [test/TEST_REPORT.md](../test/TEST_REPORT.md) | テスト結果・ベンチマーク・テストカバレッジ |
