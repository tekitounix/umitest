# umiusb ライブラリ リファレンス

## 1. ファイルツリー

### コアライブラリ

```
lib/umiusb/
├── docs/
│   └── ASRC_DESIGN.md                 # PI制御ベース ASRC 設計ドキュメント
├── examples/
│   └── stm32f4_midi.hh               # STM32F4 USB MIDI 使用例
├── include/
│   ├── core/                          # USB プロトコル汎用 (Audio 非依存)
│   │   ├── types.hh                   #   USB 基本型 (SetupPacket, ディスクリプタ構造体)
│   │   ├── hal.hh                     #   Hal concept 定義、HalBase CRTP
│   │   ├── device.hh                  #   Device コア (制御転送、標準リクエスト処理)
│   │   └── descriptor.hh             #   コンパイル時ディスクリプタビルダー
│   ├── audio/                         # USB Audio Class (UAC1/UAC2)
│   │   ├── audio_interface.hh         #   AudioInterface メイン実装 (~3100行)
│   │   ├── audio_types.hh            #   RingBuffer, FeedbackCalc, MidiProcessor 等
│   │   └── audio_device.hh           #   高レベル Config → AudioInterface 変換
│   ├── midi/                          # MIDI アダプタ
│   │   └── umidi_adapter.hh          #   UMI MIDI ↔ USB MIDI 変換
│   ├── hal/                           # ポート実装 (ハードウェア固有)
│   │   └── stm32_otg.hh             #   STM32 OTG FS/HS HAL (~1166行)
│   ├── umiusb.hh                     # 統合ヘッダー
│   └── descriptor_examples.hh        # ディスクリプタ使用例 (参考用、ビルド非対象)
└── xmake.lua                         # ビルド設定 (headeronly)
```

### 使用箇所 (アプリケーション)

```
examples/stm32f4_kernel/
├── src/
│   ├── bsp.hh          # USB_AUDIO_UAC2, USB_AUDIO_ADAPTIVE マクロで構成切替
│   ├── mcu.hh          # UsbAudioDevice 型定義 (UAC1/UAC2 条件コンパイル)
│   ├── mcu.cc          # Stm32FsHal インスタンス、USB 初期化、コールバック登録
│   ├── kernel.cc       # Audio IN/OUT DMA コールバック、SOF ハンドリング
│   ├── kernel.hh       # カーネルクラス宣言
│   └── main.cc         # エントリポイント
└── xmake.lua           # umi.usb 依存

examples/stm32f4_synth/
├── src/
│   └── main.cc         # umiusb 直接使用 (AudioFullDuplexMidi48k, Stm32FsHal)
└── xmake.lua           # umiusb include 直接指定
```

### 依存ライブラリ

```
lib/umidsp/include/audio/rate/
├── pi_controller.hh    # PiRateController — umiusb の PllRateController が使用
└── asrc.hh             # ASRC 実装

lib/umidi/include/       # MIDI 型定義 — umidi_adapter.hh が使用
```

### ビルド設定

```
lib/umi/xmake.lua        # target("umi.usb") headeronly、add_deps("umi.dsp")
```

### 関連ドキュメント

```
docs/umi-usb/
├── USB_AUDIO.md                  # USB Audio 仕様
└── USB_AUDIO_REDESIGN_PLAN.md    # USB Audio 再設計計画

docs/dev/
├── UAC2_DUPLEX_INVESTIGATION.md  # UAC2 duplex 調査報告 (DWC2 パリティ問題)
└── UMIUSB_REFERENCE.md           # 本ドキュメント
```

### デバッグツール

```
tools/debug/
├── read_usb_desc.py       # pyocd 経由 USB ディスクリプタ読み取り
├── read_audio_stats.py    # オーディオ統計カウンタ読み取り
├── check_audio.py         # オーディオ状態チェック
├── check_audio_v2.py      # オーディオチェック v2
├── monitor_audio.py       # リアルタイムオーディオモニタリング
├── debug_audio_now.py     # 即時オーディオデバッグ
├── stm32_debug.py         # STM32 汎用デバッグ
└── umi_debug.py           # UMI デバッグユーティリティ
```

### umios 内の関連モジュール (umiusb とは独立)

