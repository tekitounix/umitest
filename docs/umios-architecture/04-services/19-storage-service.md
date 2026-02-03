# 19 — StorageService

## 概要

ファイルシステムの非同期アクセスサービス。
SystemTask 上で動作し、アプリからの FS syscall を逐次処理する。

| 項目 | 状態 |
|------|------|
| Syscall インターフェース（番号定義） | 新設計 |
| StorageService（要求キュー） | 新設計 |
| slimfs 統合（内蔵/SPI Flash） | 新設計 |
| FATfs 統合（SD カード） | 新設計 |
| BlockDevice 抽象化 | 新設計 |

---

## アーキテクチャ

```
App (ControlTask)
  │
  │  fs::open() / read() / write() / close()
  │  → SVC → 要求キュー → 即 return
  ▼
StorageService (SystemTask)
  │
  │  要求受信 → FS 操作 → 完了通知
  ▼
ファイルシステム
  ├─ slimfs (内蔵/SPI Flash)
  └─ FATfs (SD カード)
  │
  ▼
BlockDevice
  ├─ InternalFlash
  ├─ SPI Flash (W25Qxx 等)
  ├─ FRAM / EEPROM
  └─ SD (SPI / SDIO)
```

---

## 実行モデル

FS syscall は**非同期（ノンブロッキング）**。要求投入と結果取得を分離する:

```
ControlTask:  fs::open("/flash/preset.bin", RDONLY);  // SVC → 要求投入 → 即 return
              │
              ▼
              メインループ継続（UI更新、ノブ読取、LED制御 等）
              │
              ▼
              wait_event(event::audio | event::midi | event::control | event::fs)
              │
              ├── event::audio → process() は AudioTask 側で自動
              ├── event::midi  → MIDI メッセージ処理
              ├── event::control → ノブ/ボタン処理
              └── event::fs    → fs::result() で結果取得（fd / エラー）

StorageService (SystemTask):
              │ wait_block() で要求受信
              ▼
              slimfs::file_open()
              │ erase 発生時は yield → AudioTask に CPU を渡す
              ▼
              完了 → 結果を共有メモリの result slot に書き込み
                   → ControlTask に event::fs を通知
```

### なぜ非同期モデルか

- **UI がフリーズしない**: Flash erase は数十ms。その間もノブ読取・LCD更新・LED制御を継続できる
- **WaitEvent 統合**: audio/midi/control/fs の全イベントを単一ループで処理。電子楽器の制御ループに自然に統合される
- **AudioTask に影響しない**: AudioTask（優先度 0）は完全に独立動作
- **シーケンシャル操作も容易**: 即座に結果が必要な場合は `wait_event(event::fs)` で待てばよい

### 結果の受け渡し

共有メモリ上に FS result slot を設ける:

```cpp
struct FsResult {
    int32_t value;      // fd, バイト数, 位置, 0, エラーコード
    bool ready;         // 結果が準備完了か
};
```

- `fs::open()` 等の syscall は要求を投入して即 return（戻り値 0 = 受付成功, < 0 = キュー満杯等）
- StorageService が処理完了後、result slot に書き込み + `event::fs` 通知
- ControlTask は `fs::result()` で結果を取得
- 要求は逐次処理（キュー深度 1）。前の要求が完了するまで次の要求は `EBUSY` を返す

### アプリ側使用例

```cpp
// プリセット読み込み（シーケンシャル操作）
void load_preset(const char* path) {
    fs::open(path, OpenFlags::RDONLY);
    wait_event(event::fs);
    int fd = fs::result();
    if (fd < 0) { /* エラー処理 */ return; }

    fs::read(fd, buf.data(), buf.size());
    wait_event(event::fs);
    int n = fs::result();

    fs::close(fd);
    wait_event(event::fs);
    fs::result();  // close の結果を消費（次の要求を受付可能にする）
}

// 同期ラッパー（アプリ側ユーティリティ）
inline int fs_sync(auto syscall_fn) {
    syscall_fn();
    wait_event(event::fs);
    return fs::result();
}

// 同期ラッパーを使ったプリセット読み込み
void load_preset_simple(const char* path) {
    int fd = fs_sync([&] { fs::open(path, OpenFlags::RDONLY); });
    if (fd < 0) return;
    int n = fs_sync([&] { fs::read(fd, buf.data(), buf.size()); });
    fs_sync([&] { fs::close(fd); });
}

// メインループ（UI とFS を並行処理）
void main_loop() {
    bool fs_pending = false;
    while (true) {
        uint32_t ev = wait_event(event::control | event::fs);

        if (ev & event::control) {
            handle_knobs();
            handle_buttons();
            update_lcd();
        }
        if (ev & event::fs) {
            int r = fs::result();
            on_fs_complete(r);  // アプリ固有の完了処理
            fs_pending = false;
        }

        // FS 操作が必要かつ前の要求が完了済みなら新しい要求を投入
        if (!fs_pending && need_save()) {
            fs::open("/flash/preset.bin", OpenFlags::WRONLY | OpenFlags::CREAT | OpenFlags::TRUNC);
            fs_pending = true;
        }
    }
}
```

