# UMI 実装計画 v2

## 概要

本ドキュメントは、UMI フレームワークを新仕様に更新するための実装計画です。

**核心的アーキテクチャ変更:**
- **カーネルとアプリケーションは完全に分離されたバイナリ**
- カーネルがアプリケーション（`.umiapp`）をロードして実行
- アプリは非特権モードで動作し、syscall でカーネルAPIにアクセス

**目標:** カーネル/アプリ分離アーキテクチャを実装し、stm32f4_synth で動作確認

---

## アーキテクチャ概要

```
┌─────────────────────────────────────────────────────────────┐
│                    Kernel Binary (.elf)                     │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐           │
│  │ Scheduler   │ │ App Loader  │ │ Audio DMA   │           │
│  └─────────────┘ └─────────────┘ └─────────────┘           │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐           │
│  │ Syscall     │ │ MPU Config  │ │ USB Driver  │           │
│  └─────────────┘ └─────────────┘ └─────────────┘           │
│  特権モード / 全リソースアクセス可                          │
├─────────────────────────────────────────────────────────────┤
│                    Shared Memory Region                     │
│  AudioBuffer / EventQueue / ParamBlock                      │
├─────────────────────────────────────────────────────────────┤
│                 Application Binary (.umiapp)                │
│  ┌─────────────────────────────────────────┐               │
│  │            Processor Task               │ 最高優先度    │
│  │  process() - カーネルから呼び出し        │               │
│  └─────────────────────────────────────────┘               │
│  ┌─────────────────────────────────────────┐               │
│  │            Control Task (main)          │ 低優先度      │
│  │  イベント処理、UI、パラメータ管理         │               │
│  └─────────────────────────────────────────┘               │
│  非特権モード / syscall 経由でカーネルAPI呼び出し            │
└─────────────────────────────────────────────────────────────┘
```

---

## 対象プラットフォーム

### STM32F4-Discovery

| 項目 | 仕様 |
|------|------|
| MCU | STM32F407VG (Cortex-M4F, 168MHz) |
| RAM | 192KB (CCM: 64KB + SRAM: 128KB) |
| Flash | 1MB |
| MPU | 8 リージョン（Cortex-M4） |
| Audio DAC | CS43L22 (I2C + I2S) |
| USB | Full-Speed OTG |

### 新ファイル構成（目標）

```
lib/
├── umios/
│   ├── kernel/              # カーネル実装
│   │   ├── loader.hh        # アプリローダー（新規）
│   │   ├── syscall_handler.hh # syscallハンドラ（新規）
│   │   ├── mpu_config.hh    # MPU設定（新規）
│   │   └── umi_kernel.hh    # カーネルコア（既存）
│   └── app/                 # アプリSDK（新規）
│       ├── crt0.cc          # アプリスタートアップ
│       ├── syscall.hh       # syscallラッパー
│       └── umi_app.hh       # アプリAPI
│
examples/
├── stm32f4_kernel/          # カーネルバイナリ（新規）
│   ├── xmake.lua
│   └── src/main.cc
│
├── synth_app/               # アプリバイナリ（新規）
│   ├── xmake.lua
│   └── src/
│       ├── main.cc          # Control Task
│       └── synth.hh         # Processor
```

---

## Phase 1: カーネルコア実装

### 目標
アプリをロード・実行できるカーネルの骨格を実装

### 1.1 アプリヘッダ形式

```cpp
// lib/umios/kernel/app_header.hh

struct AppHeader {
    uint32_t magic;           // 0x554D4941 ("UMIA")
    uint32_t version;         // ABI バージョン
    uint32_t entry_offset;    // _start のオフセット
    uint32_t process_offset;  // process() のオフセット
    uint32_t text_size;       // コードサイズ
    uint32_t data_size;       // 初期化データサイズ
    uint32_t bss_size;        // 未初期化データサイズ
    uint32_t stack_size;      // スタックサイズ
    uint32_t crc32;           // CRC検証
    uint8_t signature[64];    // Ed25519署名（製品版のみ）
};
```

### 1.2 アプリローダー

