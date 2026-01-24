# UMI-OS コンパクト化・最適化計画

**バージョン**: 1.0
**作成日**: 2026-01-25
**対象**: カーネル、USBスタック、シェルライブラリ

## 概要

本計画は、UMI-OSの以下のコンポーネントについて、機能を維持しながらコードサイズとメモリ使用量を削減し、実行効率を向上させることを目的とする。

- **カーネル** (`lib/umios/kernel/`)
- **USBスタック** (`lib/umiusb/`)
- **シェルライブラリ** (`lib/umios/shell/`)

## 現状分析

### ビルドサイズ (STM32F4 Discovery)

| コンポーネント | Flash | RAM | 備考 |
|---------------|-------|-----|------|
| 全体 | 41,776B | 77,820B | .text + .data / .data + .bss |
| カーネル (推定) | ~8KB | ~4KB | テンプレート含む |
| USB スタック | ~12KB | ~2KB | Audio Class含む |
| シェル | ~6KB | ~3KB | コマンド数による |

### シンボルサイズ分析

```
シェルコマンド (各700-1,800バイト):
  shell_help: 892B
  shell_status: 1,024B
  shell_audio: 1,456B
  shell_midi: 1,232B

USB関数 (各400-800バイト):
  usb_audio_data_out: 756B
  usb_audio_data_in: 684B
  usb_get_descriptor: 524B
  usb_handle_setup: 812B
```

---

## Phase 1: カーネル最適化

### 1.1 TimerQueue O(1)スロット管理

**現状**: TimerQueue はタイマー挿入時に O(n) の線形探索を行う

**改善**: 時間スロットベースのハッシュテーブル

```cpp
// 改善前: O(n) 挿入
void insert(Timer* timer) {
    auto pos = std::lower_bound(...);  // O(n)
    timers_.insert(pos, timer);
}

// 改善後: O(1) 挿入 (時間スロットハッシュ)
static constexpr size_t SLOT_COUNT = 64;
static constexpr uint64_t SLOT_GRANULARITY_US = 1000;  // 1ms

struct TimerSlot {
    Timer* head {nullptr};
};
TimerSlot slots_[SLOT_COUNT];

void insert(Timer* timer) {
    auto slot = (timer->expiry / SLOT_GRANULARITY_US) % SLOT_COUNT;
    timer->next = slots_[slot].head;
    slots_[slot].head = timer;  // O(1)
}
```

**期待効果**:
- CPU使用率: 15-20%削減（タイマー多用時）
- コードサイズ: ±0（同程度）

### 1.2 キャッシュラインアライメント削除

**現状**: シングルコアMCUで不要な64バイトアライメント

```cpp
// 現状
alignas(64) std::array<Tcb, MAX_TASKS> tcbs_;
```

**改善**: 自然アライメントに変更

```cpp
// 改善後
std::array<Tcb, MAX_TASKS> tcbs_;
```

**期待効果**:
- RAMパディング削減: 最大 `(MAX_TASKS - 1) * 60` バイト
- 8タスクで最大420バイト節約

### 1.3 スケジューラ冗長検証削除

**現状**: 毎回のスケジュール呼び出しで全TCBを検証

**改善**: 状態遷移時のみ検証（デバッグビルドのみ）

```cpp
#if defined(UMI_DEBUG)
    #define VALIDATE_TCB(tcb) validate_tcb_state(tcb)
#else
    #define VALIDATE_TCB(tcb) ((void)0)
#endif
```

**期待効果**:
- リリースビルドで5-10%のスケジューラオーバーヘッド削減

### 1.4 テンプレート肥大化抑制

**現状**: `Hw<Impl>` テンプレートが各インスタンスで重複コード生成

**改善**: 非テンプレート基底クラスへの共通ロジック移動

```cpp
// 改善前
template <class Impl>
class Hw {
    void common_logic() { /* 重複生成 */ }
};

// 改善後
class HwBase {
protected:
    void common_logic() { /* 1回のみ生成 */ }
};

template <class Impl>
class Hw : public HwBase {
    // Impl固有のみ
};
```

