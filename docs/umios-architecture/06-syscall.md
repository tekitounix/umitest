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
 20–29:  構成・パラメータ（set_app_config, set_route_table 等）
 30–39:  MIDI / イベント（midi_send, midi_recv, read_sysex 等）
 40–49:  情報取得（get_shared, get_param 等）
 50–59:  I/O（log, set_led 等）
 60–69:  ファイルシステム（将来）
 70–255: 予約
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
| 20 | `SetAppConfig` | `umi::set_app_config(cfg)` | `config: const AppConfig*` | 0 / エラー | 新設計 |
| 21 | `SetRouteTable` | `umi::set_route_table(rt)` | `table: const RouteTable*` | 0 / エラー | 新設計 |
| 22 | `SetParamMapping` | `umi::set_param_mapping(pm)` | `mapping: const ParamMapping*` | 0 / エラー | 新設計 |
| 23 | `SetInputMapping` | `umi::set_input_mapping(im)` | `mapping: const InputParamMapping*` | 0 / エラー | 新設計 |
| 24 | `ConfigureInput` | `umi::configure_input(cfg)` | `input_id: uint8_t, cfg: const InputConfig*` | 0 / エラー | 新設計 |
| 25 | `SendParamRequest` | `umi::send_param_request(req)` | `req: const ParamSetRequest*` | 0 / エラー | 新設計 |

### グループ 3: MIDI / SysEx (30–39)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 30 | `MidiSend` | `umi::midi_send(data, len, dest)` | `data: const uint8_t*, len: uint16_t, dest: uint8_t` | 0 / エラー | 将来 |
| 31 | `MidiRecv` | `umi::midi_recv(buf, maxlen, &src)` | `buf: uint8_t*, maxlen: uint16_t, src: uint8_t*` | 受信バイト数 | 将来 |
| 32 | `ReadSysex` | `umi::read_sysex(buf, len, &src)` | `buf: uint8_t*, len: uint16_t, src: uint8_t*` | 受信バイト数 | 将来 |
| 33 | `SendSysex` | `umi::send_sysex(data, len, dest)` | `data: const uint8_t*, len: uint16_t, dest: uint8_t` | 0 / エラー | 将来 |

### グループ 4: 情報取得 (40–49)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 40 | `GetShared` | `umi::get_shared()` | — | `SharedMemory*` | 実装済み |

### グループ 5: I/O (50–59)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 50 | `Log` | `umi::log(msg)` | `msg: const char*, len: uint16_t` | 0 / エラー | 実装済み |
| 51 | `Panic` | `umi::panic(msg)` | `msg: const char*` | — (停止) | 実装済み |

### グループ 6: ファイルシステム (60–69)

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 60 | `FileOpen` | — | `path, flags` | fd / エラー | 将来 |
| 61 | `FileRead` | — | `fd, buf, len` | 読み取りバイト数 | 将来 |
| 62 | `FileWrite` | — | `fd, buf, len` | 書き込みバイト数 | 将来 |
| 63 | `FileClose` | — | `fd` | 0 / エラー | 将来 |
| 64 | `FileSeek` | — | `fd, offset, whence` | 新位置 | 将来 |
| 65 | `FileStat` | — | `path, stat_buf` | 0 / エラー | 将来 |
| 66 | `DirOpen` | — | `path` | dirfd / エラー | 将来 |
| 67 | `DirRead` | — | `dirfd, entry_buf` | 0 / エラー | 将来 |
| 68 | `DirClose` | — | `dirfd` | 0 / エラー | 将来 |

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

constexpr uint32_t Audio    = (1 << 0);   // オーディオバッファ準備完了
constexpr uint32_t Midi     = (1 << 1);   // MIDI データ利用可能
constexpr uint32_t VSync    = (1 << 2);   // ディスプレイリフレッシュ
constexpr uint32_t Timer    = (1 << 3);   // タイマーティック
constexpr uint32_t Control  = (1 << 4);   // ControlEvent 到着
constexpr uint32_t Shutdown = (1 << 31);  // シャットダウン要求

} // namespace umi::syscall::event
```

> **旧ドキュメントとの差異**:
> - `Button` (1 << 4) → `Control` に変更。ハードウェア入力は ControlEvent に統合されたため

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
