# 09 — アプリバイナリ仕様

## 形式一覧

| 形式 | 拡張子 | ターゲット | 内容 |
|------|--------|-----------|------|
| UMI App | .umia | 組み込み (Cortex-M) | AppHeader + フラットバイナリ |
| UMI Module | .umim | WASM | WebAssembly バイナリ |
| VST3 | .vst3 | デスクトップ | VST3 プラグインバンドル |
| AU | .component | macOS | Audio Unit プラグインバンドル |
| CLAP | .clap | デスクトップ | CLAP プラグインバンドル |

## .umia 形式（組み込み）

### AppHeader

128 バイト、4 バイトアラインメント。

```cpp
namespace umi {

inline constexpr uint32_t APP_MAGIC = 0x414D4955;  // メモリ上 [55 49 4D 41] = "UIMA"
inline constexpr uint32_t APP_ABI_VERSION = 1;

enum class AppTarget : uint32_t {
    USER        = 0,    // 一般ユーザー向け
    DEVELOPMENT = 1,    // 開発用（署名不要）
    RELEASE     = 2,    // リリース用（署名必須）
};

struct alignas(4) AppHeader {
    // --- 識別 (16B) ---
    uint32_t magic;             // APP_MAGIC (0x414D4955)
    uint32_t abi_version;       // APP_ABI_VERSION
    AppTarget target;           // ターゲット種別
    uint32_t flags;             // 予約 (0)

    // --- エントリポイント (8B) ---
    uint32_t entry_offset;      // ヘッダ先頭からの _start() オフセット
    uint32_t process_offset;    // Loader が設定（RegisterProc 後）

    // --- セクションサイズ (16B) ---
    uint32_t text_size;         // .text
    uint32_t rodata_size;       // .rodata
    uint32_t data_size;         // .data
    uint32_t bss_size;          // .bss

    // --- メモリ要件 (8B) ---
    uint32_t stack_size;        // 必要スタックサイズ
    uint32_t heap_size;         // 必要ヒープサイズ

    // --- 完全性検証 (8B) ---
    uint32_t crc32;             // セクション CRC32
    uint32_t total_size;        // イメージ総サイズ

    // --- 署名 (64B) ---
    uint8_t signature[64];      // Ed25519 署名

    // --- 予約 (8B) ---
    uint8_t reserved[8];        // 予約 (0)
};  // 合計 128B

} // namespace umi
```

> **旧ドキュメントとの差異**:
> - magic 値は `0x414D4955` に確定（メモリ上 `[55 49 4D 41]`）。`0x554D4941` は使用しない
> - 構造体サイズは 128B で確定

### バイナリレイアウト

```
Offset 0x000  ┌─────────────────┐
              │  AppHeader      │  128B
              │  (magic, CRC等) │
Offset 0x080  ├─────────────────┤
              │  .text          │  コード
              │                 │
              ├─────────────────┤
              │  .rodata        │  定数データ
              │                 │
              ├─────────────────┤
              │  .data          │  初期化済みデータ
              │                 │  (RAM にコピーされる)
              └─────────────────┘
```

`.bss` はバイナリに含まれない（サイズのみ AppHeader に記録。ローダがゼロ初期化する）。

### 検証フロー

ローダは以下の順に検証する:

1. `magic == APP_MAGIC` (0x414D4955)
2. `abi_version == APP_ABI_VERSION`
3. ターゲット互換性チェック（Development カーネルは全種別を許可、Release カーネルは Release のみ）
4. サイズ整合性（`total_size == 128 + text_size + rodata_size + data_size`）
5. エントリポイント範囲チェック（`entry_offset` がテキストセクション内）
6. CRC32 検証（.text + .rodata + .data の連結に対する CRC32、多項式 0xEDB88320）
7. Release 時のみ: Ed25519 署名検証（ヘッダ全体に対する署名）

```cpp
enum class LoadResult : int32_t {
    OK                  = 0,
    INVALID_MAGIC       = -1,
    INVALID_ABI         = -2,
    INVALID_TARGET      = -3,
    INVALID_SIZE        = -4,
    INVALID_ENTRY       = -5,
    CRC_MISMATCH        = -6,
    SIGNATURE_INVALID   = -7,
    MEMORY_ERROR        = -8,
};
```