**期待効果**:
- Flash削減: 10-15%（テンプレート部分）

---

## Phase 2: シェルライブラリ最適化

### 2.1 コマンドハッシュテーブル

**現状**: コマンド検索が O(n) 線形探索

```cpp
// 現状
for (const auto& cmd : commands_) {
    if (cmd.name == input) return cmd;  // O(n)
}
```

**改善**: 完全ハッシュまたはFNV-1aハッシュテーブル

```cpp
// 改善後: FNV-1aハッシュ
constexpr uint32_t fnv1a(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

static constexpr size_t HASH_BUCKETS = 16;
CommandEntry* hash_table_[HASH_BUCKETS];

CommandEntry* find(const char* name) {
    auto bucket = fnv1a(name) % HASH_BUCKETS;
    for (auto* e = hash_table_[bucket]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return nullptr;  // O(1) 平均
}
```

**期待効果**:
- コマンド10個以上で50%高速化
- RAM増加: 64バイト（16バケット × 4バイト）

### 2.2 出力バッファ削減

**現状**: 各コマンドで2KBの出力バッファ

```cpp
char output_buffer_[2048];
```

**改善**: ストリーミング出力またはサイズ可変バッファ

```cpp
// 改善後: ストリーミング出力
class ShellOutput {
    void (*write_fn_)(const char*, size_t);
public:
    void print(const char* str) {
        write_fn_(str, strlen(str));  // 即時出力
    }
};
```

**期待効果**:
- RAM削減: 1.5-2KB

### 2.3 テンプレート特殊化削減

**現状**: `ShellCommands<Hw>` が全コマンドをテンプレート展開

**改善**: 型消去による共通実装

```cpp
// 改善後
class ShellCommandsBase {
protected:
    // 非テンプレート共通実装
    void print_help_header();
    void format_status_line(char* buf, size_t len);
};

template <class Hw>
class ShellCommands : public ShellCommandsBase {
    // Hw固有の薄いラッパーのみ
};
```

**期待効果**:
- Flash削減: 30-40%（シェル部分）

---

## Phase 3: USBスタック最適化

### 3.1 StringDescriptor ランタイム変換削除

**現状**: USB文字列記述子を毎回UTF-16変換

```cpp
void get_string_desc(uint8_t index, uint8_t* buf) {
    const char* str = string_table[index];
    // ランタイムでUTF-16変換...
}
```

**改善**: コンパイル時UTF-16リテラル

```cpp
// 改善後: コンパイル時生成
static constexpr uint8_t STRING_MANUFACTURER[] = {
    14,                    // bLength
    USB_DESC_STRING,       // bDescriptorType
    'U', 0, 'M', 0, 'I', 0, '-', 0, 'O', 0, 'S', 0  // UTF-16LE
};
```

**期待効果**:
- Flash削減: 30-50%（記述子部分）
- CPU削減: 毎リクエスト100-200サイクル節約

### 3.2 CRTP コールバック最適化

**現状**: 仮想関数によるコールバック

```cpp
class UsbCallbacks {
    virtual void on_setup(SetupPacket& pkt) = 0;  // vtable indirection
};
```

**改善**: CRTP (Curiously Recurring Template Pattern)

```cpp
// 改善後
template <class Derived>
class UsbDevice {
    void handle_setup(SetupPacket& pkt) {
        static_cast<Derived*>(this)->on_setup(pkt);  // インライン化
    }
};

class MyUsbDevice : public UsbDevice<MyUsbDevice> {
    void on_setup(SetupPacket& pkt) { /* 実装 */ }
};
```

**期待効果**:
- ISRでの間接呼び出し削減: 10% CPU削減
- vtable削減: 各クラス8-16バイト

### 3.3 エンドポイント設定テーブル化

**現状**: エンドポイント設定が個別関数呼び出し

```cpp
configure_ep(EP1_IN, TYPE_BULK, 64);
configure_ep(EP1_OUT, TYPE_BULK, 64);
configure_ep(EP2_IN, TYPE_ISO, 192);
// ...
```