---

## FS 選択

| メディア | FS | 理由 |
|---------|-----|------|
| 内蔵 Flash / SPI Flash | slimfs | 電断耐性（COW）、ウェアレベリング、コンパクト実装 |
| SD カード | FATfs | PC 互換（FAT32） |

パスで自動振り分け:
- `/flash/...` → slimfs
- `/sd/...` → FATfs

---

## エラーコード

FS syscall の戻り値は `int32_t`:
- `>= 0`: 成功（open は fd、read/write はバイト数、tell は位置）
- `< 0`: `-errno`（POSIX 互換）

03-port/06-syscall.md の `SyscallError` はカーネルレベル（不正 syscall 番号等）のエラー。
FS 操作のエラーは StorageService が以下の errno で返す:

| errno | 値 | 意味 |
|-------|----|------|
| `EIO` | -5 | I/O エラー（デバイス故障、CRC不整合） |
| `EBADF` | -9 | 不正な fd / dirfd |
| `ENOMEM` | -12 | バッファ不足 |
| `ENOENT` | -2 | ファイル/ディレクトリが存在しない |
| `EEXIST` | -17 | 既に存在する（CREAT\|EXCL） |
| `ENOTDIR` | -20 | ディレクトリではない |
| `EISDIR` | -21 | ディレクトリに対するファイル操作 |
| `EINVAL` | -22 | 不正な引数 |
| `EFBIG` | -27 | ファイルサイズ上限超過 |
| `ENOSPC` | -28 | ディスク空き不足 |
| `ENAMETOOLONG` | -36 | ファイル名が長すぎる |
| `ENOSYS` | -38 | 未サポート操作（FATfs への attr 操作等） |
| `ENOTEMPTY` | -39 | ディレクトリが空でない |
| `ENODEV` | -19 | デバイス未挿入（SD カード未挿入） |
| `ECORRUPT` | -84 | ファイルシステム破損 |

slimfs の `SlimError` は上記と値が一致する設計。FATfs の `FatResult` は StorageService が変換する。

---

## マウント管理

アプリ側に Mount/Unmount syscall は公開しない。StorageService が内部で管理する。

### `/flash/` — 静的マウント

```
電源ON → StorageService 起動 → slimfs.mount() → /flash/ 有効化
shutdown → slimfs.unmount()
```

常時マウント。アプリの介入不要。

### `/sd/` — ホットスワップ対応

```
SD挿入検出（GPIO割り込み）
  → StorageService に通知
  → fat.mount() → /sd/ 有効化

SD抜去検出
  → /sd/ 上の全 fd/dirfd を強制クローズ
    （次の read/write は EIO を返す）
  → fat.unmount() → /sd/ 無効化
  → /sd/ へのアクセスは ENODEV を返す
```

アプリは `/sd/` へのアクセス時に `ENODEV` をチェックするだけでよい。

---

## Syscall インターフェース

[03-port/06-syscall.md](03-port/03-port/06-syscall.md) §グループ 6 (60–89):

