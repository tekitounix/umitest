# セキュリティリスク分析と対策指針

## 概要

本ドキュメントは、組込み電子楽器（USB MIDI シンセサイザー）における
セキュリティリスクを分析し、**実用的な対策レベル**を提示する。

### ビルドターゲット

| ターゲット | 対象 | ユーザーができること | 用途 |
|------------|------|---------------------|------|
| **配布版** | エンドユーザー | アプリ作成・ロード | 製品出荷 |
| **開発版** | 開発者 | アプリ + ドライバ作成 | 開発・評価ボード |

**両方でユーザーはカスタムアプリを作成・ロードできる。**

### 権限モデル（CPUモード）

| 層 | CPUモード | 書く人 |
|----|-----------|--------|
| **カーネル** | 特権 | メーカー |
| **ドライバ** | 特権 | メーカー / 開発者（開発版のみ） |
| **アプリケーション** | **非特権** | メーカー / 開発者 / **一般ユーザー** |

### 設計目標

1. **ユーザーアプリの安全な実行**: カスタムアプリがシステムを壊さない（非特権で保護）
2. **ハードウェア抽象化**: ユーザーはハードウェアを知らなくてもアプリが書ける
3. **製品コピー防止**: 開発版カーネルで製品アプリは動かない（互換性制御）
4. **API共通化**: 開発時と配布時でアプリコードを変える必要がない

### 基本方針

> **アプリは常に非特権モード。カーネル/ドライバは特権モードで保護。**

---

## 1. 脅威モデル

### 1.1 脅威の発生源

| 発生源 | 説明 | 対策必要度 |
|--------|------|------------|
| **外部入力** | WebMIDI、USB経由の不正データ | ★★★ 必須 |
| **ユーザーアプリ** | バグまたは悪意のあるユーザーコード | ★★★ 必須 |
| **ユーザードライバ** | 開発ボード向け、ハードウェアアクセス権あり | ★★ 推奨 |
| **物理攻撃** | デバッガ接続、チップ解析 | ★ 製品のみ |

### 1.2 想定される攻撃シナリオ

| シナリオ | 優先度 | 攻撃経路 | 影響 |
|----------|--------|----------|------|
| ユーザーアプリのバグ/暴走 | ★★★ | ロードされたアプリ | システムクラッシュ |
| WebMIDI経由の不正入力 | ★★★ | SysEx、CC | クラッシュ、意図しない動作 |
| 悪意あるアプリの配布 | ★★★ | アプリストア等 | 永続的な乗っ取り |
| 改ざんファームウェアの書込み | ★★ | DFU/JTAG | 永続的な乗っ取り |
| ドライバによるハードウェア破壊 | ★★ | 不正なレジスタ操作 | ハードウェア損傷 |
| デバッグポート経由の読出し | ★ | SWD | ファームウェア流出 |

---

## 2. カーネル/アプリ互換性設計

### 2.1 アプリの種類

| 種類 | 作成者 | 用途 | 署名 |
|------|--------|------|------|
| **製品アプリ** | メーカー | プリインストール / 公式配布 | 配布用鍵で署名 |
| **ユーザーアプリ** | 一般ユーザー | 自作アプリ | 署名なし or 自己署名 |
| **開発アプリ** | 開発者 | 開発・デバッグ用 | 署名なし |

**全てのアプリは非特権モード（ユーザーモード）で実行される。**

### 2.2 互換性マトリクス

```
                    ┌───────────────────────────────────────────────┐
                    │              アプリケーション                  │
                    ├─────────────┬─────────────┬───────────────────┤
                    │  製品アプリ  │ ユーザーアプリ│   開発アプリ     │
┌───────────────────┼─────────────┼─────────────┼───────────────────┤
│ カ │ 配布版      │     ✅      │     ✅      │      ❌          │
│ ー │             │  署名検証   │  署名なしOK  │  ターゲット不一致 │
│ ネ ├─────────────┼─────────────┼─────────────┼───────────────────┤
│ ル │ 開発版      │     ❌      │     ✅      │      ✅          │
│    │             │ ターゲット   │  署名なしOK  │   署名なしOK     │
│    │             │ 不一致      │             │                  │
└────┴─────────────┴─────────────┴─────────────┴───────────────────┘
```

**ポイント**:
- **ユーザーアプリは両方のカーネルで動作**（署名なしでロード可能）
- **製品アプリは配布版カーネルのみ**（製品コピー防止）
- **開発アプリは開発版カーネルのみ**（デバッグ用途）

