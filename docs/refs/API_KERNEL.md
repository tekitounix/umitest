# Kernel API

**状態:** ✓ 実装済み（v0.10.0）

組込み環境でのカーネル直接操作。アプリケーションは通常 syscall 経由でアクセス。

```cpp
#include <umios/kernel/umi_kernel.hh>
```

---

## Syscall ABI ✓

```cpp
// lib/umios/app/syscall.hh

namespace umi::syscall::nr {
    // Process control
    constexpr uint32_t Exit          = 0;   // アプリ終了
    constexpr uint32_t RegisterProc  = 1;   // Processor登録

    // Event handling
    constexpr uint32_t WaitEvent     = 2;   // イベント待機（ブロッキング）
    constexpr uint32_t SendEvent     = 3;   // イベント送信
    constexpr uint32_t PeekEvent     = 4;   // イベント確認（非ブロック）
    constexpr uint32_t Yield         = 5;   // 制御を返す

    // Time
    constexpr uint32_t GetTime       = 10;  // 時刻取得（μs）
    constexpr uint32_t Sleep         = 11;  // スリープ

    // Debug/Log
    constexpr uint32_t Log           = 20;  // ログ出力
    constexpr uint32_t Panic         = 21;  // パニック

    // Parameters
    constexpr uint32_t GetParam      = 30;  // パラメータ取得
    constexpr uint32_t SetParam      = 31;  // パラメータ設定

    // Shared memory
    constexpr uint32_t GetShared     = 40;  // 共有メモリ取得
}
```

### 呼び出し規約

ARM Cortex-M では `svc #0` 命令で呼び出し:
- `r0` = syscall番号
- `r1-r4` = 引数
- `r0` = 戻り値

```cpp
// 実装例
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0,
                    uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    register uint32_t r0 __asm__("r0") = nr;
    register uint32_t r1 __asm__("r1") = a0;
    register uint32_t r2 __asm__("r2") = a1;
    register uint32_t r3 __asm__("r3") = a2;
    register uint32_t r4 __asm__("r4") = a3;

    __asm__ volatile("svc #0" : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "memory");
    return static_cast<int32_t>(r0);
}
```

---

## 動的IRQ登録

```cpp
#include <umios/backend/cm/irq.hh>

// IRQハンドラをラムダで登録
umi::irq::init();
umi::irq::set_handler(irqn::DMA1_Stream5, +[]() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        g_kernel.notify(g_audio_task_id, Event::I2sReady);
    }
});
```

---

## Priority (優先度)

```cpp
enum class Priority : uint8_t {
    Realtime = 0,  // オーディオ処理 - 最高
    Server   = 1,  // ドライバ、I/O
    User     = 2,  // アプリケーション
    Idle     = 3,  // バックグラウンド - 最低
};
```

---

## Notification (タスク通知)

```cpp
// 通知を送信 (ISR-safe)
kernel.notify(task_id, Event::AudioReady);

// ブロッキング受信
uint32_t bits = kernel.wait(task_id, Event::AudioReady | Event::MidiReady);
```

---

## SpscQueue (ロックフリーキュー)

```cpp
umi::SpscQueue<int, 64> queue;

// Producer (ISR or task)
queue.try_push(42);

// Consumer (task)
if (auto val = queue.try_pop()) {
    process(*val);
}
```

---

## MPU リージョン設定 ✓

v0.10.0 実装済み:

| リージョン | アドレス範囲 | アクセス | 内容 |
|------------|-------------|----------|------|
| 0 | 0x20000000 | RW | カーネルSRAM |
| 1 | 0x08060000 | RO+X | アプリコード（Flash） |
| 2 | 0x2000C000 | RW | アプリRAM（32KB） |
| 3 | 0x20018000 | RW | 共有メモリ（16KB） |
| 5 | 0x40000000 | Device | ペリフェラル |
| 6 | 0x10000000 | RW | CCM（タスクスタック） |
| 7 | 0x08000000 | RO+X | カーネルFlash |

---

## 関連ドキュメント

- [../README.md](../README.md) - ドキュメント目次
- [API_APPLICATION.md](API_APPLICATION.md) - アプリケーションAPI
- [API_UI.md](API_UI.md) - UI API
- [API_DSP.md](API_DSP.md) - DSPモジュール
- [../specs/ARCHITECTURE.md](../specs/ARCHITECTURE.md) - アーキテクチャ概要
- [../specs/SECURITY.md](../specs/SECURITY.md) - セキュリティ分析
