# クリーンルーム実装 設計ドキュメント

リファレンスC実装の解析結果（[AUDIT.md](AUDIT.md)）に基づき、
公開仕様のみからC++23でゼロから再実装した設計と改善内容を記述する。

---

## 共通設計原則

| 原則 | 説明 |
|------|------|
| 仕様ベース実装 | Microsoft FAT仕様、littlefs SPEC.md のみに基づく |
| インスタンスメンバ | グローバル可変状態を完全排除。複数インスタンス安全 |
| ASSERT不使用 | 全チェックをif + エラーコード返却に |
| ヒープ不使用 | 全バッファはユーザー提供 or インスタンス内静的配列 |
| クリティカルパス最適化 | read/writeの関数呼び出し深度を最小化 |

---

## FATfs クリーンルーム実装

ソース: `fat/fat_core.cc`

### 内部構成

```
1. BPB定数（FAT仕様由来のオフセット定義）
2. エンディアンヘルパー（ld_u16/ld_u32/st_u16/st_u32）
3. セクタウィンドウ管理（sync_window, move_window）
4. FATテーブル操作（get_fat, put_fat — FAT12/16/32）
5. クラスタチェーン（create_chain, remove_chain — 循環検出付き）
6. ディレクトリ操作（dir_sdi, dir_next, dir_alloc, dir_read, dir_find, dir_register, dir_remove）
7. LFN処理（lfn_cmp, lfn_pick, lfn_put, gen_numname, sfn_sum, create_name）
8. パス解析（follow_path）
9. ボリューム管理（check_fs, find_volume, mount_volume）
10. 公開API実装（mount〜getfree）
```

### リファレンス欠陥の克服

| ID | 問題 | 対策 |
|----|------|------|
| F1 | f_rename クロスリンク窓 | 中間sync挿入でウィンドウを最小化 |
| F2 | グローバル可変状態 | 全状態をFatFsクラスメンバに封入 |
| F3 | 2nd FAT/FSInfo 戻り値無視 | 全disk_write戻り値をチェック、エラー伝搬 |
| F4 | 循環チェーン無限ループ | 全チェーン走査にn_fatentカウンタ上限を設定 |
| F5 | truncate サイズ不整合 | remove_chainエラー時はobjsize更新をスキップ |
| F6 | ヒープ割り当て | 完全排除。LFNバッファはスタック上に静的確保 |

### 設計の特徴

**セクタウィンドウ:**
512Bバッファ。セクタアラインかつフルセクタのread/writeではウィンドウを経由せず
ブロックデバイスに直接アクセスする（バイパス最適化）。

**FATテーブル:**
get_fat/put_fatでFAT12/16/32の3分岐。FAT12の1.5バイトパッキングは仕様通り実装。

**循環チェーン検出:**
全チェーン走査関数（remove_chain, lseek等）にn_fatentを上限とするカウンタを設置。
超過で`FR_DISK_ERR`を返却し、無限ループを防止。

**rename安全性:**
新エントリ作成 → sync → 旧エントリ削除の順序で、
クロスリンク窓を最小化。完全な排除はFAT仕様の構造的限界により不可能。

---

## littlefs クリーンルーム実装

ソース: `little/lfs_core.cc`
仕様: littlefs SPEC.md (v2.1 on-disk format)

### 内部構成

```
1. CRC-32（多項式0x04c11db7、4bitテーブル）
2. BD操作（bd_read, bd_prog, bd_erase — キャッシュ付き）
3. ブロックアロケータ（alloc_scan, alloc — ルックアヘッドビットマップ）
4. タグ操作（tag_type, tag_id, tag_size, make_tag — XOR連鎖）
5. メタデータペア読み取り（dir_fetchmatch, dir_get, dir_getinfo）
6. メタデータペア書き込み（dir_commit, dir_compact, dir_split）
7. CTZスキップリスト（ctz_index, ctz_find, ctz_extend, ctz_traverse）
8. ファイル操作（file_opencfg〜file_truncate）
9. ディレクトリ操作（dir_open〜dir_rewind）
10. FS操作（mkdir, remove, rename, stat, attr操作）
11. gstate管理（gstate_xor, deorphan, forceconsistency）
12. 公開API（format, mount, unmount, fs_gc, fs_grow等）
```