### 2.3 ユーザーアプリ署名なしのリスク受容

**リスク**: 署名なしユーザーアプリは悪意あるコードを含む可能性がある。

```
配布版カーネル + 悪意あるユーザーアプリ → 動作する（設計上許容）
```

**リスク軽減策**:
| 対策 | 効果 | 実装コスト |
|------|------|-----------|
| MPUによる分離 | カーネル/ドライバ保護 | ✅ 実装済み |
| ウォッチドッグ | 暴走検出・リセット | ✅ 実装済み |
| アプリストアレビュー | 悪意あるアプリ排除 | 運用コスト |
| 権限宣言 + ユーザー確認 | 透明性確保 | 中 |

**設計判断**: 利便性（誰でもアプリを作れる）を優先し、このリスクを受容する。
MPUとウォッチドッグにより、悪意あるアプリがカーネルを破壊することはできない。
最悪のケースはアプリ自身のクラッシュ/リセットであり、永続的な被害は発生しない。

### 2.4 互換性制御の実現方法

**署名鍵の分離**により互換性を制御:

| 項目 | 開発版 | 配布版 |
|------|--------|--------|
| カーネル内の公開鍵 | 開発用公開鍵 | 配布用公開鍵 |
| アプリ署名 | なし or 開発用秘密鍵 | 配布用秘密鍵 |
| 署名検証 | スキップ可 | **必須** |

```cpp
// lib/umios/config.hh

namespace umi::config {

enum class BuildType {
    Development,  // 開発版: 署名なしOK、デバッグ有効
    Release,      // 配布版: 署名必須、デバッグ無効
};

// ビルド時に設定
inline constexpr BuildType BUILD_TYPE = BuildType::Development;

// 公開鍵（ビルドタイプで切り替え）
inline constexpr uint8_t APP_PUBLIC_KEY[32] = 
    (BUILD_TYPE == BuildType::Development) 
        ? DEV_PUBLIC_KEY 
        : RELEASE_PUBLIC_KEY;

} // namespace umi::config
```

### 2.5 アプリヘッダのターゲット識別

```cpp
enum class AppTarget : uint32_t {
    User        = 0,  // ユーザーアプリ（署名なし、両カーネルで動作）
    Development = 1,  // 開発アプリ（開発版カーネルのみ）
    Release     = 2,  // 製品アプリ（配布版カーネルのみ、署名必須）
};

struct AppHeader {
    uint32_t magic;           // 0x554D4941 ("UMIA")
    uint32_t version;
    AppTarget target;         // アプリの種類
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t crc32;
    uint8_t signature[64];    // Ed25519署名（製品アプリは必須）
};

// ローダーでの検証
LoadResult load_app(const uint8_t* image, size_t size) {
    auto* header = reinterpret_cast<const AppHeader*>(image);
    
    // 1. ターゲット互換性チェック
    switch (header->target) {
    case AppTarget::User:
        // ユーザーアプリは両方のカーネルで動作
        break;
    case AppTarget::Development:
        // 開発アプリは開発版カーネルのみ
        if constexpr (config::BUILD_TYPE != BuildType::Development) {
            return LoadResult::TargetMismatch;
        }
        break;
    case AppTarget::Release:
        // 製品アプリは配布版カーネルのみ
        if constexpr (config::BUILD_TYPE != BuildType::Release) {
            return LoadResult::TargetMismatch;
        }
        break;
    default:
        // 不正なターゲット値
        return LoadResult::InvalidHeader;
    }
    
    // 2. 署名検証（製品アプリは必須）
    if (header->target == AppTarget::Release) {
        if (!verify_ed25519(image, size, header->signature, config::RELEASE_PUBLIC_KEY)) {
            return LoadResult::SignatureInvalid;
        }
    }
    
    // ...
}
```

### 2.6 開発者のワークフロー

```
1. 開発フェーズ
   ├── 開発版カーネルをボードに書き込み
   ├── 開発版アプリをビルド（署名なし）
   ├── 自由にロード・デバッグ
   └── ※この段階で吸い出されても問題なし

2. 配布フェーズ  
   ├── 配布版カーネルをビルド（配布用公開鍵埋め込み）
   ├── 配布版アプリをビルド（配布用秘密鍵で署名）
   ├── RDP Level 1 を設定
   └── ※開発版カーネルでは動かない
```

```bash
# 開発版ビルド
xmake config --build-type=dev
xmake build

# 配布版ビルド
xmake config --build-type=release --sign-key=path/to/release.key
xmake build
```

