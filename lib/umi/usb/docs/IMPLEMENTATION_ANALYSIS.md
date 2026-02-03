# umiusb 実装分析

## 概要

umiusb は UMI フレームワークの USB ドライバライブラリで、USB Audio Class (UAC1/UAC2) および USB MIDI に特化した header-only 実装。C++20 concepts による静的多態性を全面採用し、vtable オーバーヘッドなしでリアルタイムオーディオ処理に適した zero-overhead 抽象化を実現している。

---

## アーキテクチャ

### レイヤー構造

```
┌─────────────────────────────────────┐
│  Application (stm32f4_kernel)       │
│  DMA コールバック、SOF ハンドラ       │
└──────────────┬──────────────────────┘
┌──────────────▼──────────────────────┐
│  USB Class Layer                    │
│  AudioInterface<UAC1/UAC2>          │
│  ディスクリプタ生成・クラスリクエスト  │
│  リングバッファ・フィードバック・MIDI  │
└──────────────┬──────────────────────┘
┌──────────────▼──────────────────────┐
│  USB Device Core (Device<Hal,Class>)│
│  標準リクエスト・EP0 制御転送        │
└──────────────┬──────────────────────┘
┌──────────────▼──────────────────────┐
│  HAL Layer (Hal concept)            │
│  Stm32OtgHal - レジスタ・FIFO 操作  │
└──────────────┬──────────────────────┘
┌──────────────▼──────────────────────┐
│  STM32 OTG FS Hardware              │
└─────────────────────────────────────┘
```

各レイヤー間は C++20 concepts で接続され、コンパイル時に型チェックが完了する。

### データフロー

**Audio OUT (Host → Device):**
```
Host → USB EP (Iso OUT) → AudioRingBuffer → DMA 半完了コールバック → DAC
```

**Audio IN (Device → Host):**
```
ADC/シンセ合成 → AudioRingBuffer → SOF ハンドラ (1ms) → USB EP (Iso IN) → Host
```

**フィードバック (Async モード):**
```
SOF 割り込み → FeedbackCalculator (バッファレベル → PI 制御) → Feedback EP → Host
```

---

## 主要コンポーネント

### コア層 (core/)

#### Hal Concept (`core/hal.hh`)

HAL 実装が満たすべきインタフェースを concept で定義。要求メソッド:

| メソッド | 役割 |
|---------|------|
| `init()` | ハードウェア初期化 |
| `connect()` / `disconnect()` | USB 接続制御 |
| `set_address()` | USB アドレス設定 |
| `ep_configure()` | エンドポイント構成 |
| `ep_write()` | エンドポイント書き込み |
| `ep_stall()` | STALL 応答 |
| `poll()` | イベントポーリング |

`HalBase<Derived>` が CRTP でアドレス管理・接続状態・イベント通知のヘルパーを提供。

#### Device テンプレート (`core/device.hh`)

```cpp
template<Hal HalT, Class ClassT>
class Device;
```

USB 標準リクエスト (GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION 等) を処理し、クラス固有リクエストを `ClassT` に委譲する。EP0 制御転送の SETUP → DATA → STATUS 段階を完全実装。

#### Descriptor Builder (`core/descriptor.hh`)

`constexpr` 関数でコンパイル時に USB ディスクリプタを生成。`Bytes<N>` テンプレートでバイト列を構築し、`DeviceDesc`, `ConfigHeader`, `InterfaceDesc`, `EndpointDesc` 等の構造体を提供。WinUSB (MS OS 2.0) ディスクリプタもサポート。String descriptor は UTF-16LE をコンパイル時に生成。

### Audio 層 (audio/)

#### AudioInterface (`audio/audio_interface.hh`, ~3100 行)

ライブラリの中核。Policy-based design により柔軟な構成を実現:

```cpp
template <UacVersion Version = UacVersion::UAC1,
          typename AudioOut_ = AudioStereo48k,
          typename AudioIn_ = NoAudioPort,
          typename MidiOut_ = NoMidiPort,
          typename MidiIn_ = NoMidiPort,
          uint8_t FeedbackEp_ = 2,
          AudioSyncMode SyncMode_ = AudioSyncMode::ASYNC,
          bool SampleRateControlEnabled_ = true,
          typename SampleT_ = int32_t>
class AudioInterface;
```

- `AudioPort<Ch, Bits, Rate, Ep>` / `MidiPort<Ep>` でポートを構成
- `NoAudioPort` / `NoMidiPort` を指定するとコンパイル時に該当機能が除外される
- ディスクリプタ生成、クラスリクエスト処理、リングバッファ・フィードバック・MIDI パケット処理を統合管理

#### AudioRingBuffer (`audio/audio_types.hh`)

```cpp
template <uint32_t Frames = 256, uint8_t Channels = 2, typename SampleT = int32_t>
class AudioRingBuffer;
```

USB ISR と Audio DMA 間を安全にブリッジするロックフリー SPSC (Single Producer Single Consumer) リングバッファ。

- `std::atomic` + `memory_order_acquire/release` によるメモリオーダリング保証
- Cubic Hermite 補間による ASRC 対応 (`read_interpolated()`)
- バッファレベル監視機能

#### FeedbackCalculator (`audio/audio_types.hh`)

Asynchronous モード用のフィードバック値計算器。

