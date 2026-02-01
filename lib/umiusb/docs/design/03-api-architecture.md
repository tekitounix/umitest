# 03 — API アーキテクチャとドメイン規定

> 元ドキュメント: [SPEED_SUPPORT_DESIGN.md](../SPEED_SUPPORT_DESIGN.md) セクション 11-14

## 11. 目標ファイル構成

### 11-1. ディレクトリツリー

```
lib/umiusb/
├── include/
│   ├── core/                           # USB プロトコル汎用層 (HW 非依存)
│   │   ├── types.hh                    # USB 基本型、Speed enum、SpeedTraits
│   │   ├── hal.hh                      # Hal concept (正規化済み)、HalBase CRTP
│   │   ├── device.hh                   # Device<Hal, Class> + BOS/Vendor request 対応
│   │   └── descriptor.hh              # constexpr ディスクリプタビルダー + webusb 名前空間
│   │
│   ├── audio/                          # USB Audio Class (HW 非依存)
│   │   ├── audio_types.hh             # RingBuffer, FeedbackCalculator, MidiProcessor
│   │   ├── audio_interface.hh         # AudioInterface<...> 本体
│   │   ├── audio_device.hh            # 高レベル Config → AudioInterface 変換
│   │   └── speed_traits.hh            # SpeedTraits<Speed>, MaxSpeed enum  [新規]
│   │
│   ├── midi/                           # MIDI アダプタ (HW 非依存)
│   │   └── umidi_adapter.hh           # UMI MIDI ↔ USB MIDI 変換
│   │
│   ├── hal/                            # HAL 実装 (HW 依存)
│   │   ├── stm32_otg.hh              # STM32 OTG FS HAL (既存)
│   │   └── stm32_otg_hs.hh           # STM32 OTG HS HAL               [将来]
│   │
│   └── umiusb.hh                      # 統合ヘッダー
│
├── examples/
│   └── stm32f4_midi.hh               # STM32F4 USB MIDI 使用例
│
├── docs/
│   ├── IMPLEMENTATION_ANALYSIS.md
│   ├── SPEED_SUPPORT_DESIGN.md        # 本ドキュメント
│   ├── ASRC_DESIGN.md
│   ├── UAC2_DUPLEX_INVESTIGATION.md
│   └── UMIUSB_REFERENCE.md
│
└── xmake.lua
```

### 11-2. 層の依存方向

```
                    使用側 (kernel, app)
                          │
                          ▼
    ┌──────────────────────────────────────┐
    │          AudioInterface              │  audio/
    │  (Class concept を満たす)             │
    └──────────┬───────────────────────────┘
               │ uses
    ┌──────────▼───────────────────────────┐
    │     Device<Hal, Class>               │  core/
    │  (標準リクエスト + BOS + Vendor)      │
    └──────────┬───────────────────────────┘
               │ requires Hal concept
    ┌──────────▼───────────────────────────┐
    │     Stm32OtgHal / 他 HAL             │  hal/
    │  (Hal concept を満たす)               │
    └──────────────────────────────────────┘

    依存は常に上→下。下位層は上位層を知らない。
    core/ と audio/ は hal/ に依存しない (concept 経由のみ)。
```

---

## 12. API リファレンスとドメイン規定

### 12-1. API 呼び出しドメイン

各メソッドがどのコンテキストで呼ばれるべきかを4つのドメインで規定する。
**ドメイン違反はデータ競合やハードフォルトの原因になる。**

| ドメイン | 説明 | 例 |
|---------|------|-----|
| **INIT** | 電源投入〜USB 接続前に1回だけ呼ぶ | コンストラクタ、コールバック登録 |
| **ISR** | USB 割り込みコンテキスト (IRQ ハンドラ内) | `on_rx`, `on_sof`, `handle_request` |
| **DMA** | Audio DMA 半完了/完了コールバック内 | `read_audio`, `write_audio_in` |
| **QUERY** | 任意のコンテキストから安全 (読み取り専用) | `is_streaming`, `buffered_frames` |

### 12-2. Hal concept API

```cpp
template<typename T>
concept Hal = requires(T& hal, const T& chal, ...) {
```

