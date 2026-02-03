# 06 — Syscall 仕様

## 原則

1. **Syscall は最小限** — 共有メモリで済むものは syscall にしない
2. **Syscall 選定基準** — 以下のいずれかを満たす場合のみ:
   - 特権ハードウェアアクセスが必要（MPU 境界を越える）
   - スケジューラ状態の変更が必要（アトミック性が必須）
   - ブートストラップ問題（SharedMemory ポインタ取得等）
3. **番号体系はスパース配置** — グループごとに 10 刻みで余裕を持たせる
4. **バックエンド非依存** — アプリ側 API は全ターゲット共通。内部実装のみ異なる

## ステータス定義

各 syscall の「状態」欄は以下の意味を持つ:

| ステータス | 意味 | コードへの影響 |
|-----------|------|--------------|
| 実装済み | カーネルに実装が存在し、アプリから呼び出し可能 | 使用可 |
| 新設計 | 本ドキュメントで仕様を確定。実装は未着手または進行中 | 使用不可（コンパイルは通るがスタブ） |
| 将来 | 仕様は方向性のみ。詳細は未確定 | 使用不可（定義なし） |

## 番号体系

```
  0– 9:  プロセス制御（exit, yield, register_proc 等）
 10–19:  時間・スケジューリング（wait_event, get_time, sleep 等）
 20–29:  構成・パラメータ（set_app_config, send_param_request）
 30–39:  MIDI / イベント（midi_send, midi_recv, read_sysex 等）
 40–49:  情報取得（get_shared, get_param 等）
 50–59:  I/O（log, set_led 等）
 60–89:  ファイルシステム
 90–255: 予約
```

> **旧ドキュメントとの差異**:
> - ARCHITECTURE.md: `RegisterProc=1, WaitEvent=2, GetTime=10, Log=20, GetShared=40`
> - syscall_numbers.hh: 連番 `0-6`、FS `32-47`
> - 本仕様で統一。旧番号は移行する

## Syscall 一覧

### グループ 0: プロセス制御 (0–9)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 0 | `Exit` | `umi::exit(code)` | `code: int32_t` | — (終了) | 実装済み |
| 1 | `Yield` | (暗黙的) | — | 0 | 実装済み |
| 2 | `RegisterProc` | `umi::register_processor(proc)` | `instance: uint32_t, fn: uint32_t, event_capacity: uint32_t` | 0 / エラー | 実装済み |
| 3 | `UnregisterProc` | — | `instance: uint32_t` | 0 / エラー | 将来 |

> **RegisterProc の event_capacity パラメータ**: アプリが希望する input_events / output_events のバッファ容量。OS の上限を超える場合は clamp される。0 を指定するとデフォルト容量が使われる。

### グループ 1: 時間・スケジューリング (10–19)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 10 | `WaitEvent` | `umi::wait_event(mask, timeout)` | `mask: uint32_t, timeout_us: uint32_t` | 発生イベントビット | 実装済み |
| 11 | `GetTime` | `umi::get_time()` | — | `uint64_t`（μs） | 実装済み |
| 12 | `Sleep` | `umi::sleep(duration)` | `usec: uint32_t` | 0 | 実装済み |

### グループ 2: 構成・パラメータ (20–29)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 20 | `SetAppConfig` | `umi::set_app_config(cfg)` | `config: const AppConfig*` | 0 / エラー | 実装済み |
| 21–24 | _(reserved)_ | — | — | — | `SetAppConfig` に統合済み |
| 25 | `SendParamRequest` | `umi::send_param_request(req)` | `req: const ParamSetRequest*` | 0 / エラー | 将来 |

### グループ 3: MIDI / SysEx (30–39)

MIDI syscall は **非同期（ノンブロッキング）** で動作する。送受信要求と結果取得を分離し、`event::midi` で完了を通知する。

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 30 | `MidiSend` | `midi::send(data, len, dest)` | `data: const uint8_t*, len: uint16_t, dest: uint8_t` | 0=受付 / エラー | 送信バイト数 / エラー | 新設計 |
| 31 | `MidiRecv` | `midi::recv(buf, maxlen, &src)` | `buf: uint8_t*, maxlen: uint16_t, src: uint8_t*` | 0=受付 / エラー | 受信バイト数 / エラー | 新設計 |
| 32 | `ReadSysex` | `midi::read_sysex(buf, len, &src)` | `buf: uint8_t*, len: uint16_t, src: uint8_t*` | 0=受付 / エラー | 受信バイト数 / エラー | 新設計 |
| 33 | `SendSysex` | `midi::send_sysex(data, len, dest)` | `data: const uint8_t*, len: uint16_t, dest: uint8_t` | 0=受付 / エラー | 送信バイト数 / エラー | 新設計 |
| 34 | `MidiResult` | `midi::result()` | — | result slot の値 | — | 新設計 |

