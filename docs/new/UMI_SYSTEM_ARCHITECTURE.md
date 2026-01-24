# UMI-OS システムアーキテクチャ詳細ドキュメント

## 1. 概要

UMI-OSは組み込みオーディオ/MIDI処理のためのリアルタイムオペレーティングシステムです。

### 1.1 システム構成図

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           Web Browser                                     │
│  ┌─────────────┐  ┌──────────────────────────────────────────────────┐  │
│  │ Shell UI    │  │ HardwareBackend (Web MIDI API)                   │  │
│  │ (custom)    │  │ - SysEx encode/decode (protocol.js)              │  │
│  └─────────────┘  └──────────────────────────────────────────────────┘  │
└────────────────────────────────┬─────────────────────────────────────────┘
                                 │ USB MIDI (SysEx)
                                 ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                    STM32F4-Discovery Kernel                               │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    Interrupt Handlers                               │  │
│  │  DMA1_Stream3 (PDM)  │  DMA1_Stream5 (I2S)  │  OTG_FS (USB)        │  │
│  │  SysTick (1ms)       │  SVC (Syscall)       │  PendSV (Switch)     │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                    │                                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    RTOS Scheduler (4-Priority)                      │  │
│  ├────────────┬────────────┬──────────────────┬───────────────────────┤  │
│  │ Priority 0 │ Priority 1 │    Priority 2    │      Priority 3       │  │
│  │ (Realtime) │  (Server)  │      (User)      │        (Idle)         │  │
│  ├────────────┼────────────┼──────────────────┼───────────────────────┤  │
│  │ Audio Task │System Task │  Control Task    │     Idle Task         │  │
│  │            │            │                  │                       │  │
│  │ - DMA通知  │ - SysEx    │  - App main()    │  - WFI (sleep)        │  │
│  │ - Audio処理│ - Shell    │  - Syscall処理   │                       │  │
│  │ - USB IN   │            │                  │                       │  │
│  └────────────┴────────────┴──────────────────┴───────────────────────┘  │
│                                    │                                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    Shared Memory (16KB)                             │  │
│  │  sample_rate  │  dt  │  sample_position  │  event_queue  │  params │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                    │                                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    Application (.umiapp)                            │  │
│  │  - Unprivileged mode (Thread/PSP)                                   │  │
│  │  - Processor登録 → オーディオコールバック                           │  │
│  └────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 2. STM32F4カーネル詳細

### 2.1 メモリレイアウト

```
Flash (1MB):
  0x08000000 ┬─────────────────────┐
             │ Kernel (.text)      │ 384KB
  0x08060000 ├─────────────────────┤
             │ App Image (.umiapp) │ 128KB
  0x080A0000 ├─────────────────────┤
             │ Reserved            │
  0x08100000 └─────────────────────┘

SRAM (128KB):
  0x20000000 ┬─────────────────────┐
             │ Kernel Data/BSS     │ 48KB
  0x2000C000 ├─────────────────────┤
             │ App RAM             │ 48KB
  0x20018000 ├─────────────────────┤
             │ Shared Memory       │ 16KB
  0x2001C000 ├─────────────────────┤
             │ DMA Buffers         │ 16KB
  0x20020000 └─────────────────────┘

CCM (64KB - DMAアクセス不可):
  0x10000000 ┬─────────────────────┐
             │ Audio Task Stack    │ 4KB
             │ System Task Stack   │ 2KB
             │ Control Task Stack  │ 8KB
             │ Idle Task Stack     │ 256B
             │ Debug Buffers       │ 残り
  0x10010000 └─────────────────────┘
```

### 2.2 タスク構成

| タスク | 優先度 | スタック | FPUポリシー | 役割 |
|--------|--------|----------|-------------|------|
| Audio Task | 0 (Realtime) | 4KB | Exclusive | DMA割り込み処理、オーディオバッファ処理、USB Audio IN送信 |
| System Task | 1 (Server) | 2KB | Forbidden | SysEx受信、シェルコマンド処理、USB MIDIハンドリング |
| Control Task | 2 (User) | 8KB | Forbidden | アプリケーション実行、Syscall処理、イベントディスパッチ |
| Idle Task | 3 (Idle) | 256B | Forbidden | WFI（Wait For Interrupt）によるスリープ |

