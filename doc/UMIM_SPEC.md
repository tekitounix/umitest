# UMI-Module フォーマット仕様書

**バージョン:** 1.0.0-draft  
**拡張子:** `.umim`  
**ステータス:** ドラフト

## 概要

UMI-Module (`.umim`) は、WebAssemblyベースのオーディオプラグインフォーマットです。

**対象プラットフォーム:**
- ブラウザ (Web Audio API)
- Node.js
- ネイティブアプリケーション (WAMR, Wasmer等)
- 組み込みシステム (Wasm3, WAMR)

**特徴:**
- **クロスプラットフォーム** - 単一バイナリがどこでも動作
- **サンドボックス実行** - メモリ安全、隔離された実行環境
- **シンプルなAPI** - 約15関数のみ
- **ゼロ依存** - 純粋なWASM、JSグルーコード不要
- **1ソースマルチターゲット** - 同一コードから複数ターゲットをビルド

## アーキテクチャ

UMIMは「アプリケーション層」と「アダプタ層」の分離により、
1つのソースコードから複数のターゲットをビルドできます。

```
┌─────────────────────────────────────────────────────────────┐
│              Application Code (Pure C++/Rust/Zig)           │
│         オーディオ処理ロジック、パラメータ定義              │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
┌───────────────┐     ┌───────────────┐     ┌───────────────┐
│  UPF Adapter  │     │   Embedded    │     │  VST3/CLAP    │
│   (WASM)      │     │    Adapter    │     │   Adapter     │
└───────────────┘     └───────────────┘     └───────────────┘
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐     ┌───────────────┐     ┌───────────────┐
│    .umim      │     │   Firmware    │     │  .vst3/.clap  │
│  Browser,     │     │  STM32, ESP32 │     │     DAW       │
│  Node.js      │     │  Bare-metal   │     │               │
└───────────────┘     └───────────────┘     └───────────────┘
```

### 対応言語

UMIMはWASMにコンパイルできる任意の言語で開発可能:

| 言語 | ツールチェイン | 備考 |
|------|---------------|------|
| C/C++ | Emscripten | 推奨、最も成熟 |
| Rust | wasm32-unknown-unknown | no_std対応可 |
| Zig | wasm32-wasi | 組み込み向き |
| AssemblyScript | 直接WASM出力 | TypeScript風構文 |
| Go | TinyGo | GCに注意 |

### MVC構造

UMIMはMVC (Model-View-Controller) パターンに基づいています:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Application                                      │
├─────────────────────────────────┬───────────────────────────────────────┤
│      Model + Controller         │              View                     │
│           (WASM)                │   ┌───────────────┬─────────────────┐ │
│  ─────────────────────────────  │   │   WebView     │   Embedded      │ │
│  • オーディオ処理 (DSP)          │   │  (HTML/CSS/JS)│   (C++ Class)   │ │
│  • パラメータ状態                │   │  100%共通      │  アプリ固有     │ │
│  • UI状態                       │   └───────────────┴─────────────────┘ │
│           100%共通               │                                       │
└─────────────────────────────────┴───────────────────────────────────────┘
```

| レイヤー | 責務 | 共通化 |
|----------|------|--------|
| **Model** | オーディオ処理、パラメータ状態 | ✓ 100% (WASM) |
| **Controller** | 入力→状態変更 | ✓ 100% (WASM) |
| **View (WebView)** | UI描画 (HTML/CSS/JS) | ✓ 100% |
| **View (Embedded)** | UI描画 (C++) | アプリ固有 |

## 設計目標

| 目標 | 説明 |
|------|------|
| 移植性 | WASMが動く環境ならどこでも動作 |
| 安全性 | メモリ安全、隔離実行 |
| 性能 | ネイティブに近い速度、ゼロコピーオーディオ |
| 簡潔さ | 最小限のAPI、複雑な状態機械なし |
| 拡張性 | 機能フラグによるオプション機能 |

## 既存フォーマットとの比較

| 項目 | VST3 | AU | CLAP | LV2 | **UMIM** |
|------|------|----|----- |-----|----------|
| ABI | C++ | ObjC | C | C | WASM |
| クロスプラットフォーム | ○ | macOSのみ | ○ | ○ | ◎ |
| ブラウザ対応 | ✗ | ✗ | ✗ | ✗ | ✓ |
| サンドボックス | ✗ | ✗ | ✗ | ✗ | ✓ |
| バイナリサイズ | 大 | 大 | 中 | 中 | 小 |
| API複雑度 | 高 | 高 | 中 | 中 | 低 |

## ファイル形式

### 拡張子

```
myplugin.umim
```

### 構造

`.umim` ファイルは標準的なWebAssemblyバイナリ (`.wasm`) であり、以下を含みます:

1. **必須エクスポート** - コアプラグイン関数
2. **オプションエクスポート** - 拡張機能
3. **メモリ** - オーディオバッファ用の共有リニアメモリ

`.wasm` ではなく `.umim` 拡張子を使用する理由:
- UMI-Module準拠を明示
- OSのファイル関連付けを可能に
- 汎用WASMモジュールと区別

### MIMEタイプ

```
application/x-umi-module
```

## コアAPI

### ライフサイクル

```c
// プラグインインスタンスを作成
// 処理開始前に一度だけ呼ばれる
void upf_create(float sample_rate);