> `MidiResult` は `event::midi` 受信後に呼び出す。result slot をクリアし、次の MIDI 要求を受付可能にする。
> `event::midi` を待たずに呼び出した場合、結果未到着なら `EAGAIN` (-11) を返す。

#### 非同期フロー

```cpp
// SysEx 送信（非同期）
midi::send_sysex(data, len, MIDI_DEST_USB);
wait_event(event::midi);
int sent = midi::result();  // 送信完了、送信バイト数取得

// MIDI 受信（非同期）
midi::recv(buf, sizeof(buf), &src);
wait_event(event::midi);
int n = midi::result();  // 受信完了、受信バイト数取得
```

#### dest / src の値

| 値 | 意味 |
|----|------|
| 0 | USB MIDI |
| 1 | UART MIDI |
| 2–7 | 予約 |

### グループ 4: 情報取得 (40–49)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 40 | `GetShared` | `umi::get_shared()` | — | `SharedMemory*` | 実装済み |

### グループ 5: I/O (50–59)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 50 | `Log` | `umi::log(msg)` | `msg: const char*, len: uint16_t` | 0 / エラー | 実装済み |
| 51 | `Panic` | `umi::panic(msg)` | `msg: const char*` | — (停止) | 実装済み |

#### Log の動作仕様

`Log` syscall は **同期的だが非ブロッキング**で動作する。

- 出力は SysEx stdio バッファ（リングバッファ）にコピーされ、即座に return する
- USB 送信は SystemTask が非同期で実行する
- **バッファ満杯時の動作**: メッセージをドロップし、`ENOSPC` (-28) を返す
- ドロップはメトリクス（`KernelMetrics.log_drops`）でカウントされる

リアルタイムタスク（process() 内）から呼び出しても安全だが、高頻度の呼び出しはバッファ溢れを引き起こすため推奨しない。デバッグ用途に限定すること。

```cpp
// 使用例（デバッグ用）
if (umi::log("tick") == -ENOSPC) {
    // バッファ満杯、メッセージはドロップされた
}
```

### グループ 6: ファイルシステム (60–89)

詳細は [19-storage-service.md](../04-services/19-storage-service.md) を参照。

#### ファイル操作 (60–68) — fd ベース

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 60 | `FileOpen` | `fs::open(path, flags)` | `path, flags` | 0=受付 / エラー | fd / エラー | 新設計 |
| 61 | `FileRead` | `fs::read(fd, buf, len)` | `fd, buf, len` | 0=受付 / エラー | 読み取りバイト数 | 新設計 |
| 62 | `FileWrite` | `fs::write(fd, buf, len)` | `fd, buf, len` | 0=受付 / エラー | 書き込みバイト数 | 新設計 |
| 63 | `FileClose` | `fs::close(fd)` | `fd` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 64 | `FileSeek` | `fs::seek(fd, off, whence)` | `fd, offset, whence` | 0=受付 / エラー | 新位置 | 新設計 |
| 65 | `FileTell` | `fs::tell(fd)` | `fd` | 0=受付 / エラー | 現在位置 | 新設計 |
| 66 | `FileSize` | `fs::size(fd)` | `fd` | 0=受付 / エラー | ファイルサイズ | 新設計 |
| 67 | `FileTruncate` | `fs::truncate(fd, size)` | `fd, size` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 68 | `FileSync` | `fs::sync(fd)` | `fd` | 0=受付 / エラー | 0 / エラー | 新設計 |

> `FileSync` は fd 単位の fsync。FS 全体の sync は StorageService が shutdown 時に実行する。

#### ディレクトリ操作 (70–74)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 70 | `DirOpen` | `fs::dir_open(path)` | `path` | 0=受付 / エラー | dirfd / エラー | 新設計 |
| 71 | `DirRead` | `fs::dir_read(dirfd, info)` | `dirfd, info` | 0=受付 / エラー | 1=あり / 0=EOF / エラー | 新設計 |
| 72 | `DirClose` | `fs::dir_close(dirfd)` | `dirfd` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 73 | `DirSeek` | `fs::dir_seek(dirfd, off)` | `dirfd, off` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 74 | `DirTell` | `fs::dir_tell(dirfd)` | `dirfd` | 0=受付 / エラー | 位置 | 新設計 |

#### パス操作 (75–79)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 75 | `Stat` | `fs::stat(path, info)` | `path, info` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 76 | `Fstat` | `fs::fstat(fd, info)` | `fd, info` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 77 | `Mkdir` | `fs::mkdir(path)` | `path` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 78 | `Remove` | `fs::remove(path)` | `path` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 79 | `Rename` | `fs::rename(old, new)` | `old, new` | 0=受付 / エラー | 0 / エラー | 新設計 |