### 2.3 カーネル最適化

#### 2.3.1 O(1) ビットマップスケジューラ

従来のO(n)線形探索スケジューラから、O(1)ビットマップベースに最適化:

```cpp
// 優先度ビットマップ（4ビット = 4優先度レベル）
std::atomic<uint8_t> ready_bitmap_ {0};

// 各優先度のタスクキュー
struct PriorityQueue {
    uint8_t head {0xFF};   // 先頭タスク
    uint8_t tail {0xFF};   // 末尾タスク
    uint8_t count {0};     // キュー内タスク数
};
std::array<PriorityQueue, 4> priority_queues_ {};

// O(1) タスク選択
std::optional<uint16_t> get_next_task() {
    uint8_t bitmap = ready_bitmap_.load(std::memory_order_acquire);
    if (bitmap == 0) return std::nullopt;

    // CTZ (Count Trailing Zeros) で最高優先度を1命令で取得
    uint8_t priority = __builtin_ctz(bitmap);
    return priority_queues_[priority].head;
}
```

**効果**: スケジューラ選択 ~50サイクル → ~5サイクル

#### 2.3.2 FPUポリシー

FPUコンテキストスイッチの変動を排除:

```cpp
enum class FpuPolicy : uint8_t {
    Forbidden = 0,  // FPU使用禁止（Shell, MIDI, UI）
    Exclusive = 1,  // FPU独占（Audio DSPタスク）
    LazyStack = 2,  // 従来の遅延保存（互換性用）
};
```

| ポリシー | 用途 | コンテキストスイッチ時のFPU処理 |
|---------|------|-------------------------------|
| Forbidden | 非FPUタスク | なし（0サイクル） |
| Exclusive | オーディオDSP | なし（独占所有） |
| LazyStack | 複数FPUタスク | ハードウェア遅延保存 |

**Exclusiveモード**: Audio Taskが FPU を独占所有。他タスクは Forbidden のため、コンテキストスイッチ時の FPU save/restore が完全に不要。

**効果**: FPUスイッチ ~100サイクル → 0サイクル（Exclusiveモード）

#### 2.3.3 Tickless電力管理

アイドル時の消費電力を最小化:

```cpp
enum class SleepMode : uint8_t {
    WFI,      // Wait For Interrupt (~1us wakeup)
    Stop,     // Stop mode (~5us wakeup, 低消費電力)
};

// スリープモード自動選択
SleepMode recommend_sleep_mode(uint64_t next_wakeup, uint64_t now, bool audio_active) {
    if (audio_active) return SleepMode::WFI;  // オーディオ中は軽いスリープ
    if (next_wakeup - now > 100) return SleepMode::Stop;  // 100us以上ならStop
    return SleepMode::WFI;
}
```

**効果**: アイドル時消費電力 ~50-80%削減（Stop mode使用時）

#### 2.3.4 MPU抽象化レイヤー

MPU有無に関わらず同一APIで動作:

```cpp
enum class ProtectionMode : uint8_t {
    Full,            // MPU有効、非特権モード（本番）
    Privileged,      // MPU無効、特権モード（MPUなしMCU）
    PrivilegedWithMpu, // MPU有効、特権モード（デバッグ）
};

// コンパイル時選択
template <class HW, ProtectionMode Mode = ProtectionMode::Full>
class Protection {
    static constexpr bool uses_mpu() { return Mode != ProtectionMode::Privileged; }
    static constexpr bool needs_syscall() { return Mode == ProtectionMode::Full; }
};
```

**効果**: MPUなしMCU（STM32F0等）でも同一アプリが動作

#### 2.3.5 パフォーマンス計測

DWT (Data Watchpoint and Trace) を使用したサイクル精度計測:

```cpp
#define UMI_ENABLE_METRICS

struct KernelMetrics {
    struct ContextSwitch {
        uint32_t count;
        uint32_t cycles_min, cycles_max;
        uint64_t cycles_sum;
    } context_switch;

    struct Audio {
        uint32_t cycles_last, cycles_max;
        uint32_t overruns, underruns;
    } audio;

    // ...
};
```

シェルコマンド `show cpu` で確認可能。