// プラグインインスタンスを破棄
// プラグインアンロード時に呼ばれる
void upf_destroy(void);
```

### オーディオ処理

```c
// オーディオを処理
// input: 入力バッファへのポインタ（シンセの場合はNULL可）
// output: 出力バッファへのポインタ
// frames: 処理するサンプル数
void upf_process(const float* input, float* output, uint32_t frames);
```

入力がNULLの場合、プラグインはシンセ/ジェネレータモードとして動作します。
ホストは入力がないことを明示的に伝えるためにNULLを渡します。

### MIDI / ノートイベント

```c
// ノートオンイベント
void upf_note_on(uint8_t note, uint8_t velocity);

// ノートオフイベント
void upf_note_off(uint8_t note);
```

### パラメータ

```c
// パラメータ総数を取得
uint32_t upf_get_param_count(void);

// パラメータ値を設定（表示単位）
void upf_set_param(uint32_t index, float value);

// パラメータ値を取得（表示単位）
float upf_get_param(uint32_t index);

// パラメータメタデータ
const char* upf_get_param_name(uint32_t index);    // パラメータ名
float upf_get_param_min(uint32_t index);           // 最小値
float upf_get_param_max(uint32_t index);           // 最大値
float upf_get_param_default(uint32_t index);       // デフォルト値
uint8_t upf_get_param_curve(uint32_t index);       // カーブ種別
const char* upf_get_param_unit(uint32_t index);    // 単位（Hz, dB等）
```

### MIDI CC

```c
// MIDI Control Changeを処理
void upf_process_cc(uint8_t channel, uint8_t cc, uint8_t value);

// 動的MIDIラーン
void upf_midi_learn(uint8_t cc, uint32_t param_id);
void upf_midi_unlearn(uint8_t cc);
```

### メモリ管理

```c
// 内部オーディオバッファへのポインタを取得
// ホストはこれを使ってゼロコピー処理が可能
float* upf_get_buffer_ptr(void);
```

## オプション拡張

拡張機能はエクスポートの存在確認により検出します。

### プラグイン情報（推奨）

```c
// プラグインメタデータ
const char* upf_get_name(void);      // プラグイン名
const char* upf_get_vendor(void);    // ベンダー名
const char* upf_get_version(void);   // バージョン文字列
uint32_t upf_get_type(void);         // プラグイン種別
```

### マルチチャンネル対応

```c
uint32_t upf_get_input_count(void);   // 入力チャンネル数
uint32_t upf_get_output_count(void);  // 出力チャンネル数
void upf_process_multi(const float** inputs, float** outputs, 
                       uint32_t channels, uint32_t frames);
```

### 状態の永続化

```c
uint32_t upf_get_state_size(void);                        // 状態サイズ
void upf_get_state(uint8_t* buffer, uint32_t size);       // 状態を取得
void upf_set_state(const uint8_t* buffer, uint32_t size); // 状態を復元
```

### レイテンシ報告

```c
uint32_t upf_get_latency(void);  // サンプル単位
```

## パラメータカーブ

| 値 | カーブ | 計算式 |
|----|--------|--------|
| 0 | リニア | `min + normalized * (max - min)` |
| 1 | 対数 | `min * pow(max/min, normalized)` |
| 2 | 指数 | `pow(normalized, 2) * (max - min) + min` |
| 3 | 平方根 | `sqrt(normalized) * (max - min) + min` |

## プラグイン種別

| 値 | 種別 | 説明 |
|----|------|------|
| 0 | エフェクト | オーディオ入力 → オーディオ出力 |
| 1 | インストゥルメント | MIDI入力 → オーディオ出力 |
| 2 | アナライザ | オーディオ入力 → データ出力 |
| 3 | MIDIエフェクト | MIDI入力 → MIDI出力 |

## メモリレイアウト

```
WASM リニアメモリ
├── 0x0000 - 0x0FFF: 予約領域（スタック、グローバル）
├── 0x1000 - 0x?????: プラグインヒープ
└── オーディオバッファ（mallocで確保）
    ├── 入力バッファ
    └── 出力バッファ
```

### バッファ管理

1. **ホスト確保**: ホストが `malloc()` でオーディオバッファを確保
2. **プラグイン内部**: プラグインが `upf_get_buffer_ptr()` でゼロコピー用バッファを提供

推奨バッファサイズ: 128〜512サンプル

## ホスト実装

### JavaScript (Web Audio)

```javascript
// プラグインをロード
const response = await fetch('synth.umim');
const wasmBytes = await response.arrayBuffer();
const module = await WebAssembly.instantiate(wasmBytes, {
    env: { /* 必要に応じてメモリインポート */ }
});
const plugin = module.instance.exports;