> `Remove` はファイル・ディレクトリ共通。非空ディレクトリは `ENOTEMPTY` で失敗。

#### カスタム属性 (80–82)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 80 | `GetAttr` | `fs::getattr(path, type, buf, len)` | `path, type, buf, len` | 0=受付 / エラー | 属性サイズ / エラー | 新設計 |
| 81 | `SetAttr` | `fs::setattr(path, type, buf, len)` | `path, type, buf, len` | 0=受付 / エラー | 0 / エラー | 新設計 |
| 82 | `RemoveAttr` | `fs::removeattr(path, type)` | `path, type` | 0=受付 / エラー | 0 / エラー | 新設計 |

> FATfs は属性非対応。`/sd/...` パスへの attr 操作は `ENOSYS` (-38) を返す。

#### FS 情報 (83)

| Nr | 名前 | アプリ API | 引数 | 戻り値 (syscall) | 結果 (result slot) | 状態 |
|----|------|-----------|------|-----------------|-------------------|------|
| 83 | `FsStat` | `fs::fs_stat(path, fsinfo)` | `path, fsinfo` | 0=受付 / エラー | 0 / エラー | 新設計 |

> `FsStat` は `FsStatInfo` 構造体を返す（block_size, block_count, blocks_used）。
> `path` でマウントポイントを指定（`"/flash"` or `"/sd"`）。

#### FS 結果取得 (84)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 84 | `FsResult` | `fs::result()` | — | result slot の値 | 新設計 |

> `event::fs` 受信後に呼び出す。result slot をクリアし次の要求を受付可能にする。

#### 85–89: 予約

## エラーコード

```cpp
namespace umi::syscall {

enum class SyscallError : int32_t {
    OK              = 0,
    INVALID_SYSCALL = -1,
    INVALID_PARAM   = -2,
    ACCESS_DENIED   = -3,
    NOT_FOUND       = -4,
    TIMEOUT         = -5,
    BUSY            = -6,
};

} // namespace umi::syscall
```

## イベントフラグ

`WaitEvent` の `mask` 引数および戻り値で使用するビットフラグ。

```cpp
namespace umi::syscall::event {

constexpr uint32_t audio    = (1 << 0);   // オーディオバッファ準備完了
constexpr uint32_t midi_in  = (1 << 1);   // MIDI 入力データ到着（従来の midi）
constexpr uint32_t vsync    = (1 << 2);   // ディスプレイリフレッシュ
constexpr uint32_t timer    = (1 << 3);   // タイマーティック
constexpr uint32_t control  = (1 << 4);   // ControlEvent 到着
constexpr uint32_t fs       = (1 << 5);   // FS 操作完了
constexpr uint32_t midi     = (1 << 6);   // MIDI syscall 完了（送受信）
constexpr uint32_t shutdown = (1u << 31); // シャットダウン要求

} // namespace umi::syscall::event
```

> **イベントの使い分け**:
> - `midi_in`: EventRouter 経由で MIDI 入力が到着した（process() の input_events に格納済み）
> - `midi`: MIDI syscall（`midi::send()`, `midi::recv()` 等）の操作が完了した

> **旧ドキュメントとの差異**:
> - `Button` (1 << 4) → `Control` に変更。ハードウェア入力は ControlEvent に統合されたため
> - `midi` (1 << 1) → `midi_in` にリネーム、`midi` (1 << 6) を MIDI syscall 完了用に追加

## Syscall 詳細

### Exit (Nr=0)

アプリケーションを終了する。カーネルは Processor を解除し、アプリをアンロードする。

```cpp
void umi::exit(int code);
```

### Yield (Nr=1)

CPU 制御を自発的にスケジューラに返す。`WaitEvent` のタイムアウト待機中に暗黙的に呼ばれる。

### RegisterProc (Nr=2)

Processor を Audio Task に登録する。`instance` は Processor 構造体へのポインタ、`fn` は `process()` 関数へのポインタ。

```cpp
int umi::register_processor(P& processor);
```

内部では型消去を行い、`(instance, fn)` のペアとして syscall に渡す。

### WaitEvent (Nr=10)

イベントを待機する。`mask` で指定したビットのいずれかが立つまでブロックする。

```cpp
uint32_t umi::wait_event(uint32_t mask = 0xFFFFFFFF, uint32_t timeout_us = 0);
```

- `mask = 0`: 全イベント待機
- `timeout_us = 0`: 無期限待機
- 戻り値: 発生したイベントのビットマスク