```cpp
// lib/umios/kernel/loader.hh

class AppLoader {
public:
    enum class LoadResult {
        Ok,
        InvalidMagic,
        InvalidVersion,
        CrcMismatch,
        SignatureInvalid,
        OutOfMemory,
    };

    /// アプリイメージを検証・ロード
    LoadResult load(const uint8_t* image, size_t size);
    
    /// Processor の process() を呼び出す（Audio ISR から）
    void call_process(AudioContext& ctx);
    
    /// Control Task の main() を開始
    void start_control_task();

private:
    void* entry_point_ = nullptr;
    void* process_fn_ = nullptr;
    void* app_base_ = nullptr;
    size_t app_size_ = 0;
};
```

### 1.3 Syscall ハンドラ

```cpp
// lib/umios/kernel/syscall_handler.hh

// ARM Cortex-M: SVC 例外ハンドラ
extern "C" void SVC_Handler() {
    // スタックから syscall 番号と引数を取得
    uint32_t* sp;
    __asm__ volatile("mrs %0, psp" : "=r"(sp));  // PSP（プロセススタック）
    
    uint32_t syscall_nr = sp[0];  // r0 = syscall番号
    uint32_t arg0 = sp[1];        // r1
    uint32_t arg1 = sp[2];        // r2
    uint32_t arg2 = sp[3];        // r3
    
    int32_t result = handle_syscall(syscall_nr, arg0, arg1, arg2);
    sp[0] = result;  // r0 に戻り値
}

int32_t handle_syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2) {
    switch (nr) {
    case syscall::Exit:
        kernel.terminate_app(a0);
        return 0;
    case syscall::RegisterProc:
        return kernel.register_processor(reinterpret_cast<void*>(a0));
    case syscall::WaitEvent:
        return kernel.wait_event(reinterpret_cast<Event*>(a0));
    case syscall::SendEvent:
        return kernel.send_event(*reinterpret_cast<Event*>(a0));
    case syscall::Log:
        return kernel.log(reinterpret_cast<const char*>(a0), a1);
    case syscall::GetTime:
        return kernel.get_time_usec();
    default:
        return -1;  // Unknown syscall
    }
}
```

### 1.4 MPU 設定

```cpp
// lib/umios/kernel/mpu_config.hh

/// アプリ用MPU設定（非特権モードで実行）
void configure_app_mpu(const AppHeader* header, void* app_base) {
    // Region 0: カーネル（特権のみ）
    mpu_set_region(0, kernel_base, kernel_size, 
                   MPU_PRIV_RO | MPU_EXEC);
    
    // Region 1: アプリ .text（読取専用、実行可）
    mpu_set_region(1, app_base, header->text_size,
                   MPU_FULL_ACCESS | MPU_EXEC);
    
    // Region 2: アプリ .data/.bss（読書可、実行不可）
    void* data_base = (uint8_t*)app_base + header->text_size;
    mpu_set_region(2, data_base, header->data_size + header->bss_size,
                   MPU_FULL_ACCESS | MPU_XN);
    
    // Region 3: アプリスタック
    mpu_set_region(3, stack_base, header->stack_size,
                   MPU_FULL_ACCESS | MPU_XN);
    
    // Region 4: 共有メモリ（AudioBuffer, EventQueue）
    mpu_set_region(4, shared_base, shared_size,
                   MPU_FULL_ACCESS | MPU_XN);
}
```

### 作業項目
- [ ] `lib/umios/kernel/app_header.hh` 作成
- [ ] `lib/umios/kernel/loader.hh` / `loader.cc` 作成
- [ ] `lib/umios/kernel/syscall_handler.hh` 作成
- [ ] `lib/umios/kernel/mpu_config.hh` 作成
- [ ] `examples/stm32f4_kernel/` 作成（カーネルバイナリ）

### 参照ドキュメント
- [SECURITY.md](../specs/SECURITY.md) - セキュリティモデル
- [API_KERNEL.md](../reference/API_KERNEL.md) - Syscall ABI
- [UMIM_SPEC.md](../UMIM_SPEC.md) - バイナリ形式

---