### ファイル操作 (60–68) — fd ベース

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 60 | `FileOpen` | `fs::open(path, flags)` | `path: const char*, flags: uint32_t` | 0=受付 / エラー | fd / エラー | 新設計 |
| 61 | `FileRead` | `fs::read(fd, buf, len)` | `fd: int, buf: uint8_t*, len: uint32_t` | 0=受付 / エラー | 読み取りバイト数 | 新設計 |
| 62 | `FileWrite` | `fs::write(fd, buf, len)` | `fd: int, buf: const uint8_t*, len: uint32_t` | 0=受付 / エラー | 書き込みバイト数 | 新設計 |
| 63 | `FileClose` | `fs::close(fd)` | `fd: int` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 64 | `FileSeek` | `fs::seek(fd, off, whence)` | `fd: int, offset: int32_t, whence: uint8_t` | 0=受付 / エラー | 新位置 | 新設計 |
| 65 | `FileTell` | `fs::tell(fd)` | `fd: int` | 0=受付 / エラー | 現在位置 | 新設計 |
| 66 | `FileSize` | `fs::size(fd)` | `fd: int` | 0=受付 / エラー | ファイルサイズ | 新設計 |
| 67 | `FileTruncate` | `fs::truncate(fd, size)` | `fd: int, size: uint32_t` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 68 | `FileSync` | `fs::sync(fd)` | `fd: int` | 0=受付 / エラー | 0 / エラー | 新設計 |

> `FileSync` は fd 単位の fsync。FS 全体の sync は StorageService が shutdown 時に実行する。

### ディレクトリ操作 (70–74)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 70 | `DirOpen` | `fs::dir_open(path)` | `path: const char*` | 0=受付 / エラー | dirfd / エラー | 新設計 |
| 71 | `DirRead` | `fs::dir_read(dirfd, info)` | `dirfd: int, info: FsInfo*` | 0=受付 / エラー | 1=あり / 0=EOF / エラー | 新設計 |
| 72 | `DirClose` | `fs::dir_close(dirfd)` | `dirfd: int` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 73 | `DirSeek` | `fs::dir_seek(dirfd, off)` | `dirfd: int, off: uint32_t` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 74 | `DirTell` | `fs::dir_tell(dirfd)` | `dirfd: int` | 0=受付 / エラー | 位置 | 新設計 |

### パス操作 (75–79)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 75 | `Stat` | `fs::stat(path, info)` | `path: const char*, info: FsInfo*` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 76 | `Fstat` | `fs::fstat(fd, info)` | `fd: int, info: FsInfo*` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 77 | `Mkdir` | `fs::mkdir(path)` | `path: const char*` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 78 | `Remove` | `fs::remove(path)` | `path: const char*` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 79 | `Rename` | `fs::rename(old, new)` | `old: const char*, new: const char*` | 0=受付 / エラー | 0 / エラー | 新設計 |

> `Remove` はファイル・ディレクトリ共通。非空ディレクトリは `ENOTEMPTY` で失敗。

### カスタム属性 (80–82)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 80 | `GetAttr` | `fs::getattr(path, type, buf, len)` | `path: const char*, type: uint8_t, buf: uint8_t*, len: uint32_t` | 0=受付 / エラー | 属性サイズ / エラー | 新設計 |
| 81 | `SetAttr` | `fs::setattr(path, type, buf, len)` | `path: const char*, type: uint8_t, buf: const uint8_t*, len: uint32_t` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 82 | `RemoveAttr` | `fs::removeattr(path, type)` | `path: const char*, type: uint8_t` | 0=受付 / エラー | 0 / エラー | 新設計 |

> FATfs は属性非対応。`/sd/...` パスへの attr 操作は `ENOSYS` (-38) を返す。

### FS 情報 (83)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 83 | `FsStat` | `fs::fs_stat(path, fsinfo)` | `path: const char*, fsinfo: FsStatInfo*` | 0=受付 / エラー | 0 / エラー | 新設計 |

> `path` でマウントポイントを指定（`"/flash"` or `"/sd"`）。

### FS 結果取得 (84)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 状態 |
|----|------|-----------|------|-----------------|------|
| 84 | `FsResult` | `fs::result()` | — | result slot の値（fd, バイト数, 位置, 0, エラー） | 新設計 |

> `event::fs` 受信後に呼び出す。result slot をクリアし、次の FS 要求を受付可能にする。
> `event::fs` を待たずに呼び出した場合、結果未到着なら `EAGAIN` (-11) を返す。

### 85–89: 予約

> `fs_gc` と `fs_grow` はアプリ側 syscall としては公開しない。
> StorageService 内部で必要に応じて呼び出す（GC はアイドル時自動、grow は FW アップデート時）。

---

## 統一情報構造体

