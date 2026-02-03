# 04 — ISR 軽量化と外部処理の疎結合設計

> 元ドキュメント: [SPEED_SUPPORT_DESIGN.md](../SPEED_SUPPORT_DESIGN.md) セクション 15-16

## 15. ISR 処理量の分析と軽量化設計

### 15-1. 現状: ISR 内で行われている処理

USB IRQ ハンドラ (`poll()`) から呼ばれる処理の実測的な重さを分析する。

#### on_rx (Audio OUT パケット受信) — **最も重い**

```
1. デバッグカウンタ更新 (~15 個)            ← 不要な負荷
2. discard/block チェック
3. デバッグ: パケット生バイト記録           ← 不要な負荷
4. ビット深度変換ループ (16bit or 24bit)    ← 必須だが重い
   - 96kHz stereo 24bit: 97 frames × 2ch = 194 サンプル変換
5. デバッグ: 不連続検出ループ              ← 不要な負荷
6. RingBuffer::write() (memcpy)            ← 必須
7. デバッグ: バッファレベル統計             ← 不要な負荷
8. コールバック呼び出し (on_audio_rx 等)
```

**問題:**
- デバッグコードが `#ifdef` ではなくランタイム変数 (`dbg_out_rx_enabled_`) で制御されている
- デバッグ無効時でもカウンタ更新 (~15 回の volatile 書込み) は常に実行
- 不連続検出ループ (行 1993-2080) が全パケットで実行される
- ビット深度変換がISR内。96kHz/24bit/stereo で **約 600 バイトのデータを1サンプルずつ変換**

#### on_sof (SOF ハンドラ) — **中程度**

```
1. デバッグカウンタ更新
2. フィードバック計算 (PI 制御: 乗算 + 加算)  ← 軽い
3. フィードバック送信 (ep_write 3 bytes)       ← 軽い
4. on_sof_app コールバック                     ← アプリ依存 (潜在的に重い)
5. send_audio_in_now()                         ← RingBuffer 読出 + パック + ep_write
```

`send_audio_in_now()` は RingBuffer から読み出し + ビット深度変換 + USB パケット構築を
ISR 内で実行している。96kHz/24bit/stereo で約 600 バイトのパケット構築。

#### handle_request (SETUP リクエスト) — **軽い**

制御転送のみ。データ量が小さく、頻度も低い。問題なし。

### 15-2. ISR 負荷の内訳推定 (96kHz/24bit/stereo, Cortex-M4)

| 処理 | 頻度 | 推定サイクル | 備考 |
|------|------|-------------|------|
| on_rx デバッグカウンタ | 1kHz | ~200 | volatile 書込み 15 回 |
| on_rx ビット深度変換 | 1kHz | ~2000 | 194 サンプル × ~10 cycle |
| on_rx 不連続検出 | 1kHz | ~1500 | 194 サンプルのペアワイズ比較 |
| on_rx RingBuffer write | 1kHz | ~500 | memcpy ~800 bytes |
| on_sof フィードバック | 1kHz | ~100 | PI 制御 + 3 byte 書込 |
| on_sof Audio IN 送信 | 1kHz | ~2500 | RingBuffer read + pack + write |
| **合計** | | **~6800** | **168MHz で約 40μs** |

1ms フレームに対して 40μs は **4% の CPU 時間**。許容範囲だが改善の余地がある。
特にデバッグコードが約 1700 サイクル (25%) を占めている。

### 15-3. 改善案

#### A. デバッグコードの条件コンパイル化

現状: ランタイム変数でデバッグを制御 → 無効時もカウンタ更新は実行

```cpp
// 現状 — 常に実行される
++dbg_on_rx_called_;
dbg_on_rx_last_ep_ = ep;
dbg_on_rx_last_len_ = static_cast<uint32_t>(data.size());
```

改善案: テンプレートパラメータでデバッグレベルを制御