### 2.4 割り込みハンドラ

```cpp
// Audio DMA (優先度5 - 高)
extern "C" void DMA1_Stream3_IRQHandler();  // PDM Microphone
extern "C" void DMA1_Stream5_IRQHandler();  // I2S Audio OUT

// USB (優先度6)
extern "C" void OTG_FS_IRQHandler();        // USB Full-Speed

// System (優先度最低)
extern "C" void SysTick_Handler();          // 1ms周期タイマー
extern "C" void SVC_Handler();              // Syscall (SVC #0)
extern "C" void PendSV_Handler();           // Context Switch
```

**割り込み優先度設計:**
```
優先度 5: Audio DMA (I2S, PDM) - 最高優先度
優先度 6: USB OTG FS
優先度 F0: SysTick
優先度 FF: PendSV - 最低優先度（Context Switch用）
```

**クリティカルセクション:**
- BASEPRI方式を採用（PRIMASK全禁止ではなく優先度ベース）
- Audio DMA割り込みはクリティカルセクション中も実行可能

### 2.5 Syscall一覧

| Nr | 名前 | 引数 | 説明 |
|----|------|------|------|
| 0 | Exit | code | アプリケーション終了 |
| 1 | RegisterProc | proc_ptr | オーディオプロセッサ登録 |
| 2 | WaitEvent | mask, timeout_us | イベント待機（ブロッキング） |
| 3 | SendEvent | bits | カーネルへイベント送信 |
| 4 | PeekEvent | mask | イベント確認（ノンブロッキング） |
| 5 | Yield | - | CPU制御を自発的に手放す |
| 10 | GetTime | - | 現在時刻（マイクロ秒）取得 |
| 11 | Sleep | usec | 指定時間スリープ |
| 20 | Log | msg, len | デバッグログ出力 |
| 21 | Panic | msg | パニック（致命的エラー） |
| 30 | GetParam | index | パラメータ値取得 |
| 31 | SetParam | index, value | パラメータ値設定 |
| 40 | GetShared | - | 共有メモリポインタ取得 |
| 50 | MidiSend | data, len | MIDI送信 |
| 51 | MidiRecv | buf, maxlen | MIDI受信 |
| 60 | SetLed | index, state | LED制御 |
| 61 | GetLed | - | LED状態取得 |
| 62 | GetButton | - | ボタン状態取得 |

### 2.6 イベントフラグ

```cpp
namespace umi::syscall::event {
    constexpr uint32_t Audio    = (1 << 0);   // オーディオバッファ準備完了
    constexpr uint32_t Midi     = (1 << 1);   // MIDIデータ利用可能
    constexpr uint32_t VSync    = (1 << 2);   // 画面リフレッシュ（将来用）
    constexpr uint32_t Timer    = (1 << 3);   // タイマーティック
    constexpr uint32_t Button   = (1 << 4);   // ボタン押下
    constexpr uint32_t Shutdown = (1 << 31);  // シャットダウン要求
}
```

### 2.7 初期化シーケンス

```
Reset_Handler()
    │
    ├── 1. .data セクション初期化（Flash → RAM）
    ├── 2. .bss セクション初期化（ゼロクリア）
    ├── 3. グローバルコンストラクタ呼び出し
    │
    ├── init_gpio()
    │   ├── LED (PD12-15)
    │   ├── User Button (PA0)
    │   ├── I2C1 (PB6, PB9)
    │   ├── I2S3 (PC7, PC10, PC12, PA4)
    │   ├── SPI2/PDM (PB10, PC3)
    │   └── USB OTG FS (PA11, PA12)
    │
    ├── init_audio()
    │   ├── I2C1 初期化
    │   ├── CS43L22 Codec 初期化（24-bit mode）
    │   ├── PLLI2S 設定（47,991Hz）
    │   ├── I2S3 初期化
    │   └── DMA1_Stream5 設定
    │
    ├── init_pdm_mic()
    │   ├── SPI2/I2S 初期化（PDM mode）
    │   ├── CIC Decimator 初期化（64x）
    │   └── DMA1_Stream3 設定
    │
    ├── init_usb()
    │   ├── USB OTG FS 初期化
    │   ├── Audio Interface 設定
    │   ├── MIDI Callback 設定
    │   └── SysEx Callback 設定
    │
    ├── init_systick()
    │   └── 1ms (168MHz / 168000)
    │
    ├── App Loading
    │   ├── AppHeader 検証
    │   ├── CRC32 チェック
    │   └── メモリマッピング
    │
    ├── Task Context 初期化
    │   ├── audio_task_entry
    │   ├── system_task_entry
    │   ├── control_task_entry
    │   └── idle_task_entry
    │
    └── Start RTOS
        └── control_task_entry() 実行開始
```

