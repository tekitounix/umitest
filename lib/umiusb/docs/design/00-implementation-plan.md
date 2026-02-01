# 統合実装計画

> 各設計ドキュメント (01〜07) の実装タスクを抽出した ToDo リスト。
> 各タスクの設計詳細は関連ドキュメントを参照。

---

## フェーズ一覧

| Phase | 内容 | 関連ドキュメント | 破壊的変更 |
|-------|------|----------------|-----------|
| 1 | Hal concept + Class concept の正規化 | [01](01-speed-support.md), [02](02-hal-winusb-webusb.md) | なし |
| 1.5 | Strategy 分離 (ASRC, Feedback, Gain, Codec) | [04](04-isr-decoupling.md) | なし (デフォルト型で後方互換) |
| 2 | WinUSB + WebUSB + MIDI 完全分離 + UAC2 FU | [02](02-hal-winusb-webusb.md), [05](05-midi-integration.md), [06](06-midi-separation.md), [07](07-uac-feature-coverage.md) | MIDI API 変更 |
| 3 | SpeedTraits と MaxSpeed 導入 | [01](01-speed-support.md) | なし (デフォルト FULL) |
| 4 | ディスクリプタ Speed 対応 + MIDI 2.0 + UAC 拡張 | [01](01-speed-support.md), [05](05-midi-integration.md), [07](07-uac-feature-coverage.md) | なし |
| 5 | ランタイム Speed 分岐 | [01](01-speed-support.md) | なし |
| 6 | HS HAL 実装 (将来・HW 依存) | [01](01-speed-support.md) | なし |

---

## Phase 1: Hal concept + Class concept の正規化

> 詳細: [01](01-speed-support.md) §5, [02](02-hal-winusb-webusb.md) §7

### Hal concept 拡張

- [ ] `Hal` concept に `get_speed()` を追加 — [02](02-hal-winusb-webusb.md) §7-4
- [ ] `Hal` concept に `ep0_prepare_rx()` を追加
- [ ] `Hal` concept に `set_feedback_ep()` を追加
- [ ] `Hal` concept に `is_feedback_tx_ready()` を追加
- [ ] `Hal` concept に `set_feedback_tx_flag()` を追加
- [ ] `Stm32OtgHal` が concept を満たすことを `static_assert` で検証

### Class concept 拡張

- [ ] `Class` concept に `handle_vendor_request()` を追加 — [02](02-hal-winusb-webusb.md) §7-5
- [ ] `Class` concept に `bos_descriptor()` を追加
- [ ] `AudioInterface` が concept を満たすことを `static_assert` で検証

---

## Phase 1.5: Strategy 分離

> 詳細: [04](04-isr-decoupling.md) §16

### ファイル構成

- [ ] `audio/strategy/` ディレクトリを作成 — [04](04-isr-decoupling.md) §16-8
- [ ] `asrc_strategy.hh` を作成
- [ ] `feedback_strategy.hh` を作成
- [ ] `gain_processor.hh` を作成
- [ ] `sample_codec.hh` を作成

### Concept 定義と抽出

- [ ] `GainProcessor<SampleT>` concept を定義 (`apply`, `set_mute`, `set_volume_db256`) — [04](04-isr-decoupling.md) §16-3
- [ ] `DefaultGain<SampleT>` を `apply_volume_out` から抽出
- [ ] `SampleCodec<SampleT>` concept を定義 (`decode`, `encode`)
- [ ] `DefaultSampleCodec<SampleT>` を `sample_from_i16/i24` から抽出
- [ ] `AsrcStrategy` concept を定義 (`update`, `reset`)
- [ ] `PiLpfAsrc` をデフォルト実装として抽出
- [ ] `FeedbackStrategy` concept を定義 (`update`, `set_actual_rate`, `get_bytes`, `reset`)
- [ ] `DefaultFeedbackCalculator` を既存の `FeedbackCalculator` からラッパーとして作成
- [ ] `AudioBridge` concept を `audio_bridge.hh` に移動 — [03](03-api-architecture.md) §13-2

### AudioInterface テンプレート拡張

- [ ] `AudioInterface` に `AsrcStrategy_`, `FeedbackStrategy_`, `GainOut_`, `GainIn_`, `Codec_` テンプレートパラメータを追加 (デフォルト型付き) — [04](04-isr-decoupling.md) §16-4
- [ ] `static_assert` で全 concept の検証
- [ ] 既存テスト (`xmake test`) が変更なしでパスすることを確認

### デバッグコードの条件コンパイル化

- [ ] `DebugLevel` enum (`NONE`, `MINIMAL`, `FULL`) を定義 — [04](04-isr-decoupling.md) §15-3
- [ ] `AudioInterface` に `DebugLevel` テンプレートパラメータを追加 (デフォルト: `NONE`)
- [ ] デバッグカウンタ更新を `if constexpr` でガード
- [ ] 不連続検出ループを `if constexpr` でガード

---