## Phase 2: アプリ SDK 実装

### 目標
アプリ開発者が使用する SDK を実装

### 2.1 アプリスタートアップ (crt0)

```cpp
// lib/umios/app/crt0.cc

extern "C" {

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

// カーネルから呼び出されるエントリポイント
void _start() {
    // グローバルコンストラクタ実行
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }
    
    // main() 実行
    extern int main();
    int ret = main();
    
    // カーネルに終了通知（syscall）
    umi::syscall::exit(ret);
}

} // extern "C"
```

### 2.2 Syscall ラッパー

```cpp
// lib/umios/app/syscall.hh

namespace umi::syscall {

inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0, 
                    uint32_t a2 = 0, uint32_t a3 = 0) {
    register uint32_t r0 __asm__("r0") = nr;
    register uint32_t r1 __asm__("r1") = a0;
    register uint32_t r2 __asm__("r2") = a1;
    register uint32_t r3 __asm__("r3") = a2;
    register uint32_t r4 __asm__("r4") = a3;
    
    __asm__ volatile("svc #0" : "+r"(r0) 
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "memory");
    return static_cast<int32_t>(r0);
}

inline void exit(int code) { call(Nr::Exit, code); }
inline int register_processor(void* proc) { return call(Nr::RegisterProc, (uint32_t)proc); }
inline Event wait_event() { Event ev; call(Nr::WaitEvent, (uint32_t)&ev); return ev; }
inline void log(const char* msg) { call(Nr::Log, (uint32_t)msg, strlen(msg)); }

} // namespace umi::syscall
```

### 2.3 アプリ API

```cpp
// lib/umios/app/umi_app.hh

namespace umi {

/// Processor を登録（syscall経由）
template<ProcessorLike P>
void register_processor(P& processor) {
    // 関数ポインタをカーネルに登録
    auto process_fn = [](void* p, AudioContext& ctx) {
        static_cast<P*>(p)->process(ctx);
    };
    syscall::register_processor(&processor, process_fn);
}

/// イベント待ち（syscall経由、ブロッキング）
inline Event wait_event() {
    return syscall::wait_event();
}

/// イベント送信（syscall経由）
inline void send_event(const Event& ev) {
    syscall::send_event(ev);
}

} // namespace umi
```

### 2.4 アプリリンカスクリプト

```ld
/* lib/umios/app/app.ld */

MEMORY {
    /* カーネルがロード時に設定 */
    APP_TEXT (rx)  : ORIGIN = 0x20010000, LENGTH = 32K
    APP_DATA (rwx) : ORIGIN = 0x20018000, LENGTH = 16K
}

SECTIONS {
    /* アプリヘッダ（先頭に配置） */
    .app_header : {
        KEEP(*(.app_header))
    } > APP_TEXT
    
    .text : { *(.text*) } > APP_TEXT
    .rodata : { *(.rodata*) } > APP_TEXT
    .data : { *(.data*) } > APP_DATA
    .bss : { *(.bss*) } > APP_DATA
}
```

### 作業項目
- [ ] `lib/umios/app/crt0.cc` 作成
- [ ] `lib/umios/app/syscall.hh` 作成
- [ ] `lib/umios/app/umi_app.hh` 作成
- [ ] `lib/umios/app/app.ld` 作成（リンカスクリプト）
- [ ] `lib/umios/app/xmake.lua` 作成（アプリSDKビルド設定）

### 参照ドキュメント
- [UMIM_SPEC.md](../UMIM_SPEC.md) - バイナリ形式
- [API_APPLICATION.md](../reference/API_APPLICATION.md) - アプリAPI

---

## Phase 3: ビルドシステム構築

### 目標
カーネルとアプリを別々にビルドする仕組み

### 3.1 xmake.lua 構成

