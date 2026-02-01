# 07 — メモリレイアウト

## 対象ターゲット

本章は主に組み込みターゲット（STM32F407VG）のメモリレイアウトを規定する。
WASM / Plugin ターゲットではホスト OS がメモリを管理するため、本章の制約は適用されない。

## STM32F407VG メモリマップ

| 領域 | アドレス | サイズ | 用途 |
|------|---------|--------|------|
| Flash | 0x0800_0000 – 0x0807_FFFF | 512KB | カーネル + アプリ |
| SRAM | 0x2000_0000 – 0x2001_FFFF | 128KB | カーネル + アプリ + 共有メモリ |
| CCM | 0x1000_0000 – 0x1000_FFFF | 64KB | オーディオバッファ（DMA 不可） |

## SRAM レイアウト (128KB)

```
0x20020000 ┌──────────────────────────┐ ← _estack
           │                          │
           │  App Stack (16KB)        │  grows ↓
           │  + Heap (grows ↑)        │
           │                          │
0x2001C000 ├──────────────────────────┤
           │                          │
           │  (予約 / 拡張用)          │
           │                          │
0x20018000 ├──────────────────────────┤
           │                          │
           │  App Data (32KB)         │  .data / .bss
           │                          │
0x2000C000 ├──────────────────────────┤
           │                          │
           │  Shared HwState (1KB)    │  SharedInputState 等
           │                          │
0x2000A800 ├──────────────────────────┤
           │                          │
           │  Shared MIDI (2KB)       │  イベントキュー、パラメータ
           │                          │
0x2000A000 ├──────────────────────────┤
           │                          │
           │  Shared Audio (8KB)      │  オーディオバッファ
           │                          │
0x20008000 ├──────────────────────────┤
           │                          │
           │  Kernel Data (32KB)      │  .data / .bss / スタック
           │                          │
0x20000000 └──────────────────────────┘
```

### MPU 保護

STM32F4 の MPU はリージョンサイズが 2^n アラインメントの制約があるため、論理レイアウトと MPU リージョンが完全には一致しない。

| リージョン | 論理領域 | MPU 範囲 | MPU サイズ | 権限 | 備考 |
|-----------|---------|---------|-----------|------|------|
| Kernel | 0x2000_0000 – 0x2000_7FFF | 0x2000_0000 – 0x2000_7FFF | 32KB | アプリからアクセス不可 | — |
| Shared | 0x2000_8000 – 0x2000_BFFF | 0x2000_8000 – 0x2000_BFFF | 16KB | RO (アプリ)、RW (カーネル) | — |
| App Data | 0x2000_C000 – 0x2001_7FFF | 0x2000_C000 – 0x2001_3FFF | 32KB | RW (非特権) | MPU は 32KB で保護。論理的には 48KB だが MPU の 2^n 制約で分割 |
| App Stack | 0x2001_C000 – 0x2001_FFFF | 0x2001_C000 – 0x2001_FFFF | 16KB | RW (非特権) | — |
| (予約) | 0x2001_8000 – 0x2001_BFFF | — | 16KB | — | ガードページまたは将来の拡張用。MPU 保護外 |

> **注**: App Data の論理領域 (48KB: 0x2000C000–0x20017FFF) と MPU リージョン (32KB: 0x2000C000–0x20013FFF) にギャップがある。0x20014000–0x20017FFF は MPU App Data リージョンに含まれないため、アプリがこの範囲にアクセスすると MemManage Fault が発生する。実際の App Data 使用量が 32KB を超える場合は MPU 設定の見直しが必要。

## Flash レイアウト (512KB)

```
0x08080000 ┌──────────────────────────┐
           │                          │
           │  App (.umia)             │  256KB (sector 6-7)
           │  ├ AppHeader (128B)      │
           │  ├ .text                 │
           │  ├ .rodata               │
           │  └ .data (初期値)        │
           │                          │
0x08040000 ├──────────────────────────┤
           │                          │
           │  Kernel                  │  240KB (sector 0-5)
           │  ├ Vector Table          │
           │  ├ .text                 │
           │  ├ .rodata               │
           │  └ .data (初期値)        │
           │                          │
0x08000000 └──────────────────────────┘
```