| メソッド | ドメイン | 説明 |
|---------|---------|------|
| `init()` | INIT | ペリフェラル初期化 |
| `connect()` | INIT | D+ プルアップ有効化、ホストに接続通知 |
| `disconnect()` | INIT | D+ プルアップ無効化 |
| `set_address(uint8_t)` | ISR | USB アドレス設定 (SET_ADDRESS 応答で自動呼出) |
| `ep_configure(EndpointConfig)` | ISR | エンドポイント構成 |
| `ep_write(ep, data, len)` | ISR | エンドポイントへ書き込み |
| `ep_stall(ep, bool in)` | ISR | STALL 応答 |
| `ep_unstall(ep, bool in)` | ISR | STALL 解除 |
| `ep0_prepare_rx(uint16_t len)` | ISR | EP0 DATA OUT ステージ受信準備 |
| `set_feedback_ep(uint8_t ep)` | ISR | フィードバック EP 番号を HAL に通知 |
| `is_feedback_tx_ready()` | ISR | フィードバック送信可能か (パリティ等) |
| `set_feedback_tx_flag()` | ISR | フィードバック送信済みフラグ |
| `poll()` | ISR | 割り込みイベント処理 (IRQ ハンドラから呼ぶ) |
| `get_speed()` | QUERY | ネゴシエーション後の実速度 |
| `is_connected()` | QUERY | 接続状態 |

### 12-3. Class concept API

```cpp
template<typename T>
concept Class = requires(T& cls, const SetupPacket& setup, ...) {
```

| メソッド | ドメイン | 呼び出し元 | 説明 |
|---------|---------|----------|------|
| `config_descriptor()` | ISR | Device (GET_DESCRIPTOR) | Configuration descriptor を返す |
| `bos_descriptor()` | ISR | Device (GET_DESCRIPTOR) | BOS descriptor を返す |
| `on_configured(bool)` | ISR | Device (SET_CONFIGURATION) | 構成変更通知 |
| `configure_endpoints(HalT&)` | ISR | Device (SET_CONFIGURATION) | EP 構成 |
| `set_interface(HalT&, iface, alt)` | ISR | Device (SET_INTERFACE) | Alt setting 変更 |
| `handle_request(setup, response)` | ISR | Device (Class SETUP) | クラスリクエスト処理 |
| `handle_vendor_request(setup, response)` | ISR | Device (Vendor SETUP) | ベンダーリクエスト処理 |
| `on_rx(ep, data)` | ISR | Device (EP OUT RX) | データ受信 |
| `on_ep0_rx(data)` | ISR | Device (EP0 DATA OUT) | EP0 データ受信 |
| `on_sof(HalT&)` | ISR | Device (SOF) | SOF 通知 |
| `on_tx_complete(HalT&, ep)` | ISR | Device (TX complete) | 送信完了通知 |

### 12-4. AudioInterface 公開 API

#### INIT ドメイン — USB 接続前に設定

| メソッド | シグネチャ | 説明 |
|---------|-----------|------|
| `set_midi_callback(cb)` | `void (MidiCallback)` | MIDI 受信コールバック登録 |
| `set_sysex_callback(cb)` | `void (SysExCallback)` | SysEx 受信コールバック登録 |
| `set_sample_rate_callback(cb)` | `void (SampleRateCallback)` | サンプルレート変更コールバック登録 |
| `set_feature_defaults(...)` | `void (bool,int16_t,bool,int16_t)` | Feature Unit 初期値 (Mute, Volume) |
| `set_default_sample_rate(rate)` | `void (uint32_t)` | 初期サンプルレート |

コールバックフィールド (INIT で設定、ISR から呼ばれる):

| フィールド | 型 | 呼ばれるタイミング |
|-----------|-----|------------------|
| `on_streaming_change` | `void (*)(bool)` | Audio OUT streaming 開始/停止時 |
| `on_audio_in_change` | `void (*)(bool)` | Audio IN streaming 開始/停止時 |
| `on_audio_rx` | `void (*)()` | Audio OUT パケット受信時 |
| `on_sof_app` | `void (*)()` | SOF (1ms) — アプリに Audio IN 書き込みタイミング通知 |
| `on_sample_rate_change` | `SampleRateCallback` | ホストがサンプルレート変更要求時 |

#### ISR ドメイン — USB 割り込みハンドラ内で自動呼出

これらは **Device から自動的に呼ばれる**。アプリケーションが直接呼ぶことはない。