### ローダの動作

1. Flash 上の AppHeader を読み取り・検証（magic, ABI, CRC, 署名）
2. .text の配置確認（XIP: Flash 上で直接実行）
3. エントリポイント (`_start()`) のアドレス計算
4. スタック・ヒープ領域を設定
5. MPU を設定（App Data / App Stack の保護）
6. PSP（プロセススタックポインタ）を設定
7. 非特権モードで `_start()` にジャンプ

> **注**: `.data`/`.bss` の初期化はローダでは行わない。`_start()` の責任である。

### _start() エントリポイント

```cpp
// crt0.cc (アプリ側)
extern "C" void _start() {
    // 1. .data コピー (Flash → RAM、リンカシンボル _sidata/_sdata/_edata 使用)
    // 2. .bss ゼロ初期化 (リンカシンボル _sbss/_ebss 使用)
    // 3. C++ グローバルコンストラクタ呼び出し (__init_array)
    // 4. main() 呼び出し
    main();
}
```

### ビルドツール

`lib/umi/tools/build/make_umia.py` で ELF から .umia を生成する。

```bash
python lib/umi/tools/build/make_umia.py \
    --input build/synth_app.elf \
    --output build/synth_app.umia \
    --target development \
    --stack-size 16384 \
    --heap-size 32768
```

リリースビルドでは `--sign` オプションで Ed25519 署名を付与する。

## .umim 形式（WASM）

### 概要

.umim は標準の WebAssembly バイナリ (.wasm) に UMI 固有のエクスポート関数を含めたもの。
AudioWorklet 上で動作する。

### 必須エクスポート

```cpp
extern "C" {
    // 初期化・破棄
    void umi_init(uint32_t sample_rate, uint32_t buffer_size);
    void umi_destroy();

    // オーディオ処理
    void umi_process(float* input_l, float* input_r,
                     float* output_l, float* output_r,
                     uint32_t frames);

    // イベント
    void umi_push_event(uint32_t type, uint32_t data);

    // パラメータ
    float umi_get_param(uint32_t index);
    void umi_set_param(uint32_t index, float value);
}
```

### ビルド

```bash
# Emscripten でビルド
xmake build headless_webhost
xmake webhost  # .wasm を web/ にコピー
```

## Plugin 形式

### VST3 / AU / CLAP

Plugin バックエンドでは、アプリケーションコードが Plugin ラッパーにリンクされて各フォーマットのバイナリになる。

```
Application Code (Processor + Controller)
         │
         ├── VST3 Adapter → .vst3 バンドル
         ├── AU Adapter   → .component バンドル
         └── CLAP Adapter → .clap バンドル
```

各アダプタは [08-backend-adapters.md](08-backend-adapters.md) で定義された変換を行う。

### Plugin メタデータ

Plugin フォーマットでは AppHeader の代わりに各フォーマット固有のメタデータを使用する:

| 項目 | VST3 | AU | CLAP |
|------|------|----|------|
| ID | FUID (128bit) | AudioComponentDescription | clap_plugin_descriptor |
| 名前 | PFactoryInfo | kAudioUnitProperty_Name | name |
| カテゴリ | subCategories | componentType | features |
| パラメータ | IEditController | AUParameter | clap_param_info |

## ABI 互換性

### .umia の ABI 契約

1. `APP_ABI_VERSION` が一致するアプリは、同一 ABI のカーネルで動作する
2. ABI バージョンが異なるアプリはロードを拒否する
3. Syscall の番号と引数の型は ABI バージョンに紐づく
4. SharedMemory のレイアウトは ABI バージョンに紐づく

### ABI 変更ルール

- **互換変更**（ABI バージョン不変）: 新 syscall の追加、予約フィールドの利用
- **非互換変更**（ABI バージョン更新）: 既存 syscall の変更、SharedMemory レイアウト変更、AppHeader 構造変更