```lua
-- lib/umios/kernel/xmake.lua
target("umios_kernel")
    set_kind("static")
    add_files("*.cc")
    add_includedirs(".", {public = true})
    add_defines("UMIOS_KERNEL=1")

-- lib/umios/app/xmake.lua  
target("umios_app_sdk")
    set_kind("static")
    add_files("crt0.cc")
    add_includedirs(".", {public = true})
    add_defines("UMIOS_APP=1")
    set_toolchains("arm-none-eabi")

-- examples/stm32f4_kernel/xmake.lua
target("stm32f4_kernel")
    set_kind("binary")
    add_deps("umios_kernel", "bsp_stm32f4_disco")
    add_files("src/*.cc")
    add_ldflags("-T", "kernel.ld")

-- examples/synth_app/xmake.lua
target("synth_app")
    set_kind("binary")
    add_deps("umios_app_sdk")
    add_files("src/*.cc")
    add_ldflags("-T", "$(projectdir)/lib/umios/app/app.ld")
    set_extension(".umiapp")
    after_build(function (target)
        -- バイナリからヘッダ付き .umiapp を生成
        os.execv("scripts/make_umiapp.py", {target:targetfile()})
    end)
```

### 3.2 ビルドコマンド

```bash
# カーネルビルド
xmake build stm32f4_kernel

# アプリビルド
xmake build synth_app

# 両方をフラッシュ
pyocd flash -t stm32f407vg build/stm32f4_kernel/release/stm32f4_kernel.elf
# アプリは別領域（例: 0x08040000）に書き込み
pyocd flash -t stm32f407vg -a 0x08040000 build/synth_app/release/synth_app.umiapp
```

### 作業項目
- [ ] `lib/umios/kernel/xmake.lua` 更新
- [ ] `lib/umios/app/xmake.lua` 作成
- [ ] `examples/stm32f4_kernel/xmake.lua` 作成
- [ ] `examples/synth_app/xmake.lua` 作成
- [ ] `scripts/make_umiapp.py` 作成（バイナリ変換）
- [ ] カーネル用リンカスクリプト `kernel.ld` 作成

---

## Phase 4: 動作確認・統合

### 目標
カーネル + アプリで動作確認

### 4.1 カーネル main.cc

```cpp
// examples/stm32f4_kernel/src/main.cc

#include <umios/kernel/umi_kernel.hh>
#include <umios/kernel/loader.hh>
#include <bsp/stm32f4_disco.hh>

// アプリイメージ（Flash の別領域に配置）
extern const uint8_t _app_image_start[];
extern const uint8_t _app_image_end[];

int main() {
    // ハードウェア初期化
    bsp::init_clocks();
    bsp::init_audio();
    bsp::init_usb();
    
    // カーネル起動
    Kernel kernel;
    kernel.init();
    
    // アプリロード
    AppLoader loader;
    auto result = loader.load(_app_image_start, 
                              _app_image_end - _app_image_start);
    if (result != AppLoader::LoadResult::Ok) {
        bsp::led_error();
        while (true) {}
    }
    
    // Audio Task（カーネルタスク）
    kernel.create_task({
        .entry = [](void* arg) {
            auto* loader = static_cast<AppLoader*>(arg);
            while (true) {
                kernel.wait(Event::AudioReady);
                AudioContext ctx = kernel.get_audio_context();
                loader->call_process(ctx);  // アプリの process() を呼び出し
            }
        },
        .arg = &loader,
        .prio = Priority::Realtime,
        .name = "audio",
    });
    
    // アプリの Control Task を起動
    loader.start_control_task();
    
    // カーネルスケジューラ開始
    kernel.run();
}
```

### 4.2 アプリ main.cc

```cpp
// examples/synth_app/src/main.cc

#include <umi/app.hh>
#include "synth.hh"

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        
        if (ev.is_shutdown()) break;
        
        switch (ev.type) {
        case EventType::NoteOn:
            synth.note_on(ev.note.number, ev.note.velocity);
            break;
        case EventType::NoteOff:
            synth.note_off(ev.note.number);
            break;
        case EventType::ParamChange:
            synth.set_param(ev.param.id, ev.param.value);
            break;
        }
    }
    
    return 0;
}
```

### 作業項目
- [ ] `examples/stm32f4_kernel/src/main.cc` 作成
- [ ] `examples/synth_app/src/main.cc` 作成
- [ ] `examples/synth_app/src/synth.hh` 移植（headless_webhost から）
- [ ] Renode シミュレーションで動作確認
- [ ] 実機での動作確認