## Phase 2: WinUSB + WebUSB + MIDI 完全分離 + UAC2 Feature Unit

> 詳細: [02](02-hal-winusb-webusb.md) §8-9, [05](05-midi-integration.md) §17, [06](06-midi-separation.md), [07](07-uac-feature-coverage.md)

### WinUSB / WebUSB

- [ ] `Device::handle_get_descriptor` に BOS 応答を追加 — [02](02-hal-winusb-webusb.md) §8-2-1
- [ ] `Device::handle_setup` で vendor request を Class に委譲 — [02](02-hal-winusb-webusb.md) §8-2-2
- [ ] `AudioInterface` に BOS 生成 (WinUSB) と vendor request ハンドラを追加 — [02](02-hal-winusb-webusb.md) §8-2-3
- [ ] `bcdUSB` を `0x0201` に変更 — [02](02-hal-winusb-webusb.md) §8-2-4
- [ ] `descriptor.hh` に `webusb` 名前空間を追加 — [02](02-hal-winusb-webusb.md) §9-3
  - [ ] `webusb::PlatformCapability(vendor_code, landing_page_idx)`
  - [ ] `webusb::UrlDescriptor(scheme, url)`
  - [ ] `webusb::SCHEME_HTTP`, `SCHEME_HTTPS` 定数
- [ ] BOS に WinUSB + WebUSB 両方の Platform Capability を統合 — [02](02-hal-winusb-webusb.md) §9-4
- [ ] `handle_vendor_request` を WinUSB / WebUSB 両対応に拡張 — [02](02-hal-winusb-webusb.md) §9-5

### MIDI 完全分離

- [ ] `UsbMidiClass<Hal>` を `midi/usb_midi_class.hh` に新設 — [06](06-midi-separation.md) §3
  - [ ] `on_configured(config_value)` 実装
  - [ ] `on_rx(ep, data, len)` 実装 (CIN パース → UMP32 → RawInputQueue)
  - [ ] `on_tx_complete(ep)` 実装
  - [ ] `handle_class_request(setup, buf, len)` 実装
  - [ ] `handle_set_interface(interface, alt_setting)` 実装
  - [ ] `send_ump(ump)` 実装 (UMP32 → CIN 逆変換)
  - [ ] `set_raw_input_queue(queue)` — EventRouter 連携
  - [ ] コンパイル時 `interface_descriptors()` / `string_descriptors()` 生成
- [ ] `CompositeAudioMidiClass` を `composite_class.hh` に新設 — [06](06-midi-separation.md) §4
  - [ ] IAD 生成 (Audio + MIDI グループ化)
  - [ ] インターフェイス番号の自動割り当て
  - [ ] Class concept メソッドのディスパッチ (インターフェイス番号で振り分け)
- [ ] `AudioInterface` から MIDI コードを除去、`AudioClass` にリネーム — [06](06-midi-separation.md) §6
- [ ] `AudioInterface` を deprecated エイリアスとして残す
- [ ] `MidiProcessor` を deprecated 化 — [05](05-midi-integration.md) §17-8
- [ ] `umidi_adapter.hh` を deprecated 化

### UAC2 Feature Unit

- [ ] `descriptor.hh` に `Uac2FeatureUnit()` ビルダーを追加 — [07](07-uac-feature-coverage.md) §8-1
- [ ] `AudioClass` に UAC2 Feature Unit ディスクリプタ生成を追加
- [ ] UAC2 Feature Unit `GET CUR` ハンドラ実装 (Mute, Volume) — [07](07-uac-feature-coverage.md) §8-2
- [ ] UAC2 Feature Unit `SET CUR` ハンドラ実装 (Mute, Volume)

### DeviceBuilder

- [ ] `DeviceBuilder<Hal, Class>` を実装 — [03](03-api-architecture.md) §13-2
  - [ ] `set_strings()` メソッド
  - [ ] `build()` メソッド (disconnect → delay → init → connect)

### テスト

- [ ] UsbMidiClass 単体の送受信テスト
- [ ] CompositeAudioMidiClass のリクエスト委譲テスト
- [ ] UAC2 Feature Unit Volume/Mute の GET/SET テスト
- [ ] IAD + AC Header のバイト列 static_assert
- [ ] 実機テスト (各 OS での MIDI デバイス認識 + ドライバレス動作)

---

## Phase 3: SpeedTraits と MaxSpeed 導入

> 詳細: [01](01-speed-support.md) §3-2〜3-3

- [ ] `SpeedTraits<Speed>` を定義 (`frame_divisor`, `fb_bytes`, `fb_shift`, `iso_binterval`, `fb_binterval`)
- [ ] `MaxSpeed` enum (`FULL`, `HIGH`) を `core/types.hh` に追加
- [ ] `AudioClass` に `MaxSpeed` テンプレートパラメータを追加 (デフォルト: `FULL`)
- [ ] `FeedbackCalculator` を `Speed` テンプレート化 — [01](01-speed-support.md) §3-6
- [ ] 既存コードが `MaxSpeed::FULL` で現状と同一動作することを確認