```
lib/umios/kernel/modules/
├── usb_audio_module.hh    # USB Audio タスクインターフェース (IUsbAudioTask, ISofCallback)
└── audio_module.hh        # AudioRingBuffer (volatile ベース), IAudioProcessor, AudioStats

lib/umios/kernel/
└── umi_audio.hh           # AudioEngine テンプレート (DMA/タスク連携)

lib/umios/backend/cm/stm32f4/
├── usb_midi.hh            # 旧 USB MIDI — umiusb 導入前。デッドコード、削除候補
└── usb_otg.hh             # 旧 OTG HAL — umiusb 導入前。デッドコード、削除候補
```

### 参考実装 (.refs/)

```
.refs/stm32f411-usbaudio/  # STM32F4 USB Audio 参考
.refs/sw_usb_audio/        # XMOS USB Audio 参考 (implicit feedback)
.refs/tinyusb/             # TinyUSB 参考
```

### テスト

```
tests/test_audio.cc        # AudioEngine テスト (umi_audio.hh 使用、umiusb 自体のテストなし)
```

## 2. 未使用・デッドコード

| ファイル | 状態 | 備考 |
|---------|------|------|
| `lib/umios/backend/cm/stm32f4/usb_midi.hh` | **デッドコード** | umiusb 導入前の旧実装。どこからも include されていない。削除候補 |
| `lib/umios/backend/cm/stm32f4/usb_otg.hh` | **デッドコード** | 同上。rcc.hh が `enable_usb_otg_fs()` を持つが usb_otg.hh 自体は未使用 |
| `lib/umiusb/include/descriptor_examples.hh` | **参考のみ** | ビルドに含まれない。ディスクリプタ API の使用例 |
| `lib/umios/kernel/modules/usb_audio_module.hh` | **未使用の可能性** | umiusb とは別の USB Audio タスク設計。stm32f4_kernel では使用していない |
| `lib/umios/kernel/modules/audio_module.hh` | **未使用の可能性** | 独立した AudioRingBuffer / IAudioProcessor。umiusb の AudioRingBuffer とは別実装 |
| `lib/umios/kernel/umi_audio.hh` | **テストのみ** | tests/test_audio.cc から使用。stm32f4_kernel では使用していない |

**注意**: umios modules 内の `AudioRingBuffer` (volatile ベース SPSC) と umiusb の `AudioRingBuffer` (atomic ベース SPSC + cubic 補間 ASRC) は**別の実装**。

## 3. クラス・型一覧

### AudioInterface (audio_interface.hh)

メインクラス。UAC1/UAC2 オーディオ＋MIDI デバイスの全機能を提供。

```cpp
template <UacVersion Version = UacVersion::Uac1,
          typename AudioOut_ = AudioStereo48k,
          typename AudioIn_ = NoAudioPort,
          typename MidiOut_ = NoMidiPort,
          typename MidiIn_ = NoMidiPort,
          uint8_t FeedbackEp_ = 2,
          AudioSyncMode SyncMode_ = AudioSyncMode::Async,
          bool SampleRateControlEnabled_ = true,
          typename SampleT_ = int32_t>
class AudioInterface;
```

| パラメータ | 説明 |
|-----------|------|
| `Version` | `Uac1` または `Uac2` |
| `AudioOut_` | Audio OUT ポート設定 (`AudioPort<...>` または `NoAudioPort`) |
| `AudioIn_` | Audio IN ポート設定 |
| `MidiOut_` / `MidiIn_` | MIDI ポート設定 (`MidiPort<...>` または `NoMidiPort`) |
| `FeedbackEp_` | Async フィードバック EP 番号 |
| `SyncMode_` | `Async` / `Adaptive` / `Sync` |
| `SampleRateControlEnabled_` | サンプルレート変更許可 |
| `SampleT_` | 内部サンプル型 (`int32_t` / `int16_t`) |

### AudioPort (audio_interface.hh)

```cpp
template <uint8_t Channels_, uint8_t BitDepth_, uint32_t SampleRate_,
          uint8_t Endpoint_, uint32_t MaxSampleRate_ = SampleRate_,
          typename Rates_ = AudioRates<SampleRate_>,
          typename AltSettings_ = DefaultAltList<BitDepth_, Rates_>,
          uint32_t ChannelConfig_ = DefaultChannelConfig<Channels_>::value>
struct AudioPort;
```