### 2.7 プロ開発者向け運用

プロの開発者がこのプラットフォームで製品を作る場合:

1. **開発時**: 開発版カーネル + 開発版アプリで開発
2. **テスト時**: 配布版カーネル + 配布版アプリで最終確認
3. **出荷時**: 配布版カーネル + 配布版アプリ + RDP設定

**鍵管理**:
- 開発用鍵: 社内共有OK（漏洩しても開発版のみ影響）
- 配布用鍵: 厳重管理（CI/CDサーバのみ保持）

---

## 3. 権限モデル

### 3.1 階層構造

```
┌─────────────────────────────────────────────────────────────────┐
│                        Hardware Layer                           │
│  (CPU, Memory, Peripherals, DMA, Audio Codec, etc.)            │
├─────────────────────────────────────────────────────────────────┤
│                         Kernel                                  │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐               │
│  │ Scheduler   │ │ Memory Mgr  │ │ IRQ Handler │               │
│  └─────────────┘ └─────────────┘ └─────────────┘               │
│  特権モード / MPU全リージョンアクセス可                          │
├─────────────────────────────────────────────────────────────────┤
│                    Driver Layer (Optional)                      │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐               │
│  │ Audio Drv   │ │ USB Driver  │ │ User Driver │               │
│  └─────────────┘ └─────────────┘ └─────────────┘               │
│  特権モード / 許可された周辺機器のみアクセス可                    │
├─────────────────────────────────────────────────────────────────┤
│                    Application Layer                            │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐               │
│  │ Synth App   │ │ Effect App  │ │ User App    │               │
│  └─────────────┘ └─────────────┘ └─────────────┘               │
│  非特権モード / 自タスクメモリ + 共有バッファのみ                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 権限レベル定義

| レベル | 名称 | CPUモード | MPUアクセス | 用途 |
|--------|------|-----------|-------------|------|
| 0 | Kernel | Privileged | 全領域 | カーネル、IRQハンドラ（ユーザー不可） |
| 1 | Driver | Privileged | 制限付き | ユーザードライバ（開発版のみ） |
| 2 | Application | Unprivileged | 自領域+共有のみ | ユーザーアプリケーション |

### 3.3 ビルドタイプ別の権限設定

| ビルドタイプ | ユーザーコード | 許可される権限レベル |
|--------------|---------------|---------------------|
| **配布版** | アプリのみ | Level 2 (Application) |
| **開発版** | アプリ + ドライバ | Level 1-2 (Driver + App) |

**注**: Level 0（カーネル）はいずれの形態でもユーザーに開放しない。

---

## 4. アプリ/ドライバのロード方式

### 4.1 既存UMI仕様との関係

| 仕様 | 役割 | 組込み | Web |
|------|------|--------|-----|
| **UMIP** | DSP処理（Processor） | ネイティブ | WASM |
| **UMIC** | UIロジック（Controller） | ネイティブ | WASM |
| **UMIM** | バイナリ形式 | ELF（独自） | WASM |

**アプリコードは共通**。プラットフォーム差異はSDKの実装で吸収する。

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Code (共通)                  │
│                                                             │
│   int main() {                                              │
│       umi::register_processor(synth);                       │
│       while (umi::wait_event()) { ... }                     │
│   }                                                         │
├───────────────────────┬─────────────────────────────────────┤
│   Embedded (Native)   │           Web (WASM)                │
│   syscall ABI         │           WASM imports              │
│   Kernel + MPU        │           AudioWorklet + Asyncify   │
└───────────────────────┴─────────────────────────────────────┘
```

### 4.2 アプリケーションバイナリ形式

#### タスク構成

アプリケーションは2つの論理タスクで構成される:

```
┌─────────────────────────────────────────────────────────────┐
│                      Kernel (特権)                          │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐           │
│  │ Scheduler   │ │ Audio Task  │ │ USB/MIDI    │           │
│  └─────────────┘ └─────────────┘ └─────────────┘           │
│                        │                                    │
│  ┌─────────────────────┴───────────────────────┐           │
│  │           Shared Memory Region              │           │
│  │  AudioBuffer / EventQueue / ParamBlock      │           │
│  └─────────────────────────────────────────────┘           │
│                        │                                    │
├────────────────────────┼────────────────────────────────────┤
│                  Application (非特権)                       │
│                        │                                    │
│  ┌─────────────────────┴───────────────────┐               │
│  │            Processor Task               │ 最高優先度    │
│  │  process() - カーネルから直接呼び出し     │               │
│  └─────────────────────────────────────────┘               │
│                                                             │
│  ┌─────────────────────────────────────────┐               │
│  │            Control Task (main)          │ 低優先度      │
│  │  イベント処理、UI、パラメータ管理         │               │
│  └─────────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────────┘
```

