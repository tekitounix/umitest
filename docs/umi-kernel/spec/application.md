# UMI アプリケーション規格仕様

**規範レベル:** MUST/SHALL/REQUIRED, SHOULD/RECOMMENDED, MAY/NOTE/EXAMPLE
**対象読者:** App Dev / Kernel Dev
**適用範囲:** .umia アプリケーション規格

---

## 1. 目的・スコープ
本書は UMI アプリケーション（.umia）の形式と実行契約を定義する。

---

## 2. .umia 形式
### 2.1 ヘッダ（128 bytes, alignas(4)）
`AppHeader` は [lib/umios/kernel/app_header.hh](lib/umios/kernel/app_header.hh) と同一でなければならない。

| フィールド | 型 | 目的 |
|---|---|---|
| `magic` | `uint32_t` | `0x414D4955`（"UMIA"）固定 |
| `abi_version` | `uint32_t` | ABI バージョン（現行: `1`） |
| `target` | `AppTarget` | `User` / `Development` / `Release` |
| `flags` | `uint32_t` | 予約（0固定） |
| `entry_offset` | `uint32_t` | `_start` のオフセット（header 先頭基準） |
| `process_offset` | `uint32_t` | `process()` のオフセット（現在未使用） |
| `text_size` | `uint32_t` | `.text` サイズ |
| `rodata_size` | `uint32_t` | `.rodata` サイズ |
| `data_size` | `uint32_t` | `.data` サイズ |
| `bss_size` | `uint32_t` | `.bss` サイズ |
| `stack_size` | `uint32_t` | Control Task 用スタック要求量 |
| `heap_size` | `uint32_t` | ヒープ要求量（0 可） |
| `crc32` | `uint32_t` | `.text + .rodata + .data` の CRC32 |
| `total_size` | `uint32_t` | `sizeof(AppHeader) + sections_size` |
| `signature[64]` | `uint8_t[64]` | Ed25519 署名（Release 向け） |
| `reserved[8]` | `uint8_t[8]` | 予約（0固定） |

`AppTarget` の意味:
- `User` : 署名不要。Development/Release の両カーネルで実行可能。
- `Development` : Development カーネルのみ。
- `Release` : Release カーネルのみ。署名検証を必須。

### 2.2 検証（loader 実装準拠）
Kernel は以下を検証し、失敗した場合はロードを拒否する。

1. `magic` が `APP_MAGIC` に一致。
2. `abi_version` が `APP_ABI_VERSION` に一致。
3. `target` がカーネルの `BuildType` と互換。
4. `total_size == sizeof(AppHeader) + (text_size + rodata_size + data_size)` かつ `image_size >= total_size`。
5. `entry_offset` が `[sizeof(AppHeader), sizeof(AppHeader) + text_size)` に収まる。
6. `crc32` は **ヘッダ直後の sections（text+rodata+data）** に対する CRC32 と一致。
7. `target == Release` の場合は Ed25519 署名検証に合格。

署名の対象は **ヘッダのみ** であり、署名検証時は `signature` 領域を 0 埋めして検証する。
セクションの改ざん検出は `crc32` が担う。

### 2.3 リロケーション（仕様決定・未実装）
`.umia` は **リロケータブル ELF** を起点とし、書き込み先アドレスに合わせて
リロケーションを適用した **flat binary** を生成してから Flash へ書き込む。

```
.umia (relocatable ELF)
  → リロケーション（.rel.dyn などを処理）
  → AppHeader + sections の flat binary
  → Flash 書き込み → XIP 実行
```

Kernel のローダは **flat binary 形式のみ** を扱い、ELF 解析/リロケーションは
ホストツール（`umiflash`）またはデバイス側アップデータが担当する。
このリロケーション方式は **仕様として決定済み**だが、現行実装は未対応。

---

## 3. エントリポイント契約
エントリポイントは `lib/umios/app/crt0.cc` の `_start()` を前提とする。

1. `.data` の初期化（flash → RAM）。
2. `.bss` のゼロ初期化。
3. グローバルコンストラクタの実行。
4. `main()` を呼び出す。

`main()` の戻り値は **そのままカーネルへ戻る**（`exit` syscall を呼ばない）。
標準 C ランタイムの `_exit(int)` は `Exit` syscall に接続される。

---

## 4. Processor / AudioContext 契約
- `process(AudioContext&)` は AudioTask から呼び出される。
- `process()` は以下を **してはならない**:
  - ヒープ割り当て
  - ブロッキング同期
  - 例外送出
  - stdio 出力