// 初期化
plugin.upf_create(audioContext.sampleRate);

// AudioWorkletProcessor内で
process(inputs, outputs, parameters) {
    const inputPtr = /* 入力を確保してコピー */;
    const outputPtr = /* 出力バッファを確保 */;
    
    plugin.upf_process(inputPtr, outputPtr, 128);
    
    // 出力をオーディオバッファにコピー
    return true;
}
```

### ネイティブ (C/C++)

```cpp
#include <wasm_runtime.h>

// .umimファイルをロード
auto module = wasm_runtime_load("synth.umim");
auto instance = wasm_runtime_instantiate(module);

// エクスポートを取得
auto upf_create = wasm_runtime_lookup_function(instance, "upf_create");
auto upf_process = wasm_runtime_lookup_function(instance, "upf_process");

// 初期化
wasm_runtime_call(upf_create, sample_rate);

// オーディオ処理
wasm_runtime_call(upf_process, input_ptr, output_ptr, frames);
```

### 組み込み (WAMR/Wasm3)

```cpp
#include <wasm3.h>

// マイクロコントローラ向け最小WASMランタイム
M3Environment* env = m3_NewEnvironment();
M3Runtime* runtime = m3_NewRuntime(env, 8192, nullptr);

// モジュールをロード
m3_ParseModule(env, &module, wasm_bytes, wasm_size);
m3_LoadModule(runtime, module);

// リンクして実行
m3_FindFunction(&upf_process, runtime, "upf_process");
m3_Call(upf_process, input, output, frames);
```

## UMIMプラグインのビルド

### UMI-OS SDKを使用

```cpp
// myplugin.cc
#include "adapter/upf_adapter.hh"
#include "myplugin_app.hh"

UPF_PLUGIN(MyPluginApp)
```

### ビルドコマンド

```bash
# xmakeを使用
xmake build myplugin

# 出力: .build/wasm/myplugin.js + myplugin.wasm
# 配布用に .wasm を .umim にリネーム
mv .build/wasm/myplugin.wasm myplugin.umim
```

### Emscripten直接

```bash
emcc myplugin.cc -o myplugin.wasm \
    -sWASM=1 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS="['_upf_create','_upf_process',...]" \
    -fno-exceptions -fno-rtti -O3

mv myplugin.wasm myplugin.umim
```

## プラグイン要件

### 必須エクスポート

有効な `.umim` は最低限以下をエクスポートする必要があります:

| エクスポート | 必須 |
|-------------|------|
| `upf_create` | ✓ |
| `upf_process` | ✓ |
| `upf_get_param_count` | ✓ |
| `upf_set_param` | ✓ |
| `upf_get_param` | ✓ |
| `malloc` | ✓ |
| `free` | ✓ |

### バリデーション

```javascript
function validateUMIM(exports) {
    const required = [
        'upf_create', 'upf_process',
        'upf_get_param_count',
        'upf_set_param', 'upf_get_param',
        'malloc', 'free'
    ];
    return required.every(fn => fn in exports);
}
```

## ベストプラクティス

### パフォーマンス

1. **process()内でメモリ確保しない** - 全バッファを事前確保
2. **SIMDを活用** - WASM SIMDでベクトル化DSP
3. **分岐を最小化** - 予測可能なコードパス
4. **パラメータ更新をバッチ処理** - 毎サンプル更新しない

### 互換性

1. **浮動小数点の非正規化数を避ける** - ゼロにフラッシュ
2. **NULL入力を処理** - 参照前にチェック
3. **パラメータインデックスを検証** - 境界チェック
4. **スレッドセーフ** - processは任意のスレッドから呼ばれる可能性

### セキュリティ

1. **ファイルシステムアクセスなし** - WASMサンドボックス
2. **ネットワークアクセスなし** - 純粋な計算のみ
3. **メモリ制限** - `ALLOW_MEMORY_GROWTH` を制限
4. **全入力を検証** - ホストデータを信頼しない

## バージョン履歴

| バージョン | ステータス | 変更内容 |
|-----------|-----------|----------|
| 1.0.0 | ドラフト | 初期仕様 |

## 参考資料

- [WebAssembly仕様](https://webassembly.github.io/spec/)
- [CLAPオーディオプラグインフォーマット](https://cleveraudio.org/)
- [LV2プラグイン標準](https://lv2plug.in/)
- [Web Audio API](https://www.w3.org/TR/webaudio/)

## ライセンス

この仕様書は CC0 1.0 Universal (パブリックドメイン) でリリースされています。

---

**UMI-OS プロジェクト**  
https://github.com/user/umi_os
