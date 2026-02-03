# 16 — App Loader

## 概要

Flash 上の .umia アプリバイナリを検証・ロードし、ControlTask で実行可能にするサービス。
AppHeader の完全な定義は [09-app-binary.md](09-app-binary.md) を参照。

| 項目 | 状態 |
|------|------|
| ヘッダ検証（magic, ABI, CRC32） | 実装済み |
| Ed25519 署名検証 | 実装済み |
| メモリレイアウト設定 | 実装済み |
| Processor 登録（syscall） | 実装済み |
| アプリ状態管理 | 実装済み |
| アプリ Fault 後の復旧 | 実装済み |

---

## アーキテクチャ

```
Flash (.umia image)
  │
  ▼
AppLoader::load()
  ├─ AppHeader 検証
  ├─ CRC32 検証
  ├─ Ed25519 署名検証 (Release のみ)
  ├─ メモリレイアウト設定
  └─ エントリポイント取得
        │
        ▼
main() → MPU 設定 → start_rtos()
        │
        ▼
ControlTask → _start() → main()
        │
        ▼
RegisterProc syscall → AudioTask で process() 呼び出し
```

---

## AppHeader

AppHeader は 128 バイト固定長。Flash 上の固定アドレスに配置される。

```cpp
struct AppHeader {
    uint32_t magic;            // 0x414D4955 ("UMIA")
    uint16_t abi_version;      // ABI バージョン
    uint16_t app_version;      // アプリバージョン
    AppTarget target;          // User / Development / Release
    // ...
    uint32_t entry_offset;     // _start() オフセット
    uint32_t text_size;        // .text セクションサイズ
    uint32_t rodata_size;      // .rodata セクションサイズ
    uint32_t data_size;        // .data セクションサイズ
    uint32_t bss_size;         // .bss セクションサイズ
    uint32_t stack_size;       // 要求スタックサイズ
    uint32_t heap_size;        // 要求ヒープサイズ
    uint32_t total_size;       // バイナリ全体サイズ
    uint32_t crc32;            // CRC32 (text + rodata + data)
    uint8_t  signature[64];    // Ed25519 署名
};
```

詳細は [09-app-binary.md](09-app-binary.md) を参照。

---

## ロードフロー

### 1. ヘッダ検証

```
AppHeader 読み取り（Flash 上の固定アドレス）
  ├─ magic == 0x414D4955 ?      → InvalidMagic
  ├─ abi_version 互換性チェック  → InvalidVersion
  ├─ セクションサイズ整合性      → InvalidSize
  └─ target チェック             → TargetMismatch
```

### 2. CRC32 検証

ヘッダの `crc32` フィールドと、実際のペイロード（text + rodata + data）の CRC32 を比較する。

```
CRC32(flash[text_start .. text_start + text_size + rodata_size + data_size])
  == header.crc32 ?  → CrcMismatch
```

### 3. Ed25519 署名検証

Release ビルド（`target == AppTarget::Release`）の場合のみ:

```
Ed25519::verify(
    public_key:  カーネル内蔵の公開鍵,
    message:     AppHeader (署名フィールド除く) + ペイロード,
    signature:   header.signature
) → SignatureInvalid / SignatureRequired
```

署名検証の暗号プリミティブについては [14-security.md](14-security.md) を参照。

### 4. メモリレイアウト設定

```cpp
AppRuntime runtime;
runtime.base       = app_ram_start;
runtime.text_start = flash_image_start + header.entry_offset;
runtime.data_start = app_ram_start;
runtime.stack_base = app_stack_start;
runtime.stack_top  = app_stack_start + header.stack_size;
```

### 5. エントリポイント取得

```cpp
// Thumb ビット（bit 0）付きのエントリポイント
void* entry = reinterpret_cast<void*>(
    reinterpret_cast<uintptr_t>(runtime.text_start) | 0x1
);
```

---

## LoadResult

```cpp
enum class LoadResult {
    Ok,
    InvalidMagic,
    InvalidVersion,
    InvalidSize,
    CrcMismatch,
    SignatureInvalid,
    SignatureRequired,
    TargetMismatch,
    OutOfMemory,
    AlreadyLoaded,
};
```

---

## アプリ状態管理

### AppState

```cpp
enum class AppState {
    None,        // アプリ未ロード
    Loaded,      // ロード完了、未起動
    Running,     // 実行中
    Suspended,   // 一時停止
    Terminated,  // 終了済み
};
```

### 状態遷移

```
None ──load()──→ Loaded ──start()──→ Running
                                        │
                                    suspend()
                                        │
                                        ▼
                                    Suspended ──resume()──→ Running
                                        │
Running ──────terminate()───────→ Terminated ──unload()──→ None
         ──────fault()──────────→ Terminated
```

---

## Processor 登録

アプリは `main()` で Processor を OS に登録する:

```cpp
// アプリ側
int main() {
    MySynth synth;
    umi::register_processor(synth);  // RegisterProc syscall (Nr=2)
    // ...
}
```

内部では型消去を行い、`(instance, process_fn)` のペアとしてカーネルに渡す:

```cpp
// カーネル側 (AppLoader)
int register_processor(void* instance, ProcessFn fn) {
    runtime.processor = instance;
    runtime.process_fn = fn;
    return 0;
}
```

登録後、AudioTask は毎フレーム `call_process(ctx)` で Processor を呼び出す。

---

## アプリ起動フロー

### crt0 (_start)

ControlTask がアプリの `_start()` を非特権モードで呼び出す。`_start()` は以下を行う:

```cpp
extern "C" void _start() {
    // 1. .data セクションを Flash → RAM にコピー
    // 2. .bss セクションをゼロクリア
    // 3. C++ グローバルコンストラクタ呼び出し
    // 4. main() 呼び出し
    // 5. main() 復帰後 → umi::exit(0)
}
```

> `.data/.bss` 初期化はカーネルではなくアプリの `_start()` が行う。
> これによりカーネルはアプリのセクション詳細を知る必要がなくなる。

---

## Fault 時の復旧

アプリが Fault を起こした場合:

1. Fault ISR が `record_fault()` で例外情報を記録（カーネル RAM）
2. SystemTask の `process_pending_fault()` が起動
3. AppLoader が `terminate()` でアプリ状態をリセット
4. AudioTask の Processor 呼び出しを無効化
5. Shell を有効化し、デバッグ可能な状態にする
6. LED をエラーパターンに変更

OS（Shell、USB、LED）はアプリ Fault 後も動作を継続する。
Fault ログの詳細は [20-diagnostics.md](../04-services/20-diagnostics.md) を参照。

---

## 実装ファイル

| ファイル | 内容 |
|---------|------|
| `lib/umios/kernel/loader.hh` | AppLoader クラス、AppRuntime |
| `lib/umios/kernel/loader.cc` | ロード・検証ロジック |
| `lib/umios/kernel/app_header.hh` | AppHeader 構造体定義 |

---

## 関連ドキュメント

- [09-app-binary.md](09-app-binary.md) — AppHeader 完全定義、.umia バイナリ形式
- [12-memory-protection.md](12-memory-protection.md) — MPU リージョン設定
- [14-security.md](14-security.md) — Ed25519 署名検証
- [15-boot-sequence.md](15-boot-sequence.md) — ロードのタイミング（Phase 2）
- [20-diagnostics.md](../04-services/20-diagnostics.md) — Fault ログ