---

## Phase 5: 旧コード移行

### 目標
既存の stm32f4_synth を新アーキテクチャに移行

### 作業項目
- [ ] `examples/stm32f4_synth/` をアーカイブ（`_archive/`）
- [ ] カーネル部分を `examples/stm32f4_kernel/` に移動
- [ ] アプリ部分を `examples/synth_app/` に移動
- [ ] DSP モジュール（synth.hh）の互換性確認
- [ ] UI/MIDI 処理の移行

---

## Phase 6: セキュリティ強化（オプション）

### 目標
製品版に向けたセキュリティ機能

### 作業項目
- [ ] Ed25519 署名検証実装
- [ ] RDP（Read Protection）設定
- [ ] 開発版/配布版ビルド切替
- [ ] 署名ツール作成

---

## チェックリスト

### Phase 1: カーネルコア実装
- [x] AppHeader 定義
- [x] AppLoader 実装
- [x] Syscall ハンドラ実装
- [x] MPU 設定実装
- [x] stm32f4_kernel ターゲット作成

### Phase 2: アプリ SDK 実装
- [x] crt0.cc スタートアップ
- [x] syscall.hh ラッパー
- [x] umi_app.hh アプリAPI
- [x] app.ld リンカスクリプト

### Phase 3: ビルドシステム
- [x] xmake.lua 設定
- [x] make_umiapp.py スクリプト
- [x] カーネル/アプリ別ビルド確認

### Phase 4: 動作確認
- [x] カーネル単体起動（ビルド成功）
- [x] アプリロード成功（ビルド成功）
- [x] process() 呼び出し動作（API実装完了）
- [x] syscall 動作（wait_event等）
- [ ] オーディオ出力確認（実機テスト必要）

### Phase 5: 旧コード移行
- [ ] stm32f4_synth アーカイブ
- [ ] コード分離完了
- [ ] 動作互換性確認

---

## 参照ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [ARCHITECTURE.md](../specs/ARCHITECTURE.md) | システムアーキテクチャ |
| [SECURITY.md](../specs/SECURITY.md) | セキュリティモデル |
| [UMIM_SPEC.md](../UMIM_SPEC.md) | バイナリ形式 |
| [API_KERNEL.md](../reference/API_KERNEL.md) | Syscall ABI |
| [API_APPLICATION.md](../reference/API_APPLICATION.md) | アプリケーション API |

### Phase 7: ビルド・テスト
- [ ] ビルド成功
- [ ] ユニットテスト通過
- [ ] フラッシュ成功
- [ ] USB Audio 動作確認
- [ ] MIDI 動作確認

---

## 関連ドキュメント

### 仕様
- [ARCHITECTURE.md](../specs/ARCHITECTURE.md) - アーキテクチャ
- [UMIP.md](../specs/UMIP.md) - Processor 仕様
- [UMIC.md](../specs/UMIC.md) - Controller 仕様
- [UMIM.md](../specs/UMIM.md) - Module 仕様
- [CONCEPTS.md](../specs/CONCEPTS.md) - Concepts 設計
- [SECURITY.md](../specs/SECURITY.md) - セキュリティ設計

### リファレンス
- [API_APPLICATION.md](../reference/API_APPLICATION.md) - アプリケーション API
- [API_DSP.md](../reference/API_DSP.md) - DSP モジュール
- [API_UI.md](../reference/API_UI.md) - UI API
- [API_BSP.md](../reference/API_BSP.md) - BSP I/O 型定義
- [API_KERNEL.md](../reference/API_KERNEL.md) - Kernel API

### 開発
- [CODING_STYLE.md](CODING_STYLE.md) - コーディングスタイル
- [DEBUG_GUIDE.md](DEBUG_GUIDE.md) - デバッグガイド
- [SIMULATION.md](SIMULATION.md) - シミュレーション

### ガイド
- [APPLICATION.md](../guides/APPLICATION.md) - 実装パターン
- [TESTING.md](../guides/TESTING.md) - テスト戦略