```cpp
enum class DebugLevel : uint8_t {
    NONE = 0,     // デバッグコード完全除去
    MINIMAL = 1,  // カウンタのみ
    FULL = 2,     // 不連続検出、パケットダンプ含む
};

template <..., DebugLevel DbgLevel = DebugLevel::NONE>
class AudioInterface {
    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if constexpr (DbgLevel >= DebugLevel::MINIMAL) {
            ++dbg_on_rx_called_;
        }
        // ...
        if constexpr (DbgLevel >= DebugLevel::FULL) {
            // 不連続検出ループ — プロダクションでは除去
        }
    }
};
```

**効果:** `DebugLevel::NONE` 時、ISR 負荷が約 25% 削減。

#### B. ビット深度変換の遅延実行

現状: ISR 内で USB パケット → SampleT 変換 → RingBuffer 書込
改善案: USB 生バイトを RingBuffer に書込み、変換は DMA コールバック (read 時) で実行

```
現状:
  ISR:  USB packet → decode (heavy) → RingBuffer<SampleT>
  DMA:  RingBuffer<SampleT> → read → I2S pack

改善案:
  ISR:  USB packet → RingBuffer<uint8_t> (memcpy only)
  DMA:  RingBuffer<uint8_t> → decode + read → I2S pack
```

**効果:**
- ISR からビット深度変換ループを除去 (~2000 サイクル削減)
- ISR は memcpy のみ (~500 サイクル)
- 変換コストは DMA コールバックに移動 (DMA は ISR より優先度が低い場合が多い)

**トレードオフ:**
- RingBuffer が `uint8_t` ベースになり、フレーム単位の管理が複雑化
- ASRC 補間が生バイトでは困難 (変換後の SampleT が必要)
- **現状の設計が妥当な理由**: ASRC を ISR-DMA 間で行うには SampleT が必要

**結論:** ASRC を使う場合、現状の ISR 内変換は必須。
ASRC を使わないモード (Adaptive/Sync) では遅延実行が有効。

#### C. send_audio_in_now の軽量化

現状: SOF (ISR) 内で RingBuffer read + ビット深度変換 + ep_write
改善案: DMA コールバック (on_sof_app) でパケットを事前構築し、SOF では ep_write のみ

```cpp
// DMA コールバック (on_sof_app) 内 — ISR より低優先度
void prepare_audio_in_packet() {
    // RingBuffer 読出 + ビット深度変換 → in_packet_buf_ に格納
    in_packet_ready_ = true;
}

// ISR (on_sof) 内 — 軽い
void on_sof(HalT& hal) {
    if (audio_in_streaming_ && in_packet_ready_) {
        hal.ep_write(EP_AUDIO_IN, in_packet_buf_, in_packet_len_);
        in_packet_ready_ = false;
    }
}
```

**効果:** ISR から ~2500 サイクルを DMA コールバックに移動。

#### D. ISR → タスクのディファード処理 (将来)

RTOS 環境では ISR 内処理を最小化し、タスクに委譲するのが一般的:

```
ISR:  USB packet → キューに push (数十サイクル)
Task: キューから pop → decode → RingBuffer write
```

これは umiusb 単体ではなく、umios のタスクシステムとの統合が必要。
現時点では ISR 内処理を維持し、デバッグコード除去と Audio IN パケット事前構築で対応する。

### 15-4. 改善後の ISR 負荷推定

| 処理 | 現状 (cycle) | 改善後 (cycle) | 削減 |
|------|-------------|---------------|------|
| on_rx デバッグ | ~1700 | 0 (`NONE`) | -1700 |
| on_rx 変換+write | ~2500 | ~2500 (維持) | 0 |
| on_sof フィードバック | ~100 | ~100 | 0 |
| on_sof Audio IN | ~2500 | ~100 (ep_write のみ) | -2400 |
| **合計** | **~6800** | **~2700** | **-60%** |

