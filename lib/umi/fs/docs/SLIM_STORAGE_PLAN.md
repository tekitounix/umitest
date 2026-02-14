# SlimFS — StorageService 適合のための機能拡張計画

## 背景

[19-storage-service.md](../../../../docs/umios-architecture/04-services/19-storage-service.md) は内蔵/SPI Flash 向けに littlefs を採用する設計で、以下を要件としている：

- 電断安全性（COW メカニズム）
- ウェアレベリング
- BlockDevice 抽象化との統合
- fd テーブル管理（最大4、自動クローズ）

SlimFS は基本的な FS 操作（open/read/write/close/seek/stat/mkdir/remove/rename）を実装済みだが、StorageService 要件に対して以下5点が不足している。

fd テーブル管理と自動クローズは StorageService 層（カーネル側）の責務とする。SlimFS はファイルシステムライブラリとして OS 非依存を維持する。

---

## 不足機能と設計

### 1. ジオメトリ API

**目的:** StorageService の BlockDevice インターフェースと整合する情報取得 API。

**API 追加:**

```cpp
// slim.hh — SlimFs クラスに追加
[[nodiscard]] uint32_t block_size() const noexcept;
[[nodiscard]] uint32_t block_count_total() const noexcept;
```

**実装:** `cfg->block_size` / `block_count` を返すだけ。

---

### 2. pending_move 復旧の完全実装

**目的:** rename 途中で電断した場合の整合性復旧。

**現状:** mount 時に `pending_move.clear()` のみ（「Phase 4 で実装」コメント）。

**設計:**

mount 時に pending_move が active の場合：

1. `pending_move.dst_dir` の `pending_move.dst_id` にエントリが存在するか確認
2. **存在する場合:** rename の dst 書き込みは完了。src エントリを削除して rename を完了
3. **存在しない場合:** rename は途中で中断。pending_move をクリアして元の状態を維持

```
mount():
  if pending_move.active():
    dst_entry = dir_find_by_index(dst_dir, dst_id)
    if dst_entry exists:
      meta_delete_entry(src_dir, src_entry_off)  // src を削除して完了
    pending_move.clear()
    superblock_write(A)
    superblock_write(B)
```

---

### 3. ウェアレベリング

**目的:** Flash ブロックの書き込み回数を均等化し、デバイス寿命を延長。

**現状の問題:** `alloc_block()` は lookahead bitmap の先頭から線形探索するため、低番号ブロックに書き込みが集中する。

**設計（littlefs 方式の軽量アプローチ）:**

#### 3a. 動的ウェアレベリング — 割り当て分散

alloc_block の開始位置をランダム化し、ブロック割り当てを分散：

```
alloc_block():
  // alloc_seed で開始位置を分散（LCG: Linear Congruential Generator）
  alloc_seed = alloc_seed * 1103515245 + 12345
  start_offset = alloc_seed % block_count
  // start_offset から lookahead を巻き付き探索
```

alloc_seed はスーパーブロックに永続化（既に存在するが未使用）。

#### 3b. lookahead 巻き付き探索

現在の alloc_block は `la.next` から `la.size` まで一方向に探索して NOSPC を返す。ブロック全体を巡回する探索に拡張：

```
alloc_block():
  // lookahead window を alloc_next から開始し、見つからなければ window をスライド
  // 全ブロックを巡回しても空きがなければ NOSPC
```

#### 3c. 静的ウェアレベリング（将来拡張）

現時点では実装しない。理由：
- 各ブロックの erase count を追跡するにはメモリまたは専用ブロックが必要
- SlimFS のコンパクトさを維持するため、動的ウェアレベリングを優先

---

### 4. COW ファイル書き込み

**目的:** 書き込み中の電断でファイルデータが壊れないことを保証。

**現状の問題:** `file_write()` は既存データブロックを直接 `bd_prog()` で上書き。書き込み中に電断するとブロック内容が不定になる。

**設計:**

#### 書き込みフロー（COW）

1. **新ブロックを割り当て** → データを書き込み
2. **チェーンのリンクを更新** → 新ブロックを前ブロックの next に設定
3. **file_sync 時にメタデータをアトミック更新** → dir entry の head_block/size を更新

既存ブロックは一切上書きしない。sync 完了後に旧ブロックを解放。

#### 具体的な変更

SlimFile に COW 用の追跡状態を追加：

```cpp
struct SlimFile {
    // ... 既存フィールド ...
    uint32_t new_head_block = SLIM_BLOCK_NULL;  // COW: 新チェーンの先頭
    uint32_t old_head_block = SLIM_BLOCK_NULL;  // COW: 旧チェーンの先頭（sync 後に解放）
};
```

**file_write の変更:**
- 新ブロックのみに書き込む（既存ブロック上書き禁止）
- file.new_head_block を追跡