| タスク | 優先度 | 役割 | syscall |
|--------|--------|------|---------|
| Processor Task | 最高 | DSP処理 | **使わない**（共有メモリ） |
| Control Task | 低 | UI/イベント処理 | 使う（ブロッキング可） |

#### エントリポイント

```cpp
// lib/umi_app/crt0.cc

extern "C" {

// グローバルコンストラクタ/デストラクタ
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

// アプリエントリポイント（カーネルが呼び出す）
void _start() {
    // 1. グローバルコンストラクタ
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }
    
    // 2. main() 実行
    extern int main();
    int ret = main();
    
    // 3. グローバルデストラクタ
    for (auto fn = __fini_array_end; fn > __fini_array_start;) {
        (*--fn)();
    }
    
    // 4. カーネルに終了通知
    umi::syscall::exit(ret);
}

} // extern "C"
```

#### ユーザーが書くコード

```cpp
// my_app/main.cc

#include <umi/app.hh>
#include "my_synth.hh"
#include "my_controller.hh"

int main() {
    // Processor を登録（カーネルのオーディオタスクから呼ばれる）
    static MySynth synth;
    umi::register_processor(synth);
    
    // main() 自身が Control Task として動作
    MyController controller(synth);
    
    while (true) {
        // イベント待ち（syscall、ブロッキング）
        umi::Event ev = umi::wait_event();
        
        if (ev.type == umi::EventType::Shutdown) {
            break;  // 終了要求
        }
        
        switch (ev.type) {
        case umi::EventType::Midi:
            controller.on_midi(ev.midi);
            break;
        case umi::EventType::Encoder:
            controller.on_encoder(ev.encoder);
            break;
        case umi::EventType::Button:
            controller.on_button(ev.button);
            break;
        }
    }
    
    return 0;
}
```

#### Processor（DSP処理）

```cpp
// my_app/my_synth.hh

class MySynth {
public:
    // カーネルのオーディオタスクから呼ばれる（syscall不要）
    void process(umi::ProcessContext& ctx) {
        // 共有メモリ経由でイベント取得
        for (const auto& ev : ctx.events()) {
            if (ev.type == umi::EventType::NoteOn) {
                note_on(ev.note.number, ev.note.velocity);
            }
        }
        
        // オーディオ生成（共有メモリに直接書き込み）
        auto out = ctx.output(0);
        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = generate();
        }
    }
    
    // Controller から呼ばれる（パラメータ変更）
    void set_cutoff(float value) {
        cutoff_.store(value, std::memory_order_relaxed);
    }

private:
    std::atomic<float> cutoff_{1000.0f};
};
```

#### syscall ABI

```cpp
// lib/umi_app/include/umi/syscall.hh

namespace umi::syscall {

enum class Nr : uint32_t {
    Exit            = 0,   // プロセス終了
    RegisterProc    = 1,   // Processor登録
    WaitEvent       = 2,   // イベント待ち（ブロッキング）
    SendEvent       = 3,   // イベント送信
    Log             = 10,  // デバッグログ
    GetTime         = 11,  // 時刻取得
};

inline int32_t call(Nr nr, uint32_t a0 = 0, uint32_t a1 = 0, 
                    uint32_t a2 = 0, uint32_t a3 = 0) {
    register uint32_t r0 __asm__("r0") = static_cast<uint32_t>(nr);
    register uint32_t r1 __asm__("r1") = a0;
    register uint32_t r2 __asm__("r2") = a1;
    register uint32_t r3 __asm__("r3") = a2;
    register uint32_t r4 __asm__("r4") = a3;
    
    __asm__ volatile("svc #0" : "+r"(r0) 
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "memory");
    return static_cast<int32_t>(r0);
}

inline void exit(int code) { call(Nr::Exit, code); }

} // namespace umi::syscall
```

#### アプリSDK API