### GetTime (Nr=11)

64-bit モノトニック時刻（マイクロ秒）を返す。

```cpp
uint64_t umi::get_time();
```

カーネル側は r0 に下位 32-bit、r1 に上位 32-bit を格納する。
32-bit タイマを 1μs 刻みで動作させ、オーバーフロー割り込み（約 71.6 分ごと）で上位ビットを加算する。

### SetAppConfig (Nr=20)

AppConfig を一括適用する。RouteTable + ParamMapping + InputParamMapping + InputConfig を一括で非活性バッファに書き込み、次のブロック境界で切り替える。

```cpp
int umi::set_app_config(const AppConfig& config);
```

詳細は [04-param-system.md](04-param-system.md) を参照。

### GetShared (Nr=40)

SharedMemory 先頭アドレスを返す。ブートストラップ時に1回だけ呼ぶ。

```cpp
void* umi::get_shared();
```

## 呼び出し規約（組み込み）

ARM Cortex-M の SVC 例外を使用する。

```
引数:  r0 = arg0, r1 = arg1, r2 = arg2, r3 = arg3
番号:  r12 = syscall_nr
呼出:  SVC #0
戻り:  r0 = 戻り値 (int32_t)
       r1 = 追加戻り値 (GetTime の上位32bit 等)
```

```cpp
inline int32_t syscall_call(uint8_t nr, uint32_t a0 = 0, uint32_t a1 = 0,
                             uint32_t a2 = 0, uint32_t a3 = 0) {
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    register uint32_t r12 __asm__("r12") = nr;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r12)
                     : "memory");
    return static_cast<int32_t>(r0);
}
```

### SVC ハンドラフロー

```
SVC #0
  │
  ▼
SVC_Handler (naked)
  ├── EXC_RETURN bit[2] でスタック判定 (MSP/PSP)
  ├── r12 から syscall_nr 取得
  └── svc_dispatch(ExceptionFrame*, nr) 呼び出し
        ├── Nr 0–3:  プロセス制御
        ├── Nr 10–12: スケジューリング
        ├── Nr 20–25: 構成変更
        ├── Nr 40:   情報取得
        ├── Nr 50–51: I/O
        ├── Nr 60–83: ファイルシステム → StorageService キューに投入（即 return）
        ├── Nr 84:    FsResult → result slot から値を取得・クリア
        └── default: INVALID_SYSCALL
```

## バックエンド別実装

| バックエンド | Syscall 実装方式 | 備考 |
|-------------|-----------------|------|
| 組み込み (Cortex-M) | SVC 例外 → カーネルハンドラ | 特権遷移あり、MPU 保護 |
| WASM | `__attribute__((import_module("umi")))` | ホスト JS が関数を提供 |
| Plugin (VST3/AU/CLAP) | 直接関数呼び出し | 同一プロセス、特権不要 |

### WASM バックエンド

```cpp
// WASM import として宣言
extern "C" {
    __attribute__((import_module("umi"), import_name("exit")))
    void umi_exit(int32_t code);

    __attribute__((import_module("umi"), import_name("wait_event")))
    uint32_t umi_wait_event(uint32_t mask, uint32_t timeout_us);

    __attribute__((import_module("umi"), import_name("get_time")))
    uint64_t umi_get_time();

    __attribute__((import_module("umi"), import_name("set_app_config")))
    int32_t umi_set_app_config(const void* config);

    // ...
}
```

ホスト JS 側が `WebAssembly.instantiate()` の `importObject` でこれらを提供する。

### Plugin バックエンド

```cpp
// Plugin アダプタが直接実装を提供
namespace umi::backend {

struct PluginRuntime {
    void exit(int32_t code);
    uint32_t wait_event(uint32_t mask, uint32_t timeout_us);
    uint64_t get_time();
    int32_t set_app_config(const AppConfig* config);
    // ...
};

} // namespace umi::backend
```

`umi::set_app_config()` 等のアプリ API は、コンパイル時にバックエンドを切り替えて適切な実装に解決される。

## 番号の割り当てポリシー

1. 各グループ内は連番で割り当てる
2. グループ間は 10 刻みで区切る（拡張余地）
3. 新しい syscall の追加時はグループ内の次の番号を使う
4. 既存の番号は変更しない（ABI 互換性）
5. 廃止された syscall は番号を予約として保持し再利用しない

## 関連ドキュメント

- [11-scheduler.md](11-scheduler.md) — WaitEvent / Yield の動作（notify / wait_block）
- [13-system-services.md](../04-services/13-system-services.md) — FS syscall の非同期設計、サービスアーキテクチャ
- [04-param-system.md](04-param-system.md) — SetAppConfig の詳細