- Full Speed: 10.14 固定小数点形式 (3 バイト)
- バッファレベルから PI 制御 (Kp=2, Ki=0.02) でフィードバック値を算出
- ホストがパケットサイズを調整し、バッファを 50% に維持
- ±1000ppm の調整範囲

詳細: [ASRC_DESIGN.md](ASRC_DESIGN.md)

#### MidiProcessor (`audio/audio_types.hh`)

USB MIDI パケットの送受信処理。CIN (Code Index Number) によるメッセージ分類、SysEx 対応 (256 バイトバッファ)。

### MIDI アダプタ (midi/)

#### umidi_adapter.hh

UMI の MIDI システム (UMP: Universal MIDI Packet) と USB MIDI 1.0 パケット間の変換を担当。

### HAL 実装 (hal/)

#### Stm32OtgHal (`hal/stm32_otg.hh`, ~1166 行)

```cpp
template <uint32_t BaseAddr = 0x50000000, uint8_t MaxEndpoints = 4>
class Stm32OtgHal : public HalBase<Stm32OtgHal<BaseAddr, MaxEndpoints>>;
```

STM32 OTG FS レジスタを直接操作する HAL 実装。

- FIFO 割り当て: RX 176w, TX0 24w, TX1 16w, TX2 8w, TX3 96w (計 320 ワード)
- Isochronous 転送のフレームパリティ制御 (DWC2 バグ対応済み)
- Bulk / Interrupt 転送対応
- デバッグカウンタ 30 個以上

---

## STM32F4 カーネルへの統合

`examples/stm32f4_kernel/` での使用パターン:

### インスタンス構成 (`mcu.cc`)

```cpp
// HAL
umiusb::Stm32FsHal usb_hal_inst;

// AudioInterface (UAC1/UAC2 は bsp.hh のマクロで切替)
UsbAudioDevice usb_audio_inst;

// Device
umiusb::Device<umiusb::Stm32FsHal, UsbAudioDevice> usb_device(
    usb_hal_inst, usb_audio_inst, {...});
```

### 初期化フロー (`init_usb()`)

1. `usb_hal_inst.disconnect()` — 既存接続をクリア
2. 50ms 遅延 — macOS の列挙解除待ち
3. シリアル番号生成 — STM32 UID から
4. `usb_device.init()` — Device 初期化、HAL コールバック設定
5. `usb_hal_inst.connect()` — USB 接続開始
6. NVIC 割り込み有効化

### ランタイム統合 (`kernel.cc`)

- **Audio OUT**: DMA 半完了コールバックでリングバッファから読み出し → DAC
- **Audio IN**: ADC/合成結果をリングバッファへ書き込み → SOF ハンドラで USB 送信
- **SOF ハンドラ (1ms 毎)**: フィードバック値送信、Audio IN 送信トリガ
- **USB 割り込み**: `usb_device.poll()` を呼び出し

---

## 設計パターン

| パターン | 適用箇所 | 効果 |
|---------|---------|------|
| **C++20 Concepts** | Hal, Class concept | vtable なし静的多態性 |
| **CRTP** | `HalBase<Derived>` | 共通機能の提供、仮想関数なし |
| **Policy-based Design** | `AudioInterface` テンプレートパラメータ | 構成の柔軟性とコンパイル時最適化 |
| **constexpr 計算** | ディスクリプタビルダー | ランタイムオーバーヘッドゼロ |
| **Lock-free SPSC** | `AudioRingBuffer` | ISR-DMA 間の安全な受け渡し |
| **PI 制御** | `FeedbackCalculator` | Async モードのクロック同期 |

---

## 実装状況

### 実装済み

| 機能 | 詳細 |
|------|------|
| USB 2.0 Full Speed | EP0 制御転送、標準リクエスト完全実装 |
| UAC1 / UAC2 | Audio OUT/IN、Full duplex 対応 |
| Async / Adaptive / Sync モード | Explicit feedback EP、Implicit feedback (UAC2 duplex) |
| ASRC | Cubic Hermite 補間 + PI 制御 |
| USB MIDI 1.0 | SysEx 対応、umidi (UMP) 統合 |
| サンプルレート切替 | 44.1kHz, 48kHz, 96kHz 等 |
| Feature Unit | Mute, Volume 制御 |
| STM32 OTG FS HAL | Iso 転送フレームパリティ修正済み |
| コンパイル時ディスクリプタ | static_assert による検証 |
| WinUSB (MS OS 2.0) | ディスクリプタサポート |

### 未実装・制限事項

| 項目 | 状態 |
|------|------|
| High Speed (480Mbps) | コードあり、未テスト |
| HID / CDC / Mass Storage | 未実装 (Audio/MIDI のみ) |
| 他プラットフォーム HAL | ESP32 等は未実装 |
| OTG dual-role | Device モードのみ |
| 最大エンドポイント数 | 4 (OTG FS 制約) |
| FIFO サイズ | 320 ワード (OTG FS 制約) |

---

## 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [UMIUSB_REFERENCE.md](UMIUSB_REFERENCE.md) | ファイルツリー、全型一覧、データフロー図 |
| [ASRC_DESIGN.md](ASRC_DESIGN.md) | PI 制御ベース ASRC 設計、パラメータ選定根拠 |
| [UAC2_DUPLEX_INVESTIGATION.md](UAC2_DUPLEX_INVESTIGATION.md) | UAC2 全二重問題の調査・修正報告 |