| メソッド | トリガ | 内部動作 |
|---------|--------|---------|
| `on_configured(bool)` | SET_CONFIGURATION | EP リセット |
| `set_interface(hal, iface, alt)` | SET_INTERFACE | Audio/MIDI EP の構成・解放 |
| `handle_request(setup, response)` | Class SETUP | サンプルレート制御、Feature Unit |
| `handle_vendor_request(setup, response)` | Vendor SETUP | WinUSB/WebUSB ディスクリプタ |
| `on_rx(ep, data)` | EP OUT 受信 | Audio OUT → RingBuffer 書込、MIDI 処理 |
| `on_sof(hal)` | SOF (1ms/125μs) | フィードバック送信、Audio IN 送信 |
| `on_tx_complete(hal, ep)` | EP IN 送信完了 | 次パケット準備 |

#### DMA ドメイン — Audio DMA コールバック内

**Audio OUT 消費** (DMA 半完了/完了コールバックで呼ぶ):

| メソッド | シグネチャ | 説明 |
|---------|-----------|------|
| `read_audio(buf, frames)` | `uint32_t (SampleT*, uint32_t)` | RingBuffer から読み出し |
| `read_audio_asrc(buf, frames)` | `uint32_t (SampleT*, uint32_t)` | ASRC 補間付き読み出し |

**Audio IN 供給** (DMA コールバックまたは `on_sof_app` で呼ぶ):

| メソッド | シグネチャ | 説明 |
|---------|-----------|------|
| `write_audio_in(buf, frames)` | `uint32_t (const SampleT*, uint32_t)` | RingBuffer へ書き込み |
| `write_audio_in_overwrite(buf, frames)` | `uint32_t (const SampleT*, uint32_t)` | 上書き書き込み (溢れ時) |
| `reset_audio_in_buffer()` | `void ()` | Audio IN バッファクリア |

**MIDI 送信** (任意タスクから。HalT& が必要):

| メソッド | シグネチャ | 説明 |
|---------|-----------|------|
| `send_midi(hal, cin, b0, b1, b2)` | `void` | 生 MIDI パケット送信 |
| `send_note_on(hal, ch, note, vel)` | `void` | Note On 送信 |
| `send_note_off(hal, ch, note, vel)` | `void` | Note Off 送信 |
| `send_cc(hal, ch, cc, val)` | `void` | CC 送信 |
| `send_sysex(hal, data, len, cable)` | `void` | SysEx 送信 |

**サンプルレート変更時** (`on_sample_rate_change` コールバック内で呼ぶ):

| メソッド | シグネチャ | 説明 |
|---------|-----------|------|
| `set_actual_rate(rate)` | `void (uint32_t)` | I2S 実レート設定 (フィードバック更新) |
| `reset_audio_out(rate)` | `void (uint32_t)` | OUT バッファ + フィードバックリセット |
| `clear_sample_rate_changed()` | `void ()` | 変更フラグクリア |

#### QUERY ドメイン — 任意コンテキストから安全

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `is_streaming()` | `bool` | Audio OUT ストリーミング中か |
| `is_audio_in_streaming()` | `bool` | Audio IN ストリーミング中か |
| `is_midi_configured()` | `bool` | MIDI EP 構成済みか |
| `is_playback_started()` | `bool` | プライミング完了、再生可能か |
| `current_sample_rate()` | `uint32_t` | 現在のサンプルレート |
| `sample_rate_changed()` | `bool` | サンプルレート変更要求ありか |
| `buffered_frames()` | `uint32_t` | OUT バッファ内フレーム数 |
| `in_buffered_frames()` | `uint32_t` | IN バッファ内フレーム数 |
| `underrun_count()` | `uint32_t` | OUT アンダーラン回数 |
| `overrun_count()` | `uint32_t` | OUT オーバーラン回数 |
| `current_feedback()` | `uint32_t` | フィードバック値 (生値) |
| `feedback_rate()` | `float` | フィードバックレート (Hz) |
| `is_muted()` / `is_in_muted()` | `bool` | Mute 状態 |
| `volume_db256()` / `in_volume_db256()` | `int16_t` | Volume (1/256 dB 単位) |

---

## 13. 呼び出しシーケンスと抽象化の課題

### 13-1. 現状: カーネルに埋め込まれたプロトコル知識

現在、以下のプロトコル知識がカーネル (stm32f4_kernel) にハードコードされている。
これは「正しく呼ばないと動かない」暗黙の契約であり、新しいアプリケーションで
同じ実装を繰り返すか、不正な呼び出しでバグを生む。

#### USB 初期化プロトコル