168MHz で約 16μs/frame。1ms フレームに対して **1.6%** の CPU 時間。

### 15-5. ドメイン再定義への影響

Audio IN パケット事前構築を導入すると、ドメイン分類が変わる:

| メソッド | 現在 | 改善後 |
|---------|------|--------|
| `send_audio_in_now(hal)` | ISR (on_sof 内自動) | ISR (ep_write のみ) |
| `prepare_audio_in_packet()` | なし | DMA (on_sof_app 内で呼ぶ) |
| `write_audio_in(buf, frames)` | DMA | DMA (変更なし) |

`on_sof_app` コールバックの役割が「データ供給タイミング通知」から
「データ供給 + パケット構築」に拡大する。

---

## 16. 外部処理の注入可能な疎結合設計

### 16-1. 問題: AudioInterface に埋め込まれた非 USB ロジック

現在の `AudioInterface` は USB プロトコル処理だけでなく、以下の DSP/クロック制御
ロジックを **内部に直接保持** している。これらは USB 自体のロジックではないが、
USB Audio の正常動作に必要な同期処理である。

| 埋め込みロジック | 場所 | 本来の所属 |
|----------------|------|-----------|
| **PllRateController** (PI 制御) | `audio_interface.hh:1272-1273` | umidsp (レート制御) |
| **ASRC レート平滑化** (IIR LPF) | `audio_interface.hh:1276-1285` | umidsp (フィルタ) |
| **update_asrc_rate()** (PI + LPF 統合) | `audio_interface.hh:1101-1112` | umidsp (ASRC) |
| **FeedbackCalculator** (10.14 形式) | `audio_interface.hh:1271` | USB Audio 仕様 (USB 寄り) |
| **ビット深度変換** (16/24bit ↔ SampleT) | `audio_interface.hh:1061-1076` | 汎用オーディオ処理 |
| **ボリューム制御** (dB → リニアゲイン) | `audio_interface.hh:2417-2461` | umidsp (ゲイン) |
| **Cubic Hermite 補間** | `audio_types.hh:382-459` (via RingBuffer) | umidsp (補間) |

**問題点:**

1. **テスト困難** — ASRC やボリューム制御を単体テストできない (USB スタック全体が必要)
2. **差し替え不可** — PI パラメータやフィルタ特性を変えるにはテンプレートパラメータを増やすしかない
3. **循環的依存** — バッファレベル → PI → ASRC レート → 補間読み出し → バッファレベル、の制御ループが AudioInterface 内部で閉じている
4. **再利用不可** — 同じ ASRC/フィルタを非 USB オーディオ (I2S→I2S ブリッジ等) で使えない

### 16-2. 設計方針: Strategy パターンによる注入

USB AudioInterface は以下の **接点 (seam)** で外部処理を注入可能にする。
注入は C++20 concept による静的ポリモーフィズムで実現し、vtable オーバーヘッドはゼロ。

#### 接点の分類

```
USB パケット
    │
    ▼
┌──────────────────────────────────────────────────────┐
│  AudioInterface (USB プロトコル層)                      │
│                                                        │
│  on_rx ─→ [SampleDecoder] ─→ RingBuffer ─→ on_read    │
│                                    │                    │
│  on_sof ─→ [FeedbackStrategy] ←───┘                    │
│                                                        │
│  read_audio ─→ RingBuffer ─→ [AsrcStrategy] ─→         │
│                              [GainProcessor] ─→ out    │
│                                                        │
│  write_audio_in ─→ RingBuffer ─→ [SampleEncoder] ─→    │
│                                   on_sof → ep_write    │
└──────────────────────────────────────────────────────┘
    │                    ▲
    ▼                    │
[AudioBridge]       [ClockSync]
 (DMA/I2S)          (PLL/クロック)
```

### 16-3. 注入ポイントと concept 定義

#### A. AsrcStrategy — レート変換の注入