---

## 3. Shared Memory 構造

```cpp
struct SharedMemory {
    // ===== Audio Buffers (定義済み、現在未使用) =====
    static constexpr size_t AUDIO_BUFFER_FRAMES = 256;
    static constexpr size_t AUDIO_CHANNELS = 2;

    float audio_input[512];      // USB Audio OUT → App (将来用)
    float audio_output[512];     // App → USB Audio IN Right (将来用)
    float mic_input[256];        // PDM Mic → USB Audio IN Left (将来用)

    // ※ 現在の実装では直接バッファ操作 (i2s_work_buf, synth_out_mono 等)

    // ===== Audio Context =====
    uint32_t sample_rate;        // 48000 (actual: 47991)
    uint32_t buffer_size;        // 64 frames (実際の値)
    float dt;                    // 1.0f / sample_rate (事前計算)
    uint64_t sample_position;    // 累積サンプル数

    // ===== Event Queue (Lock-free SPSC) =====
    static constexpr size_t EVENT_QUEUE_SIZE = 64;
    Event event_queue[64];
    std::atomic<uint32_t> event_write_idx;
    std::atomic<uint32_t> event_read_idx;

    // ===== Parameters =====
    static constexpr size_t MAX_PARAMS = 32;
    std::atomic<float> params[32];

    // ===== LED/Button State =====
    std::atomic<uint8_t> led_state;       // bit0-3 = LED0-3
    std::atomic<uint8_t> button_pressed;  // 消費型フラグ
    std::atomic<uint8_t> button_current;  // 現在状態

    // ===== Methods =====
    bool push_event(const Event& ev);
    bool pop_event(Event& ev);
    void set_sample_rate(uint32_t rate);  // sample_rate と dt を更新
};
```

---

## 4. アプリケーション構成

### 4.1 AppHeader フォーマット (128 bytes)

```cpp
struct AppHeader {
    // Identification (16 bytes)
    uint32_t magic;              // 0x414D4955 ("UMIA")
    uint32_t abi_version;        // ABI バージョン (1)
    AppTarget target;            // User/Development/Release
    uint32_t flags;              // 予約

    // Entry Points (8 bytes)
    uint32_t entry_offset;       // _start() オフセット
    uint32_t process_offset;     // process() オフセット (optional)

    // Section Sizes (16 bytes)
    uint32_t text_size;          // .text セクション
    uint32_t rodata_size;        // .rodata セクション
    uint32_t data_size;          // .data セクション
    uint32_t bss_size;           // .bss セクション

    // Memory Requirements (8 bytes)
    uint32_t stack_size;         // 必要スタックサイズ
    uint32_t heap_size;          // 必要ヒープサイズ

    // Integrity (8 bytes)
    uint32_t crc32;              // IEEE 802.3 CRC
    uint32_t total_size;         // 全体サイズ

    // Signature (64 bytes)
    uint8_t signature[64];       // Ed25519署名（Release版）

    // Reserved (8 bytes)
    uint8_t reserved[8];
};
```

### 4.2 crt0.cc スタートアップ

```cpp
extern "C" void _start() {
    // 1. .data セクション初期化
    extern uint32_t _sidata, _sdata, _edata;
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    // 2. .bss セクション初期化
    extern uint32_t _sbss, _ebss;
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    // 3. コンストラクタ呼び出し
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn)
        (*fn)();

    // 4. main() 呼び出し
    extern int main();
    main();

    // main()から戻った場合、カーネルに制御が戻る
}
```

### 4.3 アプリケーション例