```cpp
// lib/umi_app/include/umi/app.hh

namespace umi {

// Processor登録
template<typename T>
void register_processor(T& processor) {
    auto fn = [](void* p, ProcessContext& ctx) {
        static_cast<T*>(p)->process(ctx);
    };
    syscall::call(syscall::Nr::RegisterProc,
                  reinterpret_cast<uint32_t>(&processor),
                  reinterpret_cast<uint32_t>(fn));
}

// イベント待ち（Control Task用、ブロッキング）
inline Event wait_event(uint32_t timeout_ms = UINT32_MAX) {
    Event ev;
    syscall::call(syscall::Nr::WaitEvent,
                  reinterpret_cast<uint32_t>(&ev), timeout_ms);
    return ev;
}

// ログ出力
inline void log(const char* msg) {
    syscall::call(syscall::Nr::Log, reinterpret_cast<uint32_t>(msg));
}

} // namespace umi
```

#### 共有メモリ構造

```cpp
// カーネルとアプリで共有
struct SharedRegion {
    // オーディオバッファ
    struct {
        float output[2][BUFFER_SIZE];
        float input[2][BUFFER_SIZE];
        std::atomic<uint32_t> ready;
    } audio;
    
    // イベントキュー（ロックフリー）
    struct {
        Event buffer[64];
        std::atomic<uint32_t> head;
        std::atomic<uint32_t> tail;
    } events;
    
    // パラメータ
    struct {
        float values[MAX_PARAMS];
        std::atomic<uint32_t> dirty;
    } params;
};
```

#### カーネルによる実行

- カーネルは `_start` を Control Task として起動
- `register_processor()` で登録された関数をオーディオタスクから呼び出し
- MPUでアプリのメモリ領域を分離
- 共有メモリ経由でゼロコピーデータ受け渡し

#### Web版（WASM）での実装

Web版も同じ `main()` パターン。プラットフォーム差異はSDKで吸収。

```cpp
// lib/umi_app/wasm/runtime.cc（Web版SDK実装）

#include <emscripten.h>

namespace umi {
namespace {
    void* g_processor = nullptr;
    void (*g_process_fn)(void*, ProcessContext&) = nullptr;
    bool g_has_event = false;
    Event g_pending_event;
}

void register_processor_impl(void* proc, void (*fn)(void*, ProcessContext&)) {
    g_processor = proc;
    g_process_fn = fn;
}

Event wait_event(uint32_t timeout_ms) {
    while (!g_has_event) {
        emscripten_sleep(1);  // Asyncify
    }
    g_has_event = false;
    return g_pending_event;
}

} // namespace umi

// WASM エクスポート
extern "C" {

EMSCRIPTEN_KEEPALIVE void umi_init() {
    extern int main();
    main();
}

EMSCRIPTEN_KEEPALIVE void umi_process(float* in, float* out, uint32_t frames) {
    if (umi::g_process_fn) {
        ProcessContext ctx{in, out, frames};
        umi::g_process_fn(umi::g_processor, ctx);
    }
}

EMSCRIPTEN_KEEPALIVE void umi_push_event(uint32_t type, const uint8_t* data, uint32_t size) {
    umi::g_pending_event = Event::from_bytes(type, data, size);
    umi::g_has_event = true;
}

}
```

| 項目 | 組込み | Web (WASM) |
|------|--------|------------|
| エントリポイント | `_start` → `main()` | `umi_init` → `main()` |
| Processor呼び出し | カーネルタスク | AudioWorklet |
| イベント待ち | syscall（ブロック） | Asyncify（yield） |
| ビルド | arm-none-eabi-gcc | emscripten |

#### コルーチンサポート

Control Task（main）内でC++20コルーチンが使用可能。`lib/umios/kernel/coro.hh` に実装済み。

```cpp
// アプリでのコルーチン使用例
#include <umi/app.hh>
#include <umi/coro.hh>

using namespace umi::coro;
using namespace umi::coro::literals;

// LEDブリンクをコルーチンで
Task<void> led_task(SchedulerContext<8>& ctx) {
    while (true) {
        led_toggle();
        co_await ctx.sleep(500ms);
    }
}

// MIDIハンドラをコルーチンで
Task<void> midi_task(SchedulerContext<8>& ctx) {
    while (true) {
        co_await ctx.wait_for(KernelEvent::MidiReady);
        process_midi();
    }
}

int main() {
    static MySynth synth;
    umi::register_processor(synth);
    
    // コルーチンスケジューラ
    Scheduler<8> sched(umi::syscall::wait, umi::syscall::get_time);
    SchedulerContext ctx(sched);
    
    sched.spawn(led_task(ctx));
    sched.spawn(midi_task(ctx));
    
    sched.run();  // [[noreturn]]
}
```