slimfs と FATfs の差異を吸収する共通の情報構造体:

```cpp
namespace umi::fs {

enum class FsType : uint8_t {
    REG = 1,
    DIR = 2,
};

struct FsInfo {
    FsType type;
    uint32_t size;
    char name[64];  // 最大63文字 + NUL。組込み用途で十分
};

struct FsStatInfo {
    uint32_t block_size;    // バイト単位
    uint32_t block_count;   // 総ブロック数
    uint32_t blocks_used;   // 使用中ブロック数
};

} // namespace umi::fs
```

`SlimInfo` / `FatFileInfo` から StorageService が変換して返す。
`FsStatInfo` は `fs_stat("/flash", &fsinfo)` で取得。空き容量は `block_count - blocks_used` で計算。

> `FsInfo` / `FsStatInfo` はアプリの共有メモリ上に配置する。
> syscall の `info` 引数はアプリ側のポインタで、StorageService が MPU 境界を越えて書き込む。
> `name[64]` は 72B の構造体サイズとなり、スタック上に置いても問題ない。

---

## オープンフラグ

```cpp
namespace umi::fs {

enum class OpenFlags : uint32_t {
    RDONLY = 1,
    WRONLY = 2,
    RDWR   = 3,
    CREAT  = 0x0100,
    EXCL   = 0x0200,
    TRUNC  = 0x0400,
    APPEND = 0x0800,
};

} // namespace umi::fs
```

slimfs の `SlimOpenFlags` と値が一致する設計。FATfs 側は StorageService が変換する。

---

## BlockDevice インターフェース

```cpp
struct BlockDevice {
    int read(uint32_t block, uint32_t offset, void* buf, uint32_t size);
    int write(uint32_t block, uint32_t offset, const void* buf, uint32_t size);
    int erase(uint32_t block);
    uint32_t block_size();   // バイト単位
    uint32_t block_count();
};
```

### erase の非同期処理

Flash erase は数十ミリ秒かかる。erase 中に CPU を他タスクに譲る:

```
StorageService:
  block_device.erase(block)
    ├─ erase 開始（DMA/ハードウェア）
    ├─ wait_block(EraseComplete)  → CPU を ControlTask に譲る
    └─ 完了通知で復帰 → 次の操作に進む
```

---

## 電断安全性

### slimfs

slimfs は Copy-on-Write（COW）メカニズムで電断耐性を実現する:
- 既存データを上書きせず、新しいブロックに書き込む
- メタデータの更新はアトミック（1ブロック書き込み完了で切り替え）
- スーパーブロック二重書き込みで FS メタデータを保護
- 追加のトランザクションログは不要

### FATfs

FAT32 は電断に脆弱。SD カードの場合:
- 書き込み中の電断でファイル破損の可能性
- 重要データは slimfs 側に保存することを推奨

---

## ファイルディスクリプタ

- アプリごとに最大 4 個のファイルディスクリプタを同時オープン可能
- fd はカーネル内部のテーブルインデックス（0-3）
- dirfd は fd とは別のテーブル（最大 2 個）
- アプリ Fault / Exit 時にオープン中の fd / dirfd は自動クローズ（`close_all`）

---

## カスタム属性の用途

電子楽器でのメタデータ管理に使用する:

| type | 用途 | サイズ |
|------|------|--------|
| 1 | カテゴリタグ（"Bass", "Lead" 等） | ~16 B |
| 2 | お気に入りフラグ | 1 B |
| 3 | 作成元（ファクトリ/ユーザー） | 1 B |
| 4 | カラーインデックス | 1 B |

属性 type の割り当てはアプリケーション定義。FS 層は type を不透明な uint8_t として扱う。

---

## StorageService 内部動作

### GC（メタデータコンパクション）

StorageService はアイドル時（要求キューが空、かつ ControlTask が待機中）に
`fs_gc()` を呼び出し、削除済みエントリを除去する。

### fs_grow

FW アップデートでパーティションレイアウトが変更された場合、
ブート時に StorageService が `fs_grow()` を呼び出してブロック数を拡張する。

---

## 関連ドキュメント

- [03-port/06-syscall.md](03-port/03-port/06-syscall.md) — Syscall 番号体系
- [13-system-services.md](13-system-services.md) — SystemTask でのディスパッチ