- `process()` は **サンプル精度**のイベント処理を行う。

### 4.1 C++ ランタイム制約
- 例外（throw/catch）は **非対応**。
- RTTI（`dynamic_cast` / `typeid`）は **非対応**。

これらはアプリ規格として **使用禁止** とする。
理由: バイナリサイズ増加、起動/実行時オーバーヘッド、実装/デバッグ複雑化が大きく、
リアルタイム性とトレードオフが釣り合わないため。

**例: process() 実装例（AudioContext API 準拠）**
```cpp
void process(AudioContext& ctx) {
  for (const auto& ev : ctx.input_events) {
    if (ev.type == EventType::Midi) {
      // handle MIDI
    }
  }

  auto* out_l = ctx.output(0);
  auto* out_r = ctx.output(1);
  if (!out_l || !out_r) {
    return;
  }

  for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
    float s = render_sample();
    out_l[i] = s;
    out_r[i] = s;
  }
}
```

---

## 5. OS-アプリ間インタフェース

アプリが OS とやり取りする手段は **SharedMemory** と **syscall** の2種類のみ。

### 5.1 syscall（ABI 準拠）
syscall の番号と呼び出し規約は [lib/umios/app/syscall.hh](lib/umios/app/syscall.hh) に一致しなければならない。
呼び出しは `r0=nr, r1..r4=arg`、戻り値は `r0`。

**実装済み（stm32f4_kernel）**

| nr | 名前 | 引数 | 戻り値 | 挙動 |
|---:|---|---|---|---|
| 0 | `exit` | `code` | 0 | アプリ終了（loader terminate） |
| 1 | `yield` | なし | 0 | コンテキストスイッチ要求 |
| 2 | `wait_event` | `mask`, `timeout_us` | `uint32_t` | 発生したイベントビットマスクを返す |
| 3 | `get_time` | なし | `uint64_t` | 64-bit モノトニック時刻（μs） |
| 4 | `get_shared` | なし | `void*` | `SharedMemory*` を返す |
| 5 | `register_proc` | `processor`, `fn` | 0 / -1 | Processor 登録（`fn==0` の場合は simple 登録） |

**予約/将来（ABI は定義済みだがカーネル未実装）**
`unregister_proc (6)`, `peek_event (7)`, `send_event (8)`, `sleep_usec (9)`, `log (10)`, `get_param (11)`, `set_param (12)`。

**注:** `get_time` は RTC ベースの 64-bit 時刻を返すこと。

### 5.2 SharedMemory
SharedMemory のレイアウトは **ハードウェア入出力やインターフェイス構成に依存**する。
そのため、共通メタ情報と HW 依存レイアウトを分離して定義する。

#### 5.2.1 共通メタ情報
SharedMemory の先頭には、**ABI/制限値/機能フラグ**を置くことを仕様として決定する。
このヘッダは **未実装**であり、現行実装では配置されていない。

```
struct SharedMemoryHeader {
  uint32_t abi_version;    // (major << 16) | minor
  uint32_t shared_size;    // SharedMemory 全体サイズ
  uint32_t app_ram_size;   // アプリ使用可能 RAM
  uint32_t max_processors; // 登録可能 Processor 数
  uint32_t feature_flags;  // bit0: FS, bit1: USB MIDI, bit2: PDM Mic, bit3-31: reserved
};
```

実装時は **SharedMemory 先頭**に配置し、`GetShared` で一括取得できること。

#### 5.2.2 HW 依存レイアウト
HW 依存部分はターゲットごとに定義する。
現行実装の例は [lib/umios/kernel/loader.hh](lib/umios/kernel/loader.hh) の `SharedMemory` を参照する。

**主要フィールド（例）**
- `audio_input[]` / `audio_output[]` / `mic_input[]`
- `sample_rate`, `buffer_size`, `dt`, `sample_position`
- `event_queue[]`, `event_write_idx`, `event_read_idx`
- `params[32]`
- `flags`, `led_state`, `button_pressed`, `button_current`
- `heap_base`, `heap_size`

EventQueue は SPSC として設計され、
Kernel が `push_event()`、アプリが `pop_event()` を行う（現状は syscalls 未接続）。

---

## 6. ABI/互換性
`abi_version` の不一致は非互換とする（`APP_ABI_VERSION` と一致必須）。