```cpp
#include <umi_app.hh>
#include <umios/app/syscall.hh>

// ProcessorLike concept (継承不要)
struct MySynth {
    float phase = 0.0f;

    void process(umi::AudioContext& ctx) {
        auto* out_l = ctx.output(0);
        auto* out_r = ctx.output(1);

        // MIDIイベント処理
        for (const auto& ev : ctx.input_events) {
            if (ev.type == umi::EventType::Midi) {
                // handle MIDI
            }
        }

        // オーディオ生成
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float sample = generate_sample();
            out_l[i] = sample;
            if (out_r) out_r[i] = sample;
        }
    }
};

static MySynth g_synth;

int main() {
    umi::register_processor(g_synth);

    // main()から戻るとカーネルに制御が戻る
    // コルーチン使用時はScheduler.run()でブロック
    return 0;
}
```

---

## 5. Web構成

### 5.1 ディレクトリ構成

```
examples/headless_webhost/web/
├── lib/umi_web/
│   ├── index.js               # メインエクスポート
│   ├── midi_shell_bridge.js   # MIDI-Shell接続
│   │
│   ├── core/
│   │   ├── index.js           # モジュールエクスポート
│   │   ├── backend.js         # BackendInterface基底クラス
│   │   ├── manager.js         # BackendManager
│   │   ├── protocol.js        # SysExエンコーディング
│   │   └── backends/
│   │       ├── hardware.js    # WebMIDI（実機）
│   │       ├── umim.js        # UMIM/WASMバックエンド
│   │       ├── umios.js       # UMI-OS直接制御
│   │       └── renode.js      # Renodeシミュレータ
│   │
│   ├── components/
│   │   ├── index.js           # コンポーネントエクスポート
│   │   ├── shell/             # シェルUI（カスタム実装）
│   │   ├── keyboard/          # 仮想MIDIキーボード
│   │   ├── param-control/     # パラメータスライダー
│   │   ├── waveform/          # 波形表示
│   │   ├── midi-monitor/      # MIDIモニタ
│   │   ├── device-selector/   # デバイス選択（MIDI/Audio）
│   │   └── backend-selector/  # バックエンド選択
│   │
│   └── theme/
│       └── index.js           # テーマ設定
│
└── index.html
```

### 5.2 SysExプロトコル (protocol.js)

**メッセージフォーマット:**
```
F0              SysEx Start
7E 7F 00        UMI Manufacturer ID
CMD             Command byte
SEQ             Sequence number (0-127)
[PAYLOAD...]    7-bit encoded payload
CHECKSUM        XOR checksum (7-bit)
F7              SysEx End
```

**コマンド定義:**
```javascript
const Command = {
    // Standard IO (0x01-0x0F)
    STDOUT_DATA: 0x01,    // シェル出力
    STDERR_DATA: 0x02,    // エラー出力
    STDIN_DATA:  0x03,    // シェル入力
    STDIN_EOF:   0x04,    // 入力終了
    FLOW_CTRL:   0x05,    // フロー制御

    // Firmware Update (0x10-0x1F)
    FW_QUERY:    0x10,
    FW_INFO:     0x11,
    FW_BEGIN:    0x12,
    FW_DATA:     0x13,
    FW_VERIFY:   0x14,
    FW_COMMIT:   0x15,
    FW_ROLLBACK: 0x16,
    FW_REBOOT:   0x17,
    FW_ACK:      0x18,
    FW_NACK:     0x19,

    // System (0x20-0x2F)
    PING:        0x20,
    PONG:        0x21,
    RESET:       0x22,
    VERSION:     0x23,
};
```

**7-bitエンコーディング:**
```javascript
// 7バイト入力 → 8バイト出力
// MSBバイト + 7データバイト（上位ビットクリア）
function encode7bit(data) {
    const result = [];
    let i = 0;
    while (i < data.length) {
        let msbByte = 0;
        const chunk = [];
        for (let j = 0; j < 7 && i < data.length; j++) {
            const byte = data[i++];
            if (byte & 0x80) msbByte |= (1 << j);
            chunk.push(byte & 0x7F);
        }
        result.push(msbByte);
        result.push(...chunk);
    }
    return result;
}
```

### 5.3 HardwareBackend (hardware.js)