| パラメータ | 説明 |
|-----------|------|
| `Channels_` | チャンネル数 (1=mono, 2=stereo) |
| `BitDepth_` | ビット深度 (16 / 24) |
| `SampleRate_` | ノミナルサンプルレート |
| `Endpoint_` | EP 番号 |
| `MaxSampleRate_` | パケットサイズ計算用最大レート |
| `Rates_` | `AudioRates<48000, 96000>` 等の離散レートリスト |
| `AltSettings_` | UAC1 オルタネート設定リスト |
| `ChannelConfig_` | UAC2 チャンネル設定ビット |

### MidiPort (audio_interface.hh)

```cpp
template <uint8_t Cables_, uint8_t Endpoint_, uint16_t PacketSize_ = 64>
struct MidiPort;
```

### プリセット型エイリアス (audio_interface.hh)

| 型 | 定義 |
|----|------|
| `NoAudioPort` | `AudioPort<0, 16, 48000, 0>` |
| `NoMidiPort` | `MidiPort<0, 0>` |
| `AudioStereo48k` | `AudioPort<2, 16, 48000, 1>` |
| `AudioMono48k` | `AudioPort<1, 16, 48000, 1>` |
| `AudioMidi<Cables, PacketSize>` | MIDI のみの AudioInterface |
| `AudioInterface48kAsync` | UAC1 stereo OUT-only async |
| `AudioInterface48kAsyncV2` | UAC2 stereo OUT-only async |
| `AudioMidiInterface48k` | UAC1 stereo OUT + MIDI |
| `AudioMidiInterface48kV2` | UAC2 stereo OUT + MIDI |
| `AudioFullDuplex48k` | UAC1 stereo duplex (OUT+IN) |
| `AudioFullDuplex48kV2` | UAC2 stereo duplex (OUT+IN) |
| `AudioFullDuplexMidi48k` | UAC1 stereo duplex + MIDI |

### audio_types.hh

| 型 | 説明 |
|----|------|
| `AudioRates<uint32_t... Rates>` | コンパイル時サンプルレートリスト |
| `AudioAltSetting<BitDepth, Rates>` | UAC1 Alt 設定 |
| `AudioAltList<AltSettings...>` | Alt 設定リスト |
| `FeedbackCalculator<UacVersion>` | フィードバック値計算 (10.14 / 16.16) |
| `AudioRingBuffer<Frames, Ch, T>` | ロックフリー SPSC リングバッファ (cubic エルミート補間 ASRC 対応) |
| `MidiProcessor` | USB MIDI パケット処理 (SysEx 対応) |
| `PllRateController` | `umidsp::PiRateController` エイリアス (PI 制御) |

### Device (device.hh)

```cpp
template <typename HalT, typename ClassT>
class Device;
```

USB デバイスコア。標準リクエスト (GET_DESCRIPTOR, SET_ADDRESS 等) 処理。
`ClassT` (= AudioInterface) にクラス固有リクエストを委譲。

### HAL (hal.hh)

| 型 | 説明 |
|----|------|
| `Hal` concept | HAL 実装が満たすべき要件定義 |
| `HalBase<Derived>` | CRTP ベースクラス (共通機能) |
| `HalCallbacks` | HAL → Device コールバック構造体 |

### STM32 OTG HAL (hal/stm32_otg.hh)

| 型 | 説明 |
|----|------|
| `Stm32OtgHal<BaseAddr, MaxEps>` | STM32 OTG FS/HS 実装 |
| `Stm32FsHal` | `Stm32OtgHal<0x50000000, 4>` (OTG FS) |

### Descriptor (descriptor.hh)

| 型 | 説明 |
|----|------|
| `Bytes<N>` | コンパイル時バイトバッファ |
| `StringDesc<N>` | コンパイル時 UTF-16LE 文字列ディスクリプタ |
| `DeviceDesc` / `ConfigHeader` / `InterfaceDesc` / `EndpointDesc` 等 | 各種ディスクリプタ構造体 |

### audio_device.hh (高レベル Config API)

| 型 | 説明 |
|----|------|
| `StreamConfig<...>` | ストリーム設定 |
| `AudioDeviceConfig<...>` | デバイス全体設定 |
| `AudioDeviceTraits<Config>` | Config → AudioInterface マッピング |
| `AudioInterfaceFromConfig<Config>` | Config → 最終型生成 |