```
現状 (kernel/mcu.cc):
  1. コールバック登録                    ← kernel が知る必要がある順序
  2. usb_hal.disconnect()               ← 前回のファームウェアの状態クリア
  3. 50ms 遅延                          ← macOS 列挙解除待ち (暗黙知)
  4. シリアル番号文字列生成 (UID→UTF-16LE 手動変換)
  5. usb_device.set_strings(...)
  6. usb_device.init()
  7. usb_hal.connect()
  8. NVIC 有効化
```

問題: 順序を間違えると、コールバック未登録でクラッシュ、
列挙失敗、シリアル番号が表示されないなどの不具合が起きる。

#### サンプルレート変更プロトコル

```
現状 (kernel/kernel.cc):
  on_sample_rate_change コールバック内で:
  1. I2S IRQ 無効化
  2. DMA 停止
  3. I2S 停止
  4. DMA バッファゼロクリア
  5. PLLI2S 再構成 → 実レート取得
  6. キュードレーン
  7. usb_audio.set_actual_rate(actual_rate)
  8. usb_audio.reset_audio_out(actual_rate)
  9. usb_audio.clear_sample_rate_changed()
  10. DMA は停止のまま (streaming 再開で起動)
```

問題: 10 ステップの厳密な順序が必要。7-9 の順序を間違えると
フィードバック値が不正になり、ホスト側でクリック音が発生する。

#### Audio OUT 消費プロトコル

```
現状 (kernel/kernel.cc):
  DMA 半完了/完了コールバック内で:
  1. is_streaming() チェック
  2. read_audio(buf, frames) で RingBuffer から読み出し
  3. I2S フォーマットにパック (24-bit MSB 詰め、2 ワード/サンプル)
  4. プライミング未完了なら無音出力
```

問題: パック関数が STM32 I2S 固有。他プラットフォームでは異なるフォーマット。

### 13-2. 改善案: プロトコルをライブラリが規定する設計

#### 方針

1. **正しい呼び出し順序をコンパイラが強制する** — Builder パターン、状態型
2. **プラットフォーム固有部分を concept で分離する** — AudioBridge concept
3. **ライブラリが「いつ何を呼ぶか」を規定する** — コールバックではなく、ライブラリがドライブ

#### AudioBridge concept — HW 依存操作の抽象化

カーネルに埋め込まれていた「DMA 停止→クロック変更→DMA 再開」の
プロトコル知識をライブラリ側に移す。アプリは AudioBridge concept を
満たす型を提供するだけでよい。

```cpp
/// Audio ハードウェアブリッジ (I2S, SAI, etc.)
/// アプリケーションが実装し、AudioInterface に渡す
template<typename T>
concept AudioBridge = requires(T& bridge, uint32_t rate) {
    // クロック/DMA 制御 — サンプルレート変更時にライブラリが呼ぶ
    { bridge.stop_audio() } -> std::same_as<void>;
    { bridge.configure_clock(rate) } -> std::same_as<uint32_t>;  // returns actual rate
    { bridge.start_audio() } -> std::same_as<void>;
    { bridge.clear_buffers() } -> std::same_as<void>;
};
```

ライブラリ側のサンプルレート変更処理:

```cpp
// AudioInterface 内部 — アプリはこの順序を知る必要がない
template<AudioBridge BridgeT>
void handle_sample_rate_change(BridgeT& bridge, uint32_t new_rate) {
    bridge.stop_audio();
    bridge.clear_buffers();
    uint32_t actual = bridge.configure_clock(new_rate);
    set_actual_rate(actual);
    reset_audio_out(actual);
    clear_sample_rate_changed();
    // DMA は streaming 再開時に自動起動
}
```

#### Device 初期化の型安全化

```cpp
/// USB デバイスの初期化を型安全に行う
/// 必須ステップが欠けるとコンパイルエラー
template<Hal HalT, Class ClassT>
class DeviceBuilder {
    HalT& hal_;
    ClassT& class_;
    DeviceInfo info_;
    std::span<const std::span<const uint8_t>> strings_;
    bool has_strings_ = false;

public:
    DeviceBuilder(HalT& hal, ClassT& cls, const DeviceInfo& info)
        : hal_(hal), class_(cls), info_(info) {}

    DeviceBuilder& set_strings(std::span<const std::span<const uint8_t>> s) {
        strings_ = s;
        has_strings_ = true;
        return *this;
    }

    /// 初期化を実行し Device を返す
    /// disconnect → delay → init → connect の順序をライブラリが保証
    Device<HalT, ClassT> build() {
        hal_.disconnect();
        // macOS 列挙解除待ち (内部で適切な遅延)
        delay_for_host_de_enumeration();
        Device<HalT, ClassT> dev(hal_, class_, info_);
        if (has_strings_) dev.set_strings(strings_);
        dev.init();
        hal_.connect();
        return dev;
    }
};
```