**コルーチン機能**:
- `Task<T>`: コルーチン戻り値型
- `co_await ctx.sleep(duration)`: 非ブロッキングスリープ
- `co_await ctx.wait_for(mask)`: イベント待ち
- `co_await 100ms`: chrono literalsで直接待機

#### カーネルによるアプリロード

```cpp
// kernel/app_loader.cc

struct AppHeader {
    uint32_t magic;         // 0x414D4955 ("UMIA")
    uint32_t version;
    uint32_t entry_offset;  // _start のオフセット
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t crc32;
    uint8_t signature[64];  // Ed25519（製品アプリのみ）
};

TaskId load_and_start_app(const uint8_t* image, size_t size) {
    auto* header = reinterpret_cast<const AppHeader*>(image);
    
    // 1. 検証（magic, CRC, 署名）
    if (!validate_app_header(header, size)) return {};
    
    // 2. メモリ確保・コピー
    void* code = allocate_app_code(header->text_size);
    void* data = allocate_app_data(header->data_size + header->bss_size);
    void* stack = allocate_app_stack(header->stack_size);
    
    load_sections(image, header, code, data);
    
    // 3. MPU設定
    mpu::configure_app_regions(code, data, stack, header);
    
    // 4. Control Task として起動
    auto _start = reinterpret_cast<void(*)(void)>(
        reinterpret_cast<uintptr_t>(code) + header->entry_offset
    );
    
    TaskConfig cfg{
        .entry = [](void* arg) {
            reinterpret_cast<void(*)(void)>(arg)();
        },
        .arg = reinterpret_cast<void*>(_start),
        .prio = Priority::User,
        .uses_fpu = true,
        .name = "app",
    };
    
    return kernel.create_task(cfg);
}
```

### 4.3 ドライバ（開発版のみ）

ドライバはハードウェアアクセスが必要なためネイティブコード + MPU。

```cpp
// ユーザードライバ: my_peripheral.hh

namespace umi::driver {

struct DriverContext {
    // IRQ登録
    bool request_irq(uint32_t irqn, void (*handler)(void*), void* arg);
    void free_irq(uint32_t irqn);
    
    // DMAバッファ
    void* alloc_dma_buffer(size_t size, size_t align);
    void free_dma_buffer(void* ptr);
    
    // ペリフェラルアクセス（許可されたもののみ）
    volatile void* get_peripheral(uint32_t base_addr);
};

// ドライバエントリポイント
bool my_driver_init(DriverContext* ctx) {
    // ペリフェラル初期化
    auto* spi = ctx->get_peripheral(SPI1_BASE);
    // ...
    return true;
}

} // namespace umi::driver
```

### 4.4 MPUリージョン割当て

```
Cortex-M4 MPU（8リージョン）:

Region 0: Flash - Kernel Code (XN=0, RO, Privileged)
Region 1: Flash - App Code (XN=0, RO, All)
Region 2: SRAM - Kernel Data (RW, Privileged only)
Region 3: SRAM - Shared Buffers (RW, All) ← Audio/MIDI
Region 4: SRAM - App Data (RW, Unprivileged) ← 現在のアプリ
Region 5: Stack Guard (No Access) ← オーバーフロー検出
Region 6: Peripherals - Kernel (RW, Privileged)
Region 7: Peripherals - Allowed (RW, Driver) ← ドライバ用
```

### 4.5 制約事項

#### MPUリージョン数（8個）

| 条件 | 対応 |
|------|------|
| 単一アプリ実行 | ✅ 十分 |
| 複数アプリ同時実行 | ❌ **非対応**（設計対象外） |
| 複数ドライバ | コンテキストスイッチ時に再設定 |

#### MPUがない場合（Cortex-M0等）

MPUの有無は**ビルドシステム側で設定**する。コード内でのマクロ判定は行わない。

```lua
-- xmake.lua
target("firmware")
    -- MPUありターゲット（STM32F4等）
    add_defines("UMI_HAS_MPU=1")
    add_defines("UMI_ALLOW_USER_CODE=1")

target("firmware_m0")
    -- MPUなしターゲット（STM32F0等）
    add_defines("UMI_HAS_MPU=0")
    -- 配布版: ユーザーコードロード禁止
    -- 開発版: 全信頼モード（自己責任）
    if is_mode("release") then
        add_defines("UMI_ALLOW_USER_CODE=0")
    else
        add_defines("UMI_ALLOW_USER_CODE=1")
    end
```