```javascript
class HardwareBackend extends BackendInterface {
    async start() {
        // Web MIDI API でデバイス接続
        this.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
        const devices = await this.getDevices();
        return this.connectDevice(devices[0].name);
    }

    sendShellCommand(text) {
        const data = new TextEncoder().encode(text + '\r');
        const msg = buildMessage(Command.STDIN_DATA, this.txSequence++, data);
        this.output.send(msg);
    }

    _handleSysEx(msg) {
        switch (msg.command) {
            case Command.STDOUT_DATA:
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                this.onShellOutput?.(text, 'stdout');
                break;
            // ...
        }
    }
}
```

---

## 6. シェルシステム

### 6.1 コマンド一覧

**基本コマンド:**
| コマンド | 説明 |
|----------|------|
| `help` | ヘルプ表示 |
| `version` | バージョン表示 |
| `uptime` | 稼働時間表示 |
| `whoami` | アクセスレベル表示 |
| `auth <level> <pw>` | 認証 |
| `logout` | ログアウト |

**Showコマンド:**
| コマンド | 説明 |
|----------|------|
| `show system` | システム概要 |
| `show cpu` | CPU負荷 |
| `show memory` | メモリ使用量 |
| `show tasks` | タスクリスト |
| `show audio` | オーディオ状態 |
| `show midi` | MIDI状態 |
| `show battery` | バッテリー状態 |
| `show power` | 電源管理 |
| `show usb` | USB状態 |
| `show errors` | エラーログ |
| `show config` | 現在設定 |

**管理者コマンド (ADMIN):**
| コマンド | 説明 |
|----------|------|
| `config midi channel <1-16>` | MIDIチャンネル設定 |
| `config midi transpose <-24..24>` | トランスポーズ設定 |
| `config audio gain <0-100>` | オーディオゲイン設定 |
| `config power sleep <min>` | スリープタイムアウト設定 |
| `diag watchdog [feed\|enable\|disable]` | ウォッチドッグ制御 |
| `diag reset` | システムリセット |

**工場コマンド (FACTORY):**
| コマンド | 説明 |
|----------|------|
| `factory info` | 工場情報表示 |
| `factory serial set <sn>` | シリアル番号設定 |
| `factory test` | 工場テスト実行 |
| `factory lock` | 工場ロック（不可逆） |

### 6.2 認証システム

```cpp
enum class AccessLevel : uint8_t {
    User = 0,      // 基本コマンドのみ
    Admin = 1,     // 設定変更可能
    Factory = 2,   // 全機能アクセス
};

// デフォルトパスワード（開発用）
// Admin: "admin"
// Factory: "factory"

// セッションタイムアウト: 5分
// ロックアウト: 3回失敗で30秒
```

### 6.3 StateProvider インターフェース

```cpp
/// ShellCommandsが要求するインターフェース
/// 各プラットフォームで実装が必要
struct StateProvider {
    KernelStateView& state();       // カーネル状態
    ShellConfig& config();          // シェル設定
    ErrorLog<16>& error_log();      // エラーログ
    SystemMode& system_mode();      // システムモード
    void reset_system();            // システムリセット
    void feed_watchdog();           // ウォッチドッグフィード
    void enable_watchdog(bool);     // ウォッチドッグ有効/無効
};
```

---

## 7. オーディオシステム

### 7.1 オーディオフロー（実装）

**2つの独立した系統**で構成されています：

#### 系統1: USB Audio OUT → I2S DAC（パススルー再生）

```
┌─────────────────────────────────────────────────────────────────┐
│                    USB Audio OUT (Host → Device)                 │
│                    48kHz / 24-bit Stereo                         │
│                    ホストPCからの再生音                           │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              umiusb::UsbAudioDevice Ring Buffer                  │
│              usb_audio.read_audio(i2s_work_buf, 64)              │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              i2s_work_buf[128] int32_t (64 frames × 2ch)         │
│              pack_i2s_24() で24bit変換                           │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              I2S DAC (CS43L22) - ヘッドホン出力                   │
│              DMA Double-Buffer: audio_buf0/audio_buf1            │
│              47,991Hz / 24-bit Stereo                            │
└─────────────────────────────────────────────────────────────────┘
```

#### 系統2: App + Mic → USB Audio IN（デバイス→ホスト送信）