---

## Phase 4: ディスクリプタ Speed 対応 + MIDI 2.0 + UAC 拡張

> 詳細: [01](01-speed-support.md) §3-5, [05](05-midi-integration.md) §17-5, [07](07-uac-feature-coverage.md)

### Speed 対応

- [ ] `build_descriptor` に `Speed` テンプレートパラメータを追加 — [01](01-speed-support.md) §3-5
- [ ] パケットサイズ、bInterval を `SpeedTraits` から取得
- [ ] `Device` に Device Qualifier 応答を追加
- [ ] `Device` に Other Speed Configuration 応答を追加

### MIDI 2.0

- [ ] `UsbMidiVersion` enum (`MIDI_1_0`, `MIDI_2_0`) を定義 — [05](05-midi-integration.md) §17-5
- [ ] `UsbMidiClass` に MIDI 2.0 (UMP ネイティブ) 受信を追加
  - [ ] `process_ump_stream(data, len)` 実装
  - [ ] `ump_word_count(mt)` 関数実装
- [ ] `descriptor.hh` に Group Terminal Block ビルダーを追加
- [ ] `descriptor.hh` に MIDI 2.0 Alt Setting 1 用ディスクリプタを追加
- [ ] Alt Setting 0/1 の切り替え対応 (`handle_set_interface`)
- [ ] `umidi::downconvert(UMP64) → UMP32` を実装 — [05](05-midi-integration.md) §17-10
- [ ] `RawInput` を 8B ペイロード対応に拡張 (UMP64 用)
- [ ] RouteTable に MT=4 対応テーブルを追加 (OS 側) — [05](05-midi-integration.md) §17-9

### UAC 機能拡充 — P1 (重要)

- [ ] `descriptor.hh` に `SelectorUnit()` ビルダーを追加 — [07](07-uac-feature-coverage.md) §8-1
- [ ] Selector Unit `GET/SET CUR` ハンドラを実装 — [07](07-uac-feature-coverage.md) §8-2
- [ ] `descriptor.hh` に `ClockSelector()` ビルダーを追加 (UAC2)
- [ ] Clock Selector `GET/SET CUR` ハンドラを実装
- [ ] AC Interrupt Endpoint のディスクリプタ + 送信機構を実装
- [ ] Terminal Types 定数を追加 — [07](07-uac-feature-coverage.md) §4
  - [ ] `Headphones` (0x0302)
  - [ ] `LineIn` (0x0501), `LineOut` (0x0603)
  - [ ] `Synthesizer` (0x0703), `Instrument` (0x0710)
  - [ ] `DigitalIn` (0x0502), `SpdifOut` (0x0605)
  - [ ] `Headset` (0x0402)
- [ ] Adaptive 同期モード対応 (`AdaptiveSyncStrategy`) — [07](07-uac-feature-coverage.md) §5
- [ ] 32bit PCM 対応 (SubslotSize=4, BitResolution=32) — [07](07-uac-feature-coverage.md) §7
- [ ] Channel Cluster ディスクリプタ生成 (UAC2 `bmChannelConfig`) — [07](07-uac-feature-coverage.md) §6

### UAC 機能拡充 — P2 (推奨)

- [ ] `descriptor.hh` に `MixerUnit()` ビルダーを追加
- [ ] Mixer Unit `GET/SET CUR` ハンドラを実装
- [ ] Feature Unit に Input Gain コントロールを追加 (UAC2)
- [ ] Feature Unit に Bass/Mid/Treble コントロールを追加
- [ ] Feature Unit に AGC コントロールを追加
- [ ] 32bit Float (IEEE 754) フォーマット対応
- [ ] 6ch+ マルチチャンネル対応

### テスト

- [ ] Speed 対応ディスクリプタの static_assert
- [ ] MIDI 2.0 Alt Setting 切り替えテスト
- [ ] Selector/Clock Selector 切り替えテスト
- [ ] Interrupt EP 通知送信テスト
- [ ] 新規ディスクリプタビルダーの static_assert
- [ ] 実機テスト (各 OS でのクラスコンプライアンス確認)

---

## Phase 5: ランタイム Speed 分岐

> 詳細: [01](01-speed-support.md) §3-7

- [ ] `on_configured` 時に HAL から `get_speed()` を取得し `current_speed_` を設定
- [ ] `frame_divisor()` メソッドで FS/HS を切り替え
- [ ] Audio IN 送信のフレーム数計算を Speed 対応
- [ ] `on_sof` に `microframe_count_` を導入 (HS: 8 microframe = 1ms)
- [ ] フィードバック計算の除数を Speed で切り替え

---

## Phase 6: HS HAL 実装 (将来・HW 依存)

> 詳細: [01](01-speed-support.md) §3-8

- [ ] STM32 OTG HS HAL の実装 (`stm32_otg_hs.hh`)
- [ ] FIFO サイズ拡張
- [ ] ULPI PHY 設定
- [ ] 実機テスト