```cpp
// lib/umios/config.hh
namespace umi::config {

// ビルドシステムから設定される
inline constexpr bool HAS_MPU = UMI_HAS_MPU;
inline constexpr bool ALLOW_USER_CODE = UMI_ALLOW_USER_CODE;

} // namespace umi::config
```

**制約**:
- MPUなし + 配布版 → ユーザーコードロード禁止（固定ファームのみ）
- MPUなし + 開発版 → 全信頼モード（自己責任）

#### DMAとMPUの関係

**注意**: MPUはCPUアクセスのみ保護。**DMAはMPUをバイパス**する。

```
対策:
├── DMAバッファはカーネルが管理（固定アドレス）
├── ユーザーコードにDMAレジスタへのアクセスを許可しない
├── アプリからはオフセット/インデックスでのみアクセス
└── 共有バッファはDMA完了後にカーネルがコピー
```

```cpp
// アプリはバッファアドレスを直接知らない
// オフセットでのみアクセス
void process(AudioContext& ctx) {
    // ctx.output(0) はカーネルが管理するアドレスを返す
    // アプリはDMAバッファのアドレスを直接指定できない
    auto* out = ctx.output(0);
    // ...
}
```

#### IRQハンドラの権限

割り込みハンドラは常に特権モードで実行される。

```
対策:
├── ユーザーIRQハンドラは直接登録させない
├── IRQ内ではタスク通知のみ実行
└── 実際の処理はタスクコンテキストで実行
```

**オーディオ処理のフロー**:

オーディオ処理もIRQ内では行わない。IRQはタスク通知のみで、
実際の処理は高優先度のオーディオタスクで行う。

```cpp
// カーネル初期化時: 動的IRQ登録
void setup_audio_irq() {
    namespace irqn = umi::stm32f4::irq;
    
    // I2S DMA完了IRQ
    umi::irq::set_handler(irqn::DMA1_Stream5, +[]() {
        if (dma_i2s.transfer_complete()) {
            dma_i2s.clear_tc();
            g_audio_ctx.i2s_buf = (dma_i2s.current_buffer() == 0) 
                                  ? audio_buf1 : audio_buf0;
            // タスク通知のみ - 処理はタスクで行う
            g_kernel.notify(g_audio_task_id, Event::I2sReady);
        }
    });
}

// オーディオタスク（最高優先度）
void audio_task() {
    while (true) {
        // IRQからの通知を待つ
        auto event = g_kernel.wait(Event::I2sReady | Event::PdmReady);
        
        // タスクコンテキストでアプリのprocess()を呼び出し
        // MPUはコンテキストスイッチ時に設定済み
        current_app->process(audio_ctx);
    }
}
```

**レイテンシ考慮**: オーディオタスクは最高優先度で実行。
IRQ→タスク切り替えのオーバーヘッドは数μs程度で、
48kHz（バッファ64サンプル = 1.3ms）では問題にならない。

#### ウォッチドッグタイマー（必須）

アプリの無限ループ/暴走対策として**ウォッチドッグは必須**。

```cpp
// オーディオコールバック内でキック
void audio_callback() {
    watchdog_kick();  // 正常動作中は定期的にリセット
    // アプリが暴走すると watchdog タイムアウト → リセット
}
```

---

## 5. 実装対策

### 5.1 入力バリデーション（必須）

**リスク**: WebMIDI経由で不正なSysExやCCが送られ、バッファオーバーフローや
予期しない状態遷移を引き起こす。

**対策コスト**: 低（数百行のコード）
**性能影響**: 無視できる（1メッセージあたり数十サイクル）

```cpp
// 推奨実装: lib/umidi/include/umidi/validator.hh

namespace umidi {

inline bool validate_sysex(std::span<const uint8_t> data) {
    // 1. サイズチェック
    if (data.size() > MAX_SYSEX_SIZE) return false;
    // 2. フレーム構造チェック
    if (data.empty() || data.front() != 0xF0 || data.back() != 0xF7) return false;
    // 3. 7bit境界チェック（MIDI規格）
    for (size_t i = 1; i < data.size() - 1; ++i) {
        if (data[i] & 0x80) return false;
    }
    return true;
}

inline bool validate_cc(uint8_t channel, uint8_t cc, uint8_t value) {
    return channel < 16 && cc < 128 && value < 128;
}

} // namespace umidi
```

---

### 5.2 MPUによるメモリ分離

**リスク**: ユーザーアプリのバグ/悪意あるコードがカーネルやドライバを破壊。

ユーザーアプリをロードする場合、**MPU分離は必須**。