```
┌─────────────────────────────────────────────────────────────────┐
│              MIDI IN (USB MIDI)                                  │
│              Note On/Off, CC, etc.                               │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              Application Processor                               │
│              process(AudioContext& ctx)                          │
│              - ctx.input_events() でMIDI受信                     │
│              - ctx.output() へシンセ出力                         │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              synth_out_mono[64] → last_synth_out[128]            │
│              float → int16_t 変換                                │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          │
┌─────────────────────────┴───────────────────────────────────────┐
│              PDM Microphone                                      │
│              SPI2/I2S → DMA → CIC 64x Decimation                 │
│              pcm_buf[64] int16_t @ 47,991Hz                      │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              stereo_buf[128] int16_t                             │
│              L: pcm_buf (Microphone)                             │
│              R: last_synth_out (App Synth)                       │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              USB Audio IN (Device → Host)                        │
│              Left: Mic, Right: Synth                             │
│              DAWで録音・モニタリング可能                          │
└─────────────────────────────────────────────────────────────────┘
```

**Note:**
- 2つの系統は**完全に独立**（USB OUT の音声はアプリに渡されない）
- アプリの入力は**MIDI**のみ（シンセサイザー用途）
- SharedMemory.audio_input/output は定義済みだが現在未使用

### 7.2 サンプルレート

```
目標: 48,000 Hz
実際: 47,991 Hz (PLLI2S制限)

PLLI2S設定:
  PLLI2SN = 258
  PLLI2SR = 3
  I2SDIV = 3
  ODD = 1

計算:
  PLLI2SCLK = 1MHz × 258 / 3 = 86 MHz
  Fs = 86MHz / (256 × (2×3 + 1)) = 47,991 Hz
```

### 7.3 DMAダブルバッファリング

```cpp
// I2S DMA (64フレーム × 4ワード = 256ワード)
uint16_t audio_buf0[256];  // DMA Buffer 0
uint16_t audio_buf1[256];  // DMA Buffer 1

// PDM DMA (256ワード)
uint16_t pdm_buf0[256];    // DMA Buffer 0
uint16_t pdm_buf1[256];    // DMA Buffer 1

// 処理フロー:
// 1. DMA完了割り込み発生
// 2. Audio Taskに通知
// 3. Audio Taskが処理（別バッファを使用中）
// 4. 処理完了、次のバッファを待機
```

---

## 8. デバッグ機能

### 8.1 デバッグカウンタ（GDB経由で参照）

```cpp
// Audio Debug
uint32_t dbg_i2s_isr_count;        // I2S DMA割り込み回数
uint32_t dbg_fill_audio_count;     // オーディオ処理回数
uint32_t dbg_underrun;             // アンダーラン回数
uint32_t dbg_overrun;              // オーバーラン回数

// USB Debug
uint32_t dbg_usb_rx_count;         // USB Audio OUT受信回数
uint32_t dbg_feedback;             // Feedback値 (10.14形式)
uint32_t dbg_out_buf_level;        // 出力バッファレベル

// SysEx Debug
uint32_t dbg_sysex_rx_count;       // SysEx受信回数
uint32_t dbg_sysex_dropped;        // SysExドロップ回数
uint32_t dbg_sysex_processed;      // SysEx処理回数

// Glitch Detection
uint32_t dbg_glitch_count;         // グリッチ検出回数
int32_t dbg_glitch_window[256];    // キャプチャバッファ
```

### 8.2 GDBコマンド例

```bash
# PyOCD GDBサーバー起動
pyocd gdbserver --target stm32f407vg

# GDB接続
arm-none-eabi-gdb build/stm32f4_kernel/release/stm32f4_kernel.elf
(gdb) target remote localhost:3333

# デバッグカウンタ確認
(gdb) p dbg_underrun
(gdb) p dbg_usb_rx_count
(gdb) p dbg_sysex_rx_count

# バッファダンプ
(gdb) x/64x dbg_glitch_window
```

---

## 9. ビルドとデプロイ

### 9.1 ビルドコマンド

```bash
# カーネルビルド
xmake build stm32f4_kernel

# アプリケーションビルド
xmake build synth_app

# 全ターゲットビルド
xmake build
```

### 9.2 フラッシュ書き込み