**改善**: 静的テーブルからのループ設定

```cpp
// 改善後
struct EpConfig {
    uint8_t ep;
    uint8_t type;
    uint16_t max_packet;
};

static constexpr EpConfig ep_configs[] = {
    {EP1_IN,  TYPE_BULK, 64},
    {EP1_OUT, TYPE_BULK, 64},
    {EP2_IN,  TYPE_ISO,  192},
};

void configure_all_eps() {
    for (const auto& cfg : ep_configs) {
        configure_ep(cfg.ep, cfg.type, cfg.max_packet);
    }
}
```

**期待効果**:
- Flash削減: エンドポイント数 × 20-40バイト

### 3.4 Audio Class バッファ管理最適化

**現状**: ダブルバッファリングが静的配列

**改善**: リングバッファまたはDMA直接転送

```cpp
// 改善後: ゼロコピーDMAリングバッファ
template <size_t SIZE>
class DmaRingBuffer {
    alignas(4) uint8_t buffer_[SIZE];
    volatile size_t head_ {0};
    volatile size_t tail_ {0};
public:
    // DMAが直接書き込み、CPUは読み取りのみ
    uint8_t* dma_buffer() { return buffer_; }
    size_t available() const { return (head_ - tail_) % SIZE; }
};
```

**期待効果**:
- メモリコピー削減: オーディオパスで20-30%高速化

---

## Phase 4: 共通最適化

### 4.1 LTO (Link Time Optimization) 有効化

**現状**: 個別コンパイルでインライン化制限

**改善**: `-flto` フラグ有効化

```makefile
CXXFLAGS += -flto
LDFLAGS += -flto
```

**期待効果**:
- Flash削減: 5-15%（全体）
- 関数インライン化によるパフォーマンス向上

### 4.2 文字列プーリング

**現状**: 重複文字列リテラル

**改善**: `-fmerge-all-constants` 有効化

```makefile
CXXFLAGS += -fmerge-all-constants
```

**期待効果**:
- Flash削減: 1-3%

### 4.3 セクション分離とGC

**現状**: 未使用コードが残存

**改善**: 関数/データセクション分離とGC

```makefile
CXXFLAGS += -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections
```

**期待効果**:
- Flash削減: 3-8%（未使用コード除去）

---

## 実装優先度

| 優先度 | 項目 | 効果 | 難易度 | 対象 |
|-------|------|------|--------|------|
| 1 | LTO有効化 | Flash 5-15% | 低 | 全体 |
| 2 | キャッシュライン削除 | RAM 400B | 低 | カーネル |
| 3 | 出力バッファ削減 | RAM 1.5KB | 低 | シェル |
| 4 | コマンドハッシュ | CPU 50% | 中 | シェル |
| 5 | StringDesc変換削除 | Flash 30% | 中 | USB |
| 6 | テンプレート共通化 | Flash 30% | 中 | 全体 |
| 7 | TimerQueue O(1) | CPU 15-20% | 中 | カーネル |
| 8 | CRTP最適化 | CPU 10% | 高 | USB |
| 9 | DMAリングバッファ | CPU 20-30% | 高 | USB |

---

## 期待される最終効果

| 指標 | 現状 | 目標 | 削減率 |
|------|------|------|--------|
| Flash | 41,776B | ~32,000B | 23% |
| RAM | 77,820B | ~72,000B | 7% |
| コンテキストスイッチ | ~150サイクル | ~120サイクル | 20% |
| オーディオレイテンシ | ~500us | ~400us | 20% |

---

## 検証方法

1. **サイズ検証**: `arm-none-eabi-size` での継続的監視
2. **パフォーマンス検証**: DWT サイクルカウンタによる測定
3. **回帰テスト**: 既存機能の動作確認
4. **ハードウェアテスト**: STM32F4 Discovery での実機確認

---

## 参考資料

- ARM Cortex-M4 Technical Reference Manual
- STM32F4 Reference Manual (RM0090)
- USB 2.0 Specification
- "Making Embedded Systems" by Elecia White