### 13-3. 改善後の典型的なアプリケーションコード

```cpp
// === bsp.hh (ボード固有) ===
struct MyAudioBridge {
    void stop_audio() { disable_i2s_irq(); dma_disable(); i2s_disable(); }
    uint32_t configure_clock(uint32_t rate) { return configure_plli2s(rate); }
    void start_audio() { dma_init(); enable_i2s_irq(); i2s_enable(); dma_enable(); }
    void clear_buffers() { memset(dma_buf, 0, sizeof(dma_buf)); }
};

// === main.cc ===
// 型定義
using UsbAudio = umiusb::AudioInterface<...>;
umiusb::Stm32FsHal hal;
UsbAudio audio;

// 初期化 — ライブラリが正しい順序を保証
auto device = umiusb::DeviceBuilder(hal, audio, device_info)
    .set_strings(string_table)
    .build();

// コールバック登録 — ドメインが型で明示される
audio.set_midi_callback([](uint8_t cable, const uint8_t* data, uint8_t len) {
    // ISR コンテキストで呼ばれる
    midi_queue.push(data, len);
});

// DMA コールバック — read_audio は DMA ドメイン
void dma_half_complete(uint16_t* buf) {
    audio.read_audio(work_buf, FRAME_COUNT);  // DMA ドメイン
    pack_i2s(buf, work_buf, FRAME_COUNT);
}

// SOF コールバック — write_audio_in は on_sof_app 内
audio.on_sof_app = []() {
    audio.write_audio_in(adc_buf, FRAME_COUNT);  // DMA ドメイン
};

// サンプルレート変更 — ライブラリがプロトコルを実行
MyAudioBridge bridge;
audio.on_sample_rate_change = [&](uint32_t rate) {
    audio.handle_sample_rate_change(bridge, rate);  // ライブラリが順序を保証
};

// USB IRQ
extern "C" void OTG_FS_IRQHandler() {
    device.poll();  // ISR ドメイン
}
```

### 13-4. ドメイン違反の防止（将来）

コンパイル時にドメイン違反を検出する仕組みとして、
タグ型による制約が考えられる:

```cpp
// ISR ドメインでのみ呼べるメソッドにタグを要求
struct IsrToken { /* 生成は HAL の poll() 内部のみ */ };

void on_rx(IsrToken, uint8_t ep, std::span<const uint8_t> data);
// → ISR 外から呼ぶと IsrToken が無いのでコンパイルエラー
```

これは将来の強化候補であり、現時点ではドキュメントでの規定とする。

---

## 14. 全 API 一覧 (ドメイン別)

### Device<Hal, Class>

| ドメイン | メソッド | 説明 |
|---------|---------|------|
| INIT | `DeviceBuilder::build()` | 初期化 (disconnect→delay→init→connect) |
| ISR | `poll()` | USB IRQ ハンドラから呼ぶ |
| QUERY | `is_configured()` | 構成済みか |
| QUERY | `is_suspended()` | サスペンド中か |

### AudioInterface<...>

#### INIT

| メソッド | 説明 |
|---------|------|
| `set_midi_callback(cb)` | MIDI 受信コールバック |
| `set_sysex_callback(cb)` | SysEx 受信コールバック |
| `set_sample_rate_callback(cb)` | サンプルレート変更コールバック |
| `set_feature_defaults(...)` | Feature Unit 初期値 |
| `set_default_sample_rate(rate)` | 初期サンプルレート |
| `on_streaming_change` | Audio OUT streaming 状態変更通知 |
| `on_audio_in_change` | Audio IN streaming 状態変更通知 |
| `on_audio_rx` | Audio OUT パケット受信通知 |
| `on_sof_app` | SOF タイミング通知 (Audio IN 書込用) |
| `on_sample_rate_change` | サンプルレート変更通知 |

#### ISR (Device から自動呼出 — アプリは直接呼ばない)