現状は `PllRateController` + IIR LPF + `read_interpolated()` が一体化。
これを「レート決定」と「補間実行」に分離し、注入可能にする。

```cpp
/// ASRC レート決定 strategy
/// バッファレベルから再生レート比 (Q16.16) を算出する
template<typename T>
concept AsrcStrategy = requires(T& asrc, int32_t buffer_level) {
    // バッファレベルから再生レート比を更新・取得
    // 返値: Q16.16 形式 (0x10000 = 1.0x、つまり等速)
    { asrc.update(buffer_level) } -> std::convertible_to<uint32_t>;

    // リセット (サンプルレート変更時等)
    { asrc.reset() } -> std::same_as<void>;
};
```

**デフォルト実装** (現状の動作を再現):

```cpp
/// PI 制御 + IIR LPF によるデフォルト ASRC strategy
/// umidsp::PiRateController をラップ
struct PiLpfAsrc {
    umidsp::PiRateController pi;
    int64_t smoothed_q32 = int64_t(0x10000) << 16;
    uint32_t lpf_alpha = 2863;  // tau ≈ 2s

    uint32_t update(int32_t buffer_level) {
        int32_t ppm = pi.update(buffer_level);
        uint32_t target = ppm_to_rate_q16(ppm);
        int64_t target_q32 = int64_t(target) << 16;
        int64_t error = target_q32 - smoothed_q32;
        smoothed_q32 += (error * lpf_alpha) >> 32;
        return uint32_t(smoothed_q32 >> 16);
    }

    void reset() {
        pi.reset();
        smoothed_q32 = int64_t(0x10000) << 16;
    }
};
```

**差し替え例** — 外部 PLL からのクロック偏差を直接注入:

```cpp
/// 外部 PLL のクロック偏差をそのまま使う ASRC strategy
struct ExternalPllAsrc {
    std::atomic<int32_t>* ppm_source;  // 外部 PLL が書き込む

    uint32_t update(int32_t /*buffer_level*/) {
        // バッファレベルは無視し、PLL のクロック偏差を直接使用
        return ppm_to_rate_q16(ppm_source->load(std::memory_order_relaxed));
    }

    void reset() {}
};
```

#### B. FeedbackStrategy — USB フィードバック値の算出

現状は `FeedbackCalculator` がバッファレベルから PI 制御でフィードバックを算出。
Implicit feedback (UAC2 duplex) では別のロジックが必要。

```cpp
/// USB Async feedback 値の算出 strategy
template<typename T>
concept FeedbackStrategy = requires(T& fb, int32_t buffer_level, uint32_t actual_rate) {
    // バッファレベルからフィードバック値を更新
    { fb.update(buffer_level) } -> std::same_as<void>;

    // 実サンプルレートの設定 (基準値)
    { fb.set_actual_rate(actual_rate) } -> std::same_as<void>;

    // USB 送信用バイト列を取得 (FS: 3 bytes 10.14, HS: 4 bytes 16.16)
    { fb.get_bytes() } -> std::convertible_to<std::span<const uint8_t>>;

    // リセット
    { fb.reset(actual_rate) } -> std::same_as<void>;
};
```

**同期の接続性**: FeedbackStrategy と AsrcStrategy は同じバッファレベルを入力とする。
AudioInterface が `buffered_frames()` を取得し、両方に渡す:

```
RingBuffer::buffered_frames()
    ├─→ FeedbackStrategy::update(level)    [ISR: on_sof]
    └─→ AsrcStrategy::update(level)        [DMA: read_audio_asrc]
```

両者は独立して動作し、直接参照しない。同じバッファレベル信号を共有するだけ。

#### C. GainProcessor — ボリューム/ミュート処理の注入