## 4. データフロー

### Audio OUT (USB → DAC)

```
Host
 │ Isochronous OUT packet (EP1, 毎 1ms)
 ▼
Stm32OtgHal::poll()  ← RXFLVL 割り込み、FIFO 読み出し
 │
 ▼
AudioInterface::on_rx()
 │ int24/int16 デコード → int32_t
 │ out_ring_buffer_.write()
 │ FeedbackCalculator 更新 (バッファレベル監視)
 │
 ▼
AudioInterface::read_audio() / read_audio_interpolated()
 │ kernel DMA callback (I2S half/complete) から呼び出し
 │ ASRC 時は cubic エルミート補間 + PiRateController
 ▼
I2S DMA → DAC (CS43L22)
```

### Audio IN (アプリケーション → USB)

```
I2S DMA (合成出力) / ADC
 │ DMA callback
 ▼
AudioInterface::write_audio_in(int32_t*)
 │ in_ring_buffer_.write()
 │
 ▼
SOF 割り込み (1ms 毎)
 │
AudioInterface::on_sof() → send_audio_in_now()
 │ in_ring_buffer_.read()
 │ int32_t → int24/int16 エンコード
 │ hal.ep_write(EP_AUDIO_IN)
 │ ※ iso IN パリティ: DSTS.FNSOF の偶奇に合わせる
 ▼
Host
```

### Implicit Feedback (UAC2 duplex async)

```
条件: use_implicit_fb = (Async) && HAS_AUDIO_OUT && HAS_AUDIO_IN

・Audio IN パケットレート (1000/秒) がホストへの暗黙のフィードバック
・明示的 Feedback EP は省略 (ディスクリプタから除外)
・Audio IN sync type = 0x25 (Async + Implicit FB)
・Apple TN2274 / XMOS リファレンスに準拠
```

### Explicit Feedback (Async, OUT-only)

```
SOF 割り込み (bRefresh=2, 2ms 毎)
 │
 ▼
AudioInterface::on_sof() → try_send_feedback()
 │ バッファレベルから PID 制御でフィードバック値計算
 │ 10.14 形式 3 バイト (macOS xHCI の制約)
 │ hal.ep_write(EP_FEEDBACK)
 ▼
Host  → OUT パケットサイズ調整
```

### MIDI

```
Host → EP_MIDI_OUT (Bulk) → AudioInterface::on_rx()
 → MidiProcessor::parse() → midi_rx_callback_

AudioInterface::send_midi() → MidiProcessor::build()
 → hal.ep_write(EP_MIDI_IN) → Host
```

## 5. HAL 依存関係

```
AudioInterface ─────────────────────────────────────────
    │
    ├── Device<HalT, ClassT>
    │       │
    │       └── HalT (= Stm32FsHal)
    │              │
    │              ├── STM32 OTG FS レジスタ直接操作
    │              │   ├── GAHBCFG, GUSBCFG, GINTMSK (グローバル)
    │              │   ├── DCFG, DCTL, DSTS (デバイス)
    │              │   ├── DIEPCTL/DOEPCTL (EP 制御)
    │              │   ├── DIEPTSIZ/DOEPTSIZ (転送サイズ)
    │              │   ├── DIEPMSK/DOEPMSK (EP 割り込みマスク)
    │              │   └── FIFO (EP データ読み書き)
    │              │
    │              └── 外部依存
    │                  ├── NVIC (IRQ 有効化) ← bsp::irq::otg_fs
    │                  └── GPIO (DP/DM ピン) ← bsp::usb::dm/dp
    │
    ├── AudioRingBuffer (ロックフリー、std::atomic)
    │
    ├── FeedbackCalculator (純粋計算)
    │
    ├── MidiProcessor (USB MIDI パケットパーサ)
    │       └── umidi_adapter.hh → lib/umidi (MIDI 型変換)
    │
    └── PllRateController → lib/umidsp PiRateController (PI 制御)
```

## 6. 使用例

### stm32f4_kernel (現在のメインターゲット)

**構成切替 (bsp.hh)**:
```cpp
#define USB_AUDIO_ADAPTIVE 0  // 1: Adaptive sync, 0: Async
#define USB_AUDIO_UAC2     0  // 1: UAC2, 0: UAC1
```