| メソッド | トリガ |
|---------|--------|
| `on_configured(bool)` | SET_CONFIGURATION |
| `configure_endpoints(hal)` | SET_CONFIGURATION (configured=true) |
| `set_interface(hal, iface, alt)` | SET_INTERFACE |
| `handle_request(setup, response)` | Class SETUP |
| `handle_vendor_request(setup, response)` | Vendor SETUP (WinUSB/WebUSB) |
| `on_rx(ep, data)` | EP OUT 受信 |
| `on_ep0_rx(data)` | EP0 DATA OUT |
| `on_sof(hal)` | SOF |
| `on_tx_complete(hal, ep)` | TX 完了 |

#### DMA (Audio DMA コールバックから呼ぶ)

| メソッド | 説明 |
|---------|------|
| `read_audio(buf, frames)` | Audio OUT 消費 |
| `read_audio_asrc(buf, frames)` | Audio OUT 消費 (ASRC 補間) |
| `write_audio_in(buf, frames)` | Audio IN 供給 |
| `write_audio_in_overwrite(buf, frames)` | Audio IN 供給 (上書き) |
| `reset_audio_in_buffer()` | Audio IN バッファクリア |
| `send_midi(hal, ...)` | MIDI 送信 |
| `send_note_on/off/cc(hal, ...)` | MIDI ショートメッセージ送信 |
| `send_sysex(hal, data, len, cable)` | SysEx 送信 |

#### DMA (サンプルレート変更時)

| メソッド | 順序 | 説明 |
|---------|------|------|
| `set_actual_rate(rate)` | 1st | I2S 実レートをフィードバックに反映 |
| `reset_audio_out(rate)` | 2nd | バッファ + フィードバックリセット |
| `clear_sample_rate_changed()` | 3rd | 変更フラグクリア |

→ **`handle_sample_rate_change(bridge, rate)` で 3 つまとめて実行推奨**

#### QUERY (任意コンテキスト)

| メソッド | 戻り値 |
|---------|--------|
| `is_streaming()` | Audio OUT ストリーミング中 |
| `is_audio_in_streaming()` | Audio IN ストリーミング中 |
| `is_midi_configured()` | MIDI 構成済み |
| `is_playback_started()` | プライミング完了 |
| `current_sample_rate()` | サンプルレート (Hz) |
| `sample_rate_changed()` | 変更要求あり |
| `buffered_frames()` / `in_buffered_frames()` | バッファ内フレーム数 |
| `underrun_count()` / `overrun_count()` | アンダーラン/オーバーラン回数 |
| `current_feedback()` | フィードバック生値 |
| `feedback_rate()` | フィードバックレート (Hz) |
| `is_muted()` / `volume_db256()` | Mute/Volume 状態 |

### AudioRingBuffer<Frames, Channels, SampleT>

| ドメイン | メソッド | 説明 |
|---------|---------|------|
| INIT | `reset()` | バッファクリア |
| INIT | `reset_and_start()` | サイレンスプリバッファ付きリセット |
| ISR | `write(data, frames)` | USB RX → バッファ書込 |
| ISR | `write_overwrite(data, frames)` | 上書き書込 (溢れ許容) |
| DMA | `read(buf, frames)` | バッファ → DMA 読出 |
| DMA | `read_interpolated(buf, frames, rate_q16)` | ASRC 補間読出 |
| QUERY | `capacity()` | バッファ容量 |
| QUERY | `buffered_frames()` | 格納フレーム数 |
| QUERY | `buffer_level()` | バッファレベル |
| QUERY | `is_playback_started()` | 再生開始済みか |

### FeedbackCalculator

| ドメイン | メソッド | 説明 |
|---------|---------|------|
| INIT | `reset(nominal_rate)` | 初期化 |
| INIT | `set_buffer_half_size(size)` | バッファ半分サイズ設定 |
| ISR | `set_actual_rate(rate)` | 実レート更新 |
| ISR | `update_from_buffer_level(level)` | バッファレベルからフィードバック更新 |
| ISR | `get_feedback_bytes()` | フィードバックバイト列 (USB 送信用) |
| QUERY | `get_feedback()` | フィードバック値 (Q10.14) |
| QUERY | `get_feedback_rate()` | フィードバックレート (Hz) |

### MidiProcessor

| ドメイン | メソッド | 説明 |
|---------|---------|------|
| INIT | `on_midi = ...` | MIDI コールバック設定 |
| INIT | `on_sysex = ...` | SysEx コールバック設定 |
| ISR | `process_packet(header, b0, b1, b2)` | USB MIDI パケット処理 |
| QUERY | `status_to_cin(status)` | ステータス→CIN 変換 (static) |

---