```bash
# PyOCD経由
pyocd flash --target stm32f407vg build/stm32f4_kernel/release/stm32f4_kernel.bin

# OpenOCD経由
openocd -f board/stm32f4discovery.cfg \
    -c "program build/stm32f4_kernel/release/stm32f4_kernel.bin verify reset exit"
```

### 9.3 Web UI起動

```bash
cd examples/headless_webhost
npm install
npm run dev
# ブラウザで http://localhost:5173 を開く
```

---

## 10. ファイル一覧

### 10.1 カーネル

| ファイル | 説明 |
|----------|------|
| `examples/stm32f4_kernel/src/main.cc` | カーネルメイン |
| `examples/stm32f4_kernel/kernel.ld` | リンカスクリプト |
| `lib/umios/kernel/umi_kernel.hh` | カーネルコア（O(1)スケジューラ、FPUポリシー含む） |
| `lib/umios/kernel/shared_memory.hh` | 共有メモリ |
| `lib/umios/kernel/shell_commands.hh` | シェルコマンド |
| `lib/umios/kernel/loader.hh` | アプリローダー |
| `lib/umios/kernel/protection.hh` | MPU抽象化レイヤー |
| `lib/umios/kernel/metrics.hh` | パフォーマンス計測（DWT） |
| `lib/umios/kernel/port/cm4/switch.hh` | Cortex-M4 コンテキストスイッチ |
| `lib/umios/kernel/port/cm4/context.hh` | Cortex-M4 コンテキスト管理 |

### 10.2 アプリケーション

| ファイル | 説明 |
|----------|------|
| `lib/umios/app/syscall.hh` | Syscall API |
| `lib/umios/app/crt0.cc` | スタートアップ |
| `lib/umios/app/app_header.hh` | ヘッダーフォーマット |

### 10.3 Web

| ファイル | 説明 |
|----------|------|
| `examples/headless_webhost/web/lib/umi_web/core/protocol.js` | SysExプロトコル |
| `examples/headless_webhost/web/lib/umi_web/core/backends/hardware.js` | WebMIDIバックエンド |
| `examples/headless_webhost/web/lib/umi_web/components/shell/index.js` | シェルUI |

### 10.4 プラットフォームドライバ

| ファイル | 説明 |
|----------|------|
| `lib/umios/backend/cm/stm32f4/gpio.hh` | GPIO |
| `lib/umios/backend/cm/stm32f4/i2s.hh` | I2S Audio |
| `lib/umios/backend/cm/stm32f4/i2c.hh` | I2C |
| `lib/umios/backend/cm/stm32f4/cs43l22.hh` | Audio Codec |
| `lib/umios/backend/cm/stm32f4/pdm_mic.hh` | PDM Microphone |
| `lib/umios/backend/cm/stm32f4/rcc.hh` | クロック制御 |
| `lib/umios/backend/cm/stm32f4/power.hh` | 電力管理・Tickless |
| `lib/umiusb/include/audio_interface.hh` | USB Audio |

---

## 11. ビルドサイズ

### 11.1 stm32f4_kernel

```
Flash: 41,776 / 1,048,576 bytes (4.0%)
RAM:   77,820 / 131,072 bytes (59.4%)
```

| セクション | サイズ | 説明 |
|-----------|--------|------|
| .text | 41,692 B | コード（Flash） |
| .data | 84 B | 初期化済みデータ |
| .bss | 77,736 B | 未初期化データ（RAM） |

### 11.2 主要RAM消費

| シンボル | サイズ | 説明 |
|---------|--------|------|
| usb_audio | 28,320 B | USB Audio Class バッファ |
| g_shared | 6,824 B | 共有メモリ領域 |
| g_app_event_queue | 6,152 B | アプリイベントキュー |
| usb_hal | 3,032 B | USB HAL状態 |
| タスクスタック | 14,336 B | CCM RAM（4K+2K+8K） |

カーネル自体はヘッダーオンリー（テンプレート）でインライン展開されるため、
独立したサイズは計測困難。主要関数：
- PendSV_Handler: ~96 bytes
- umi_cm4_switch_context: ~192 bytes

---

*Document Version: 1.1*
*Last Updated: 2025-01-25*