> **注**: STM32F4 の Flash セクターサイズは不均一（16K×4 + 64K×1 + 128K×3 = 512KB）。
> Kernel は sector 0–5 (16K×4 + 64K + 128K = 240KB)、App は sector 6–7 (128K×2 = 256KB) を使用する。

## CCM (Core Coupled Memory) レイアウト (64KB)

CCM は DMA アクセス不可だが、CPU からは最速でアクセスできる。
オーディオ処理の作業バッファに使用する。

```
0x10010000 ┌──────────────────────────┐
           │  (未使用)                 │
0x10002000 ├──────────────────────────┤
           │  Audio Work Buffers      │  ~8KB
           │  ├ i2s_work_buf          │  512B (128 × int32_t)
           │  ├ synth_out_mono        │  256B (64 × float)
           │  └ last_synth_out        │  256B (64 × int16_t × 2)
0x10000000 └──────────────────────────┘
```

## 共有メモリ (.shared)

### SharedMemory 構造体

カーネルとアプリ間で共有するメモリ領域。アプリは `umi::get_shared()` syscall でポインタを取得する。

SharedMemory の完全な構造体定義（全メンバー、サイズ、書き込み権限、内包構造体）は [10-shared-memory.md](10-shared-memory.md) を参照。

SharedMemory 全体のサイズは約 5.9KB で、上記の Shared Audio (8KB) + Shared MIDI (2KB) + Shared HwState (1KB) = 11KB の領域内に収まる。

### リンカシンボル

```
_shared_audio_start   = 0x20008000  (8KB)
_shared_midi_start    = 0x2000A000  (2KB)
_shared_hwstate_start = 0x2000A800  (1KB)
```

カーネル起動時に `SharedMemorySymbols` 経由でこれらのアドレスを参照し、SharedMemory を構築する。

## バッファサイズの階層

| レベル | サイズ | 周期 (@48kHz) | 用途 |
|--------|--------|---------------|------|
| DMA バッファ | 64 サンプル | ~1.33ms | I2S DMA 転送単位 |
| アプリバッファ | 256 サンプル | ~5.33ms | `process()` 呼び出し単位 |
| USB リングバッファ | 512 サンプル | — | USB Audio ドリフト補正 |

### DMA バッファ (64 サンプル)

```cpp
namespace mcu::audio {
    inline constexpr uint32_t buffer_size = 64;
    inline constexpr uint32_t i2s_words_per_frame = 4;       // stereo 24bit → 4 × 16bit
    inline constexpr uint32_t i2s_dma_words = buffer_size * i2s_words_per_frame;  // 256
}
```

DMA Half/Complete 割り込みで Audio Task に通知される。

### アプリバッファ (256 サンプル)

```cpp
struct StreamConfig {
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = 256;     // process() に渡されるバッファサイズ
};
```

DMA バッファ 4 回分 (64 × 4 = 256) を蓄積してから `process()` を呼ぶ。
レイテンシとパフォーマンスのトレードオフ。

> **旧ドキュメントとの差異**:
> - ARCHITECTURE.md の SharedMemory は `buffer_size = 256` (AUDIO_BUFFER_FRAMES)
> - 実装の DMA 転送は `buffer_size = 64`
> - 両方正しいが異なるレベルの値。本仕様で階層を明確化

### USB リングバッファ

```cpp
struct AudioModuleConfig {
    uint32_t ring_size = 512;       // リングバッファ総容量
    uint32_t ring_target = 256;     // 目標充填レベル
    uint32_t ring_high = 384;       // 上限（超過時スキップ）
    uint32_t ring_low = 128;        // 下限（下回り時デュプリケート）
};
```

USB Audio と I2S のクロック差を吸収する。PID フィードバックで SOF レートを制御。

## OS 側メモリ使用量の概算

| 項目 | サイズ | 備考 |
|------|--------|------|
| カーネル .text + .rodata | ~64KB | Flash |
| カーネル .data + .bss | ~8KB | SRAM |
| カーネルスタック | ~4KB | SRAM |
| Vector Table (SRAM) | ~512B | VTOR 切り替え |
| Shared Audio | 8KB | オーディオバッファ |
| Shared MIDI | 2KB | イベントキュー |
| Shared HwState | 1KB | I/O 状態 |
| **合計 SRAM** | **~24KB** | 128KB 中 |
| **アプリ利用可能** | **~48KB** | .data 32KB + stack/heap 16KB |