```cpp
/// オーディオゲイン処理 strategy
template<typename T, typename SampleT>
concept GainProcessor = requires(T& gp, SampleT* buf, uint32_t frames, uint8_t channels) {
    // バッファに in-place でゲインを適用
    { gp.apply(buf, frames, channels) } -> std::same_as<void>;

    // パラメータ設定 (USB Feature Unit から呼ばれる)
    { gp.set_mute(bool{}) } -> std::same_as<void>;
    { gp.set_volume_db256(int16_t{}) } -> std::same_as<void>;
};
```

**デフォルト実装** (現状の `apply_volume_out` を抽出):

```cpp
template<typename SampleT>
struct DefaultGain {
    bool muted = false;
    int16_t volume_db256 = 0;

    void apply(SampleT* buf, uint32_t frames, uint8_t channels) {
        if (muted) {
            __builtin_memset(buf, 0, frames * channels * sizeof(SampleT));
            return;
        }
        if (volume_db256 < 0) {
            // 現状の dB→リニア変換ロジック
            apply_db_gain(buf, frames * channels, volume_db256);
        }
    }
    void set_mute(bool m) { muted = m; }
    void set_volume_db256(int16_t v) { volume_db256 = v; }
};
```

**差し替え例** — 外部エフェクトチェーンを挿入:

```cpp
template<typename SampleT>
struct EffectChainGain {
    DefaultGain<SampleT> volume;
    umidsp::Limiter<SampleT> limiter;
    umidsp::Eq3Band<SampleT> eq;

    void apply(SampleT* buf, uint32_t frames, uint8_t channels) {
        volume.apply(buf, frames, channels);
        eq.process(buf, frames, channels);
        limiter.process(buf, frames, channels);
    }
    void set_mute(bool m) { volume.set_mute(m); }
    void set_volume_db256(int16_t v) { volume.set_volume_db256(v); }
};
```

#### D. SampleCodec — ビット深度変換の注入

```cpp
/// USB Audio サンプルフォーマット変換 strategy
template<typename T, typename SampleT>
concept SampleCodec = requires(T& codec, const uint8_t* usb_data, SampleT* samples,
                               uint32_t count, uint8_t bit_depth) {
    // USB バイト列 → SampleT (Audio OUT: ISR で呼ばれる)
    { codec.decode(usb_data, samples, count, bit_depth) } -> std::same_as<void>;

    // SampleT → USB バイト列 (Audio IN: ISR/DMA で呼ばれる)
    { codec.encode(std::declval<const SampleT*>(), std::declval<uint8_t*>(),
                   count, bit_depth) } -> std::same_as<void>;
};
```

これにより、DSD (1-bit) や 32-bit float などの非標準フォーマットにも対応可能。

#### E. AudioBridge — HW 依存操作の抽象化 (セクション 13 の再掲 + 拡張)

```cpp
/// Audio ハードウェアブリッジ (I2S, SAI, CODEC etc.)
template<typename T>
concept AudioBridge = requires(T& bridge, uint32_t rate) {
    // クロック/DMA 制御
    { bridge.stop_audio() } -> std::same_as<void>;
    { bridge.configure_clock(rate) } -> std::same_as<uint32_t>;  // returns actual rate
    { bridge.start_audio() } -> std::same_as<void>;
    { bridge.clear_buffers() } -> std::same_as<void>;

    // クロック偏差情報 (ASRC strategy への入力として使える)
    // 実装しない場合は 0 を返す (バッファレベルベースの ASRC にフォールバック)
    { bridge.clock_offset_ppm() } -> std::convertible_to<int32_t>;
};
```

### 16-4. 改善後の AudioInterface テンプレートシグネチャ