### リファレンス欠陥の克服

| ID | 問題 | 対策 |
|----|------|------|
| L1 | ASSERT 70箇所がリリースで無効 | 全てif + エラーリターンに置換 |
| L2 | dir_traverse 再帰でスタックオーバーフロー | 明示的スタック（固定深度）に変更 |
| L3 | lfs_npw2: a==1でUB | std::bit_widthで安全に実装 |
| L4 | lock/unlock NULL未検証 | LFS_THREADSAFEガードでstatic_assert |
| L5 | rcacheバッファ暗黙的再利用 | キャッシュドロップを明示化 |

### 設計の特徴

**タグXOR連鎖:**
32bitビッグエンディアンタグ `[valid(1)|type3(11)|id(10)|length(10)]`。
隣接タグとのXOR演算により、コミットログ内を双方向にイテレーション可能。

**メタデータペア:**
2ブロックの交互書き込み。各コミットにCRC-32とFCRC（forward CRC）を付与。
電源断時はCRC不正なコミットを無視し、直前の整合状態に自動復帰。

**CTZスキップリスト:**
ブロックnは`countr_zero(n)+1`個のポインタを持つ（n>0）。
O(log n)のランダムシークを実現。`std::countr_zero`で計算。

**ブロックアロケータ:**
ルックアヘッドビットマップでフリーブロックを管理。
CRC-XORベースのランダムシードで開始位置を決定し、ウェアレベリングを実現。

**gstate（グローバルステート）:**
2ディレクトリにまたがる操作（rename等）をXOR分散デルタで追跡。
マウント時に全メタデータペアを走査してgstateを復元し、
不完全な操作を自動的にロールバックする。

**dir_traverse の明示的スタック:**
リファレンスの再帰呼び出しを、固定深度の配列ベーススタックに変更。
ASSERTに依存せず、深度超過時はエラーコードを返却する。

---

## 実装結果

### サイズ

| | クリーンルーム | リファレンスC | 差分 |
|---|---|---|---|
| Flash合計 | 71,028 B | 69,768 B | +1.8% |
| littlefs .text | 17,928 B | — | — |
| FATfs .text | 10,637 B | — | — |
| littlefs 行数 | 6,259 | ~8,000 | -22% |

### 性能（ARM Cortex-M4, DWT_CYCCNT）

#### littlefs

| 操作 | クリーンルーム | リファレンスC | 比率 |
|------|-------------|-------------|------|
| format+mount | 89,520 cyc | 88,896 cyc | 1.007x |
| write 1KB | 218,420 cyc | 217,486 cyc | 1.004x |
| read 1KB | 40,889 cyc | 40,574 cyc | 1.008x |
| mkdir+stat x5 | 911,810 cyc | 903,503 cyc | 1.009x |

全操作で1%未満の差。実質同等性能。

#### FATfs

| 操作 | クリーンルーム | リファレンスC | 比率 |
|------|-------------|-------------|------|
| write 4KB | 57,225 cyc | 91,688 cyc | 0.624x |
| read 4KB | 25,723 cyc | 57,337 cyc | 0.449x |
| mkdir+stat x3 | 189,162 cyc | 162,691 cyc | 1.163x |

read/writeは大幅高速化。mkdir+statは安全性改善コストで16%遅い。

### テスト

| テスト | 結果 |
|-------|------|
| littlefs 単体 | 113/113 passed |
| FATfs 単体 | 96/96 passed |
| littlefs 比較（オンディスク互換性） | 33/33 passed |
| FATfs 比較（オンディスク互換性） | 27/27 passed |
| ARM Cortex-M4 統合 | 28/28 passed |

---

## 残課題

- Flash サイズ目標 < 69,768 B（現在 71,028 B、あと -1,260 B）
  - fs_gc/fs_grow の条件除外、dir_traverse 簡略化で削減可能
- Renode ベンチマーク再計測（littlefs クリーンルーム版のサイクル数）
- FATfs mkdir+stat 性能改善（現在 1.163x）