**file_sync の変更:**
- dir entry を new_head_block で更新（アトミック）
- 成功後に old_head_block のチェーンを解放

**電断シナリオ:**
- sync 前の電断 → 新ブロックは孤立（次回 mount の alloc_scan で未参照 → 再利用可能）
- sync 中の電断 → dir entry は旧 head_block のまま（COW で旧データ保全）

---

### 5. close_all()

**目的:** StorageService がアプリ Fault/Exit 時にオープン中ファイルを一括クローズする手段。

**API 追加:**

```cpp
// slim.hh
/// 外部から渡された SlimFile 配列を一括 sync + close する。
/// StorageService が管理する fd テーブルの全エントリに対して呼ぶ。
void close_all(std::span<SlimFile> files) noexcept;
```

**実装:** 各 file に対して `file_sync` → `file = SlimFile{}` をループ。エラーは無視（Fault 時なので best-effort）。

---

## 実装順序

| 順序 | 機能 | 影響範囲 | 難度 | 状態 |
|------|------|----------|------|------|
| 1 | ジオメトリ API | slim.hh, slim_core.cc | 低 | ✓ 完了 |
| 2 | pending_move 復旧 | slim_core.cc (mount) | 低 | ✓ 完了 |
| 3 | close_all() | slim.hh, slim_core.cc | 低 | ✓ 完了 |
| 4 | ウェアレベリング | slim_core.cc (alloc) | 中 | ✓ 完了 |
| 5 | COW ファイル書き込み | slim_types.hh, slim_core.cc | 高 | ✓ 完了 |

---

## 追加改善（製品レベル化）

| 項目 | 内容 | 状態 |
|------|------|------|
| COW 後方データ保全 | seek+write 時に書き込み範囲後方の既存データを COW コピー | ✓ 完了 |
| COW エラーリカバリ | NOSPC 時に新チェーンを解放し旧チェーンを復元 | ✓ 完了 |
| file_read COW ガード | dirty && old_head_block 状態での read を BADF で拒否 | ✓ 完了 |
| file_truncate COW 対応 | dirty 状態では先に sync してから truncate 実行 | ✓ 完了 |
| cow_prepare_block | file_write の COW ロジックをヘルパー関数に分離 | ✓ 完了 |
| デッドコード除去 | 未使用の cow_copy_and_write 削除、rc=0 デッドコード削除 | ✓ 完了 |

### 制約事項

- **RDWR モードでの write 後 read**: COW 中は新チェーンが不完全なため、dirty 状態での read は非対応（BADF を返す）。StorageService は RDONLY/WRONLY のみ使用するため実用上問題なし。

---

## テスト結果

**169/169 通過** (84 既存 + 85 fault injection + 22 新規)

### 新規テスト一覧

| テスト | 検証内容 |
|--------|----------|
| test_geometry_api | block_size/block_count が config と一致 |
| test_close_all | 複数ファイル open → close_all → 全 close |
| test_pending_move_recovery_complete | rename 途中電断 → mount → rename 完了 |
| test_pending_move_recovery_incomplete | rename 未開始電断 → mount → 元の状態 |
| test_wear_leveling_distribution | 繰り返し alloc/free で特定ブロックに集中しない |
| test_cow_write_power_loss | 書き込み中電断 → mount → 旧データ保全 |
| test_cow_write_sync | 正常書き込み → sync → データ反映 |
| test_multi_write_same_block | 同一ブロック内への複数 write |
| test_multi_write_cross_block | ブロック境界を跨ぐ連続 write |
| test_overwrite_preserves_surrounding | 上書き時に前後のデータが保全される |
| test_cow_multi_block_file | マルチブロックファイルの COW 上書き |
| test_cow_truncate_during_dirty | dirty 状態での truncate |
| test_rdwr_read_after_write | RDWR で write 後 read がエラーを返す |
| test_multiple_files_cow | 複数ファイル同時 COW |
| test_write_empty_buf | サイズ 0 の write |
| test_seek_beyond_eof | EOF を超える seek |
| test_rename_overwrite | rename で既存ファイルの上書き |
| test_deeply_nested_dir | 多段ネストディレクトリ |
| test_many_files_in_dir | ディレクトリ内多数ファイル |
| test_remove_then_create | remove 直後の同名 create |
| test_file_sync_explicit | 明示的 sync のデータ永続化 |
| test_double_close | 二重 close が安全 |
| test_open_flags_excl | CREAT\|EXCL で既存ファイルがエラー |
| test_boundary_block_size_write | ブロックサイズぴったりの write |
| test_full_disk_write | ディスク容量限界までの write |
| test_full_disk_mkdir | ディスク容量限界での mkdir |

---

## 検証コマンド

```bash
xmake build test_fs_slim && xmake run test_fs_slim
```