```cpp
template <UacVersion Version = UacVersion::UAC1,
          typename AudioOut_ = AudioStereo48k,
          typename AudioIn_ = NoAudioPort,
          typename MidiOut_ = NoMidiPort,
          typename MidiIn_ = NoMidiPort,
          uint8_t FeedbackEp_ = 2,
          AudioSyncMode SyncMode_ = AudioSyncMode::ASYNC,
          bool SampleRateControlEnabled_ = true,
          typename SampleT_ = int32_t,
          // ↓ 新規: 注入可能な strategy
          typename AsrcStrategy_ = PiLpfAsrc,
          typename FeedbackStrategy_ = DefaultFeedbackCalculator<Version>,
          typename GainOut_ = DefaultGain<SampleT_>,
          typename GainIn_ = DefaultGain<SampleT_>,
          typename Codec_ = DefaultSampleCodec<SampleT_>>
class AudioInterface;
```

**デフォルト型により既存コードの変更は不要。** 現状と同一の動作が保証される。

### 16-5. 同期と接続性の確保

外部処理を注入する場合、USB フレームタイミング (SOF) と Audio クロック (DMA) の
2つの時間軸を跨いだ同期が必要。以下のデータフローで接続性を確保する。

#### データフロー図 (注入ポイント付き)

```
                  SOF (1ms / 125μs)
                       │
                       ▼
            ┌──────────────────┐
            │   on_sof (ISR)   │
            │                  │
  buffer    │  level = ring    │
  level ←───│  .buffered()     │
  (共有)    │                  │
            │  [Feedback       │──→ ep_write (feedback)
            │   Strategy]      │
            │  .update(level)  │
            └──────────────────┘

                  DMA callback
                       │
                       ▼
            ┌──────────────────┐
            │ read_audio (DMA) │
            │                  │
  buffer    │  level = ring    │
  level ←───│  .buffer_level() │
  (共有)    │                  │
            │  rate = [Asrc    │
            │   Strategy]      │
            │  .update(level)  │
            │                  │
            │  ring.read_      │──→ audio samples
            │  interpolated    │
            │  (rate)          │
            │                  │
            │  [GainProcessor] │──→ gained samples
            │  .apply(buf)     │
            └──────────────────┘

            ┌──────────────────┐
            │ on_rx (ISR)      │
            │                  │
  USB pkt ──│→ [SampleCodec]   │
            │  .decode()       │
            │        │         │
            │        ▼         │
            │  ring.write()    │──→ RingBuffer
            └──────────────────┘
```

#### 同期ポイントの整理

| 同期対象 | Producer | Consumer | 同期機構 | ドメイン |
|---------|----------|----------|---------|---------|
| RingBuffer (OUT) | on_rx (ISR) | read_audio (DMA) | atomic SPSC | ISR → DMA |
| RingBuffer (IN) | write_audio_in (DMA) | on_sof (ISR) | atomic SPSC | DMA → ISR |
| バッファレベル → Feedback | RingBuffer | FeedbackStrategy | .buffered_frames() (atomic read) | ISR |
| バッファレベル → ASRC | RingBuffer | AsrcStrategy | .buffer_level() (atomic read) | DMA |
| ASRC レート → 補間 | AsrcStrategy | RingBuffer | 関数戻り値 (同一コンテキスト) | DMA |
| 外部 PLL → ASRC | PLL ISR | AsrcStrategy | std::atomic<int32_t> | 非同期 |
| Feature Unit → Gain | handle_request (ISR) | GainProcessor | volatile / atomic | ISR → DMA |
| サンプルレート変更 | handle_request (ISR) | AudioBridge | コールバック | ISR → INIT |

#### 外部 PLL との接続パターン

外部 PLL (例: CS2100, Si5351) のクロック偏差情報を ASRC に注入する場合:

```cpp
// PLL ドライバ側 (I2C 割り込み等で更新)
std::atomic<int32_t> pll_offset_ppm{0};

// AudioBridge 実装
struct MyBridge {
    std::atomic<int32_t>* pll_ppm;
    int32_t clock_offset_ppm() { return pll_ppm->load(std::memory_order_relaxed); }
    // ...
};

// ASRC strategy — PLL 偏差 + バッファレベルのハイブリッド
struct HybridAsrc {
    umidsp::PiRateController pi;        // バッファレベル追従 (微調整)
    std::atomic<int32_t>* pll_ppm;      // PLL 粗調整

    uint32_t update(int32_t buffer_level) {
        int32_t fine_ppm = pi.update(buffer_level);  // ±50ppm 範囲
        int32_t coarse_ppm = pll_ppm->load(std::memory_order_relaxed);
        return ppm_to_rate_q16(coarse_ppm + fine_ppm);
    }
    void reset() { pi.reset(); }
};
```

### 16-6. Strategy 間の依存関係と独立性

```
                    独立                独立
FeedbackStrategy ←────── buffer_level ──────→ AsrcStrategy
       │                     ↑                      │
       │              RingBuffer (共有)              │
       │                                            │
       ▼                                            ▼
  ep_write                                  read_interpolated
  (USB feedback)                            (audio output)

  GainProcessor ← Feature Unit state → SampleCodec
       │              (独立)                  │
       ▼                                      ▼
  gained samples                        decoded samples
```

**重要: FeedbackStrategy と AsrcStrategy は互いを知らない。**
両者は RingBuffer のバッファレベルという共有信号を通じて間接的に協調する。
これは Async USB Audio の本質的な構造であり、疎結合が自然に成立する。

### 16-7. AudioInterface からの分離手順

現状のコードから strategy を分離する実装手順:

| 順序 | 作業 | 影響範囲 |
|------|------|---------|
| 1 | `DefaultGain<SampleT>` を抽出 (`apply_volume_out` → 独立クラス) | audio_interface.hh のみ |
| 2 | `DefaultSampleCodec<SampleT>` を抽出 (decode/encode 関数群) | audio_interface.hh のみ |
| 3 | `PiLpfAsrc` を抽出 (`update_asrc_rate` + 状態変数) | audio_interface.hh + audio_types.hh |
| 4 | `DefaultFeedbackCalculator` のラッパーを作成 | audio_interface.hh + audio_types.hh |
| 5 | AudioInterface にテンプレートパラメータを追加 | シグネチャ変更 (後方互換) |
| 6 | 各 concept の `static_assert` 検証を追加 | 新規 |

**後方互換性**: すべてのデフォルト型が現状の内部実装と同一のため、
既存のユーザーコードの変更は不要。

### 16-8. ファイル配置

```
lib/umiusb/include/
├── audio/
│   ├── strategy/                          [新規ディレクトリ]
│   │   ├── asrc_strategy.hh              # AsrcStrategy concept + PiLpfAsrc
│   │   ├── feedback_strategy.hh          # FeedbackStrategy concept + DefaultFeedback
│   │   ├── gain_processor.hh             # GainProcessor concept + DefaultGain
│   │   └── sample_codec.hh              # SampleCodec concept + DefaultCodec
│   ├── audio_interface.hh                # strategy をテンプレートパラメータで受ける
│   ├── audio_types.hh                    # RingBuffer, FeedbackCalculator (プリミティブ)
│   └── audio_bridge.hh                   # AudioBridge concept          [新規]
```

### 16-9. 改訂実装計画への追加

セクション 10 の Phase 1 と Phase 2 の間に以下を挿入:

#### Phase 1.5: Strategy 分離

1. `audio/strategy/` ディレクトリ作成
2. `GainProcessor` concept + `DefaultGain` 抽出 (最も独立性が高い)
3. `SampleCodec` concept + `DefaultSampleCodec` 抽出
4. `AsrcStrategy` concept + `PiLpfAsrc` 抽出
5. `FeedbackStrategy` concept + `DefaultFeedbackCalculator` ラッパー
6. `AudioBridge` concept を `audio_bridge.hh` に移動
7. `AudioInterface` のテンプレートパラメータにデフォルト型付きで追加
8. `static_assert` で全 concept の検証
9. 既存テスト (`xmake test`) が変更なしでパス

---