**型定義 (mcu.hh)**:
```cpp
// UAC2 構成 (USB_AUDIO_UAC2=1)
using UsbAudioDevice =
    umiusb::AudioInterface<umiusb::UacVersion::Uac2,
        umiusb::AudioPort<2, 24, 48000, 1, 48000, umiusb::AudioRates<48000>>,
        umiusb::AudioPort<2, 24, 48000, 3, 48000, umiusb::AudioRates<48000>>,
        umiusb::MidiPort<1, 2>, umiusb::MidiPort<1, 1>,
        2, umiusb::AudioSyncMode::Async, false>;

// UAC1 構成 (USB_AUDIO_UAC2=0)
using UsbAudioDevice =
    umiusb::AudioInterface<umiusb::UacVersion::Uac1,
        umiusb::AudioPort<2, 24, 48000, 1, 96000,
            umiusb::AudioRates<48000, 44100>,
            umiusb::AudioAltList<
                umiusb::AudioAltSetting<24, umiusb::AudioRates<48000, 44100>>,
                umiusb::AudioAltSetting<16, umiusb::AudioRates<48000, 44100>>>>,
        umiusb::AudioPort<2, 24, 48000, 3, 96000, ...同様...>,
        umiusb::MidiPort<1, 2>, umiusb::MidiPort<1, 1>,
        2, umiusb::AudioSyncMode::Async>;
```

**初期化フロー (mcu.cc)**:
```
1. Stm32FsHal::init()            OTG FS ペリフェラル初期化
2. Device 構築                    HAL + AudioInterface 結合
3. set_strings()                  文字列ディスクリプタ登録 (UID シリアル含む)
4. コールバック登録:
   - set_rx_callback()            Audio OUT 受信
   - set_audio_in_ready_callback() Audio IN 送信要求
   - set_sample_rate_callback()   サンプルレート変更通知
   - set_midi_rx_callback()       MIDI 受信
5. connect()                      USB 接続 (D+ プルアップ)
```

**ランタイムフロー**:
```
USB IRQ → hal.poll()
 ├── SOF → AudioInterface::on_sof()
 │          ├── send_audio_in_now()
 │          └── try_send_feedback() (explicit FB 時のみ)
 ├── RXFLVL → パケット受信 → on_rx()
 └── XFRC → 送信完了

I2S DMA IRQ (750Hz @48kHz/64frames)
 ├── read_audio() → OUT リングバッファ読み出し
 └── write_audio_in() → IN リングバッファ書き込み
```

### stm32f4_synth (旧ターゲット)

プリセット型 `AudioFullDuplexMidi48k` を直接使用:
```cpp
umiusb::Stm32FsHal usb_hal;
umiusb::AudioFullDuplexMidi48k usb_audio;
umiusb::Device<umiusb::Stm32FsHal, decltype(usb_audio)> usb_device(usb_hal, usb_audio, {...});
```

## 7. FIFO 割り当て (STM32 OTG FS, 320 ワード)

```
RxFIFO:   176w @ 0     Audio OUT 294B 受信
TxFIFO0:   24w @ 176   EP0 Control
TxFIFO1:   16w @ 200   MIDI IN
TxFIFO2:    8w @ 216   Feedback 3B (implicit FB 時は未使用)
TxFIFO3:   96w @ 224   Audio IN 294B
合計: 320w ✓
```

## 8. 既知の注意事項

- **macOS Full Speed**: Feedback EP の wMaxPacketSize は 3 以下必須 (xHCI babble error 回避)
- **DWC2 iso IN パリティ**: DSTS.FNSOF の偶奇にパケットパリティを合わせること。不一致時は送信されず EPENA がクリアされない (UAC2_DUPLEX_INVESTIGATION.md 参照)
- **Implicit feedback**: duplex async 時は明示的 FB EP を省略し Audio IN をフィードバックソースとする (Apple TN2274)
- **AudioRingBuffer 重複**: umios/kernel/modules/audio_module.hh にも別の AudioRingBuffer がある (volatile ベース)。umiusb 版 (atomic ベース + ASRC) とは別物
- **umiusb テストなし**: tests/ に umiusb 直接のユニットテストは存在しない
