# littlefs クリーンルーム再実装計画

## 背景

現在の `little/lfs_core.cc` は、リファレンス実装 (`.refs/littlefs/lfs.c`) と
30文字以上の同一行が約1,089行存在する。これではクリーンルーム実装と主張できないため、
SPEC.md の仕様のみを参照して完全に書き直す。

FATfs (`fat/fat_core.cc`) はリファレンスとの同一行が0行であり、問題なし。

## 目標

- リファレンスコード（`.refs/littlefs/lfs.c`）との30文字以上の同一行を50行未満に削減
- 全テスト通過（113 unit + 33 compare + 28 Renode）
- オンディスクフォーマット互換維持（littlefs v2.1）
- C++23 モダンスタイル
- MIT ライセンス

## 設計方針

### 命名規則の変更

| カテゴリ | 旧名 (リファレンス由来) | 新名 |
|---------|----------------------|------|
| 型エイリアス | `lfs_t*` | `Fs&` (= `Lfs&`) |
| BD操作 | `lfs_bd_read` | `blk_read` |
| BD操作 | `lfs_bd_prog` | `blk_write` |
| メタデータ | `lfs_dir_fetch` | `mdir_fetch` |
| メタデータ | `lfs_dir_commit` | `mdir_commit` |
| メタデータ | `lfs_dir_compact` | `mdir_compact` |
| メタデータ | `lfs_dir_traverse` | `mdir_traverse` |
| CTZリスト | `lfs_ctz_find` | `skiplist_find` |
| CTZリスト | `lfs_ctz_extend` | `skiplist_extend` |
| FS操作 | `lfs_rawremove_` | `do_remove` |
| FS操作 | `lfs_rawrename_` | `do_rename` |
| FS操作 | `lfs_rawmkdir_` | `do_mkdir` |
| マクロ | `LFS_MKTAG(...)` | `make_tag()` constexpr |
| 定数 | `LFS_ERR_xxx` | `err::xxx` namespace |
| 定数 | `LFS_TYPE_xxx` | `ty::xxx` namespace |
| 定数 | `LFS_O_xxx` | `fl::xxx` namespace |

### コード構造の変更

- 全マクロを `constexpr` 関数に置換
- エラーコードを `namespace err {}` に集約
- タグ型定数を `namespace ty {}` に集約
- ファイルフラグを `namespace fl {}` に集約
- 内部構造体を `CamelCase` に（`MdirAttr`, `DiskOffset`, `ForwardCrc`, `CommitCtx` 等）
- 関数引数を `Fs&` 参照渡しに統一（ポインタ不使用）
- `auto` の積極使用
- `static_cast` のみ使用（C キャスト不使用）

## ファイル分割構成（実装パート）

| パート | 行数目安 | 内容 |
|-------|---------|------|
| Part 1 | ~750 | CRC, 定数/namespace, タグconstexpr関数, BD層, アロケータ |
| Part 2 | ~850 | メタデータ読み取り: mdir_fetch, mdir_get_slice, mdir_find, mdir_traverse |
| Part 3 | ~1000 | メタデータ書き込み: mdir_commit, mdir_compact, mdir_split |
| Part 4 | ~900 | CTZスキップリスト, ファイル操作全般 |
| Part 5 | ~800 | ディレクトリ操作, mkdir/remove/rename/stat/attrs |
| Part 6 | ~800 | format/mount/unmount, fs_gc/grow, deorphan, 公開API |

## 検証方法

```bash
# 1. ビルド・テスト
xmake build test_fs_lfs && xmake run test_fs_lfs           # 113/113
xmake build test_fs_lfs_compare && xmake run test_fs_lfs_compare  # 33/33

# 2. 同一行チェック（30文字以上、目標 < 50行）
comm -12 \
  <(sed 's/^[ \t]*//' .refs/littlefs/lfs.c | sort -u) \
  <(sed 's/^[ \t]*//' lib/umifs/little/lfs_core.cc | sort -u) \
  | awk 'length > 30' | grep -v '^//' | grep -v '^\*' | grep -v '^#' | wc -l

# 3. Flash サイズ
xmake build renode_fs_test  # 目標: < 72,000 B
```

## 進捗

- [x] Part 1: CRC, 定数, タグ, BD層, アロケータ
- [ ] Part 2: メタデータ読み取り
- [ ] Part 3: メタデータ書き込み
- [ ] Part 4: CTZスキップリスト, ファイル操作
- [ ] Part 5: ディレクトリ操作, FS操作
- [ ] Part 6: format/mount/unmount, 公開API
- [ ] 結合・ビルド・テスト
- [ ] 同一行チェック・最終修正