```cpp
// コンテキストスイッチ時のMPU再設定
void switch_to_app(AppSlot& app) {
    // Region 4: アプリのデータ領域を設定
    mpu::configure_region(4, app.data_base, app.data_size, 
                          mpu::Access::RW_Unprivileged);
    // Region 1: アプリのコード領域を設定
    mpu::configure_region(1, app.code_base, app.code_size,
                          mpu::Access::RO_All | mpu::Attr::Executable);
}
```

**オーバーヘッド**: コンテキストスイッチ時に約20サイクル追加

---

### 5.3 スタックオーバーフロー検出（カーネルのみ）

**リスク**: バグや攻撃によりスタックが溢れ、戻りアドレスが書き換わる。

**注意**: ユーザーアプリのスタックオーバーフローはMPUで検出する
（Stack Guard Region）。コンパイラのスタックプロテクタはアプリには効かない。

**カーネルの重要な関数のみに適用**:

```cpp
// カーネル内の重要関数にのみ適用
__attribute__((stack_protect))
void kernel_critical_function() {
    char buffer[256];  // スタックバッファを持つ関数
    // ...
}
```

または、カーネルビルドのみで `-fstack-protector-strong`:

```lua
-- xmake.lua
target("kernel")
    add_cxxflags("-fstack-protector-strong")  -- カーネルのみ
    
target("user_app")
    -- スタックプロテクタは無効（MPUで保護）
```

---

### 5.4 署名検証（配布版は必須）

**リスク**: 悪意あるユーザーアプリが配布される。

```cpp
// lib/umiboot/include/umiboot/loader.hh

enum class LoadResult {
    Ok,
    InvalidMagic,
    SizeTooLarge,
    CrcMismatch,
    SignatureInvalid,
    InsufficientMemory,
};

LoadResult load_app(const uint8_t* image, size_t size) {
    auto* header = reinterpret_cast<const AppHeader*>(image);
    
    // 1. CRC検証（常に実行）
    if (!verify_crc(image, size, header->crc32)) {
        return LoadResult::CrcMismatch;
    }
    
    // 2. 署名検証（市販製品の場合）
    if constexpr (config::REQUIRE_APP_SIGNATURE) {
        if (!verify_ed25519(image, size, header->signature)) {
            return LoadResult::SignatureInvalid;
        }
    }
    
    // 3. メモリ確保とMPU設定
    // ...
}
```

**Ed25519を推奨する理由**:
- 署名サイズが小さい（64バイト）
- 検証が高速（Cortex-M4で数十ms）
- monocypher等の軽量実装がある（〜8KB Flash）

---

### 5.5 デバッグポート保護（配布版は必須）

**RDPレベル比較**:

| レベル | 保護内容 | 用途 |
|--------|----------|------|
| Level 0 | なし | 開発 |
| Level 1 | Flash読出し禁止、接続で消去 | **製品出荷** |
| Level 2 | 完全ロック | 高セキュリティ製品（OTA不可） |

**電子楽器の場合**: **Level 1で十分**

設定方法:
```bash
STM32_Programmer_CLI -c port=SWD -ob RDP=0xBB
```

---

## 6. 推奨構成

### 6.1 配布版（一般販売製品）

| 項目 | 設定 |
|------|------|
| ロード可能なアプリ | **製品アプリ + ユーザーアプリ** |
| ドライバ | メーカー製のみ（固定） |
| アプリ実行モード | **非特権（ユーザーモード）** |
| 製品アプリ署名 | **必須**（配布用鍵） |
| ユーザーアプリ署名 | 不要 |
| RDP | **Level 1** |
| ウォッチドッグ | **有効** |

### 6.2 開発版（開発ボード）

| 項目 | 設定 |
|------|------|
| ロード可能なアプリ | **開発アプリ + ユーザーアプリ** |
| ロード可能なドライバ | ユーザードライバ |
| アプリ実行モード | **非特権（ユーザーモード）** |
| ドライバ実行モード | 特権（MPU制限付き） |
| 署名 | 不要 |
| RDP | Level 0（デバッグ可） |
| ウォッチドッグ | **有効** |

---

## 7. 実装優先度まとめ

```
必須（今すぐやる）
├── 入力バリデーション
├── ウォッチドッグ設定
├── アプリローダー + CRC検証
└── カーネル/アプリ互換性制御（署名鍵分離）

配布版で必須
├── Ed25519署名（配布用鍵）
├── MPU設定
└── RDP Level 1

開発版で追加
└── ドライバローダー + ドライバAPI
```
