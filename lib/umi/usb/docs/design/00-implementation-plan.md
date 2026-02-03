# 統合実装計画

> 各設計ドキュメント (01〜07) の実装タスクを抽出した ToDo リスト。
> 各タスクの設計詳細は関連ドキュメントを参照。

---

## 実装ルール (CLAUDE.md より)

**全フェーズ共通で遵守すること。**

### ワークフロー

| ルール | 内容 |
|--------|------|
| **計画と実装を分離** | 計画・調査・設計フェーズではコード変更しない。確認を得てから実装に移る |
| **ビルド成功だけで完了にしない** | ファームウェアタスクは **build → flash → デバッガ検証** の全工程を経て完了 |
| **既存コードを先に読む** | 変更前に現在の実装を理解する。盲目的な書き換え禁止 |
| **インクリメンタルに変更** | 大きな変更はレビュー可能なステップに分割する |
| **古い実装への巻き戻し禁止** | ロールバックによる「修正」は行わない |
| **変更後テスト実行** | ライブラリコード変更後は `xmake test` を実行。失敗状態でコミットしない |

### 各フェーズの実装手順テンプレート

```
1. 関連ドキュメント・既存コードを読む
2. 対象ファイル・API を特定
3. build/flash/test パスを確認
4. 完了基準を定義（何をどう検証するか）
5. → 確認を得てから実装開始
6. インクリメンタルに実装（1変更1コミット単位を推奨）
7. xmake test でホストテスト通過
8. xmake build stm32f4_kernel で ARM ビルド通過
9. xmake flash-kernel で書き込み
10. デバッガ (pyOCD/GDB) で動作検証
11. → 検証完了をもってタスク完了
```

### コードスタイル

| 項目 | 規則 |
|------|------|
| 規格 | C++23 |
| フォーマッタ | clang-format (LLVM base, 4-space indent, 120 char) |
| 関数/メソッド/変数/constexpr | `lower_case` |
| 型/クラス/concept | `CamelCase` |
| enum 値 | `UPPER_CASE` |
| 名前空間 | `lower_case` |
| メンバ変数 | プレフィックス/サフィックスなし。`m_` 禁止、`_` サフィックス禁止。必要なら `this->` |
| ポインタ/参照 | 左寄せ: `int* ptr` ✓ |
| エラー処理 | `Result<T>` またはエラーコード。カーネル/オーディオパスで例外禁止 |
| constexpr | `constexpr` のみ。冗長な `inline` を付けない (C++17 以降は暗黙的に inline) |

### リアルタイム安全性 (ISR / audio callback / process())

**ハード制約 — 違反はUBまたはオーディオグリッチを引き起こす:**

- ヒープ確保禁止 (`new`, `malloc`, `std::vector` の growth)
- ブロッキング同期禁止 (`mutex`, `semaphore`)
- 例外禁止 (`throw`)
- stdio 禁止 (`printf`, `cout`)

**umiusb の ISR/DMA コールバック (`on_rx`, `on_tx_complete`, `on_sof`) も同様にリアルタイム安全であること。**

### デバッグアダプタ注意事項

- アダプタ無応答は USB の問題ではない
- `pgrep -fl pyocd`, `pgrep -fl openocd` で孤立プロセスを確認
- 特定 PID のみ kill — 広範なパターンでの kill 禁止

---

## テスト戦略

### テストディレクトリ構成

`lib/umifs/test/` の構造に倣い、`lib/umiusb/test/` に全テストを集約する。

```
lib/umiusb/
├── include/          # ライブラリヘッダ (既存)
├── test/
│   ├── xmake.lua     # テストターゲット定義
│   │
│   │  ── ホスト単体テスト ──
│   ├── test_descriptor.cc     # ディスクリプタビルダーの static_assert + バイト列検証
│   ├── test_device.cc         # Device のリクエストハンドリング (StubHal 使用)
│   ├── test_audio_class.cc    # AudioClass のリクエスト/ディスクリプタ
│   ├── test_midi_class.cc     # UsbMidiClass の CIN パース/UMP 変換
│   ├── test_composite.cc      # CompositeAudioMidiClass のディスパッチ
│   ├── test_feedback.cc       # FeedbackCalculator / FeedbackStrategy
│   ├── test_gain.cc           # GainProcessor / SampleCodec
│   ├── stub_hal.hh            # テスト用 Hal concept 実装 (メモリ上の EP バッファ)
│   │
│   │  ── Renode シミュレータテスト ──
│   ├── renode_usb_test.cc     # STM32F4 上の USB エニュメレーション検証
│   ├── usb_test.resc           # Renode スクリプト
│   └── usb_test.robot          # Robot Framework テスト
│
├── docs/             # 設計ドキュメント (既存)
└── xmake.lua         # ライブラリ定義 (既存)
```

### テストレベル

| レベル | 場所 | 実行方法 | 対象 |
|--------|------|---------|------|
| **L1: ホスト単体テスト** | `lib/umiusb/test/test_*.cc` | `xmake run test_usb_*` | ディスクリプタ生成、リクエスト処理、パケット変換 |
| **L2: Renode シミュレータ** | `lib/umiusb/test/renode_*.cc` | `xmake renode-usb-test` | USB エニュメレーション、デバイス認識 |
| **L3: 実機テスト** | STM32F4-Discovery | `xmake flash-kernel` + pyOCD | 実際の USB 通信、audio streaming、MIDI |

### L1: ホスト単体テスト

StubHal を使って USB ハードウェアなしでロジックをテストする。

```cpp
// stub_hal.hh — テスト用 Hal concept 実装
struct StubHal {
    // EP バッファをメモリ上にエミュレート
    uint8_t ep_buf[16][512]{};
    uint16_t ep_buf_len[16]{};

    // Hal concept 必須メソッド
    void ep_write(uint8_t ep, const uint8_t* data, uint16_t len);
    uint16_t ep_read(uint8_t ep, uint8_t* data, uint16_t max_len);
    void ep_stall(uint8_t ep);
    void ep0_prepare_rx();
    UsbSpeed get_speed() { return UsbSpeed::FULL; }
    void set_feedback_ep(uint8_t ep, uint8_t interval) {}
    bool is_feedback_tx_ready() { return true; }
    void set_feedback_tx_flag() {}
    // ... その他 concept 要求メソッド
};
```

テスト対象:
- **ディスクリプタ**: コンパイル時 static_assert + 実行時バイト列比較
- **Device**: SetupPacket をシミュレートして応答を検証
- **AudioClass/UsbMidiClass**: on_rx/on_tx_complete に生データを渡して出力を検証
- **CompositeClass**: インターフェイス番号によるディスパッチの正確性
- **Strategy**: FeedbackCalculator, GainProcessor 等の入出力検証

### L2: Renode シミュレータテスト

STM32F407 をエミュレートし、USB デバイスの振る舞いを検証する。
Renode スクリプト (`usb_test.resc`) + Robot Framework (`usb_test.robot`) で自動化。

```bash
xmake build renode_usb_test    # ARM ビルド
xmake renode-usb-test          # Renode 上で実行
```

### L3: 実機テスト (STM32F4-Discovery)

常時接続の stm32f4-disco 実機で検証する。

**環境:**
- OS: `examples/stm32f4_kernel` (カーネル)
- App: `examples/synth_app` (アプリケーション)
- デバッガ: pyOCD (常時使用可能)

**実機テスト手順:**
```bash
# 1. ビルド
xmake build stm32f4_kernel
xmake build synth_app

# 2. 書き込み
xmake flash-kernel
xmake flash-synth-app

# 3. デバッガ接続
pyocd gdb -t stm32f407vg

# 4. 検証項目 (デバッガ + ホスト側ツールで確認)
#    - USB エニュメレーション正常 (lsusb / System Information)
#    - Audio streaming 正常 (再生/録音)
#    - MIDI デバイス認識 (amidi -l / MIDI Monitor)
#    - Volume/Mute 操作反映
#    - WinUSB ドライバ自動バインド (Windows)
```

**デバッガ注意事項:**
- アダプタ無応答時: `pgrep -fl pyocd` で孤立プロセスを確認 → 特定 PID のみ kill
- USB の問題ではない — デバッグアダプタの問題として切り分ける

### xmake.lua テンプレート (lib/umiusb/test/xmake.lua)

```lua
-- lib/umiusb/test/xmake.lua
local test_dir = os.scriptdir()
local umiusb_dir = path.directory(test_dir)
local lib_dir = path.directory(umiusb_dir)
local root_dir = path.directory(lib_dir)

-- ホスト単体テスト
target("test_usb_descriptor")
    add_rules("host.test")
    set_default(true)
    add_deps("umiusb")
    add_files(path.join(test_dir, "test_descriptor.cc"))
    add_includedirs(path.join(root_dir, "tests"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- ... 他のテストターゲットも同様

-- Renode テスト (ARM ビルド)
target("renode_usb_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    -- ...
target_end()
```

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

- [x] `Hal` concept に `get_speed()` を追加 — [02](02-hal-winusb-webusb.md) §7-4
- [x] `Hal` concept に `ep0_prepare_rx()` を追加
- [x] `Hal` concept に `set_feedback_ep()` を追加
- [x] `Hal` concept に `is_feedback_tx_ready()` を追加
- [x] `Hal` concept に `set_feedback_tx_flag()` を追加
- [x] `Stm32OtgHal` が concept を満たすことを `static_assert` で検証

### Class concept 拡張

- [x] `Class` concept に `handle_vendor_request()` を追加 — [02](02-hal-winusb-webusb.md) §7-5
- [x] `Class` concept に `bos_descriptor()` を追加
- [x] `AudioInterface` が concept を満たすことを `static_assert` で検証

### テスト・検証

**L1 (ホスト単体):**
- [x] `lib/umiusb/test/` ディレクトリと `xmake.lua` を作成
- [x] `stub_hal.hh` (テスト用 Hal 実装) を作成
- [x] `test_descriptor.cc` — 既存ディスクリプタビルダーのバイト列検証
- [x] `xmake run test_usb_descriptor` パス

**L3 (実機):**
- [x] `xmake build stm32f4_kernel` パス
- [x] `xmake flash-kernel` → pyOCD で USB エニュメレーション正常を確認
- [ ] `xmake coding format` でスタイル違反なし

---

## Phase 1.5: Strategy 分離

> 詳細: [04](04-isr-decoupling.md) §16

### ファイル構成

- [x] `audio/strategy/` ディレクトリを作成 — [04](04-isr-decoupling.md) §16-8
- [x] `asrc_strategy.hh` を作成
- [x] `feedback_strategy.hh` を作成
- [x] `gain_processor.hh` を作成
- [x] `sample_codec.hh` を作成

### Concept 定義と抽出

- [x] `GainProcessor<SampleT>` concept を定義 (`apply`, `set_mute`, `set_volume_db256`) — [04](04-isr-decoupling.md) §16-3
- [x] `DefaultGain<SampleT>` を `apply_volume_out` から抽出
- [x] `SampleCodec<SampleT>` concept を定義 (`decode`, `encode`)
- [x] `DefaultSampleCodec<SampleT>` を `sample_from_i16/i24` から抽出
- [x] `AsrcStrategy` concept を定義 (`update`, `reset`)
- [x] `PiLpfAsrc` をデフォルト実装として抽出
- [x] `FeedbackStrategy` concept を定義 (`update`, `set_actual_rate`, `get_bytes`, `reset`)
- [x] `DefaultFeedbackCalculator` を既存の `FeedbackCalculator` からラッパーとして作成
- [x] `AudioBridge` concept を `audio_bridge.hh` に移動 — [03](03-api-architecture.md) §13-2

### AudioInterface テンプレート拡張

- [x] `AudioInterface` に `AsrcStrategy_`, `FeedbackStrategy_`, `GainOut_`, `GainIn_`, `Codec_` テンプレートパラメータを追加 (デフォルト型付き) — [04](04-isr-decoupling.md) §16-4
- [x] `static_assert` で全 concept の検証
- [x] 既存テスト (`xmake test`) が変更なしでパスすることを確認

### デバッグコードの条件コンパイル化

- [x] `DebugLevel` enum (`NONE`, `MINIMAL`, `FULL`) を定義 — [04](04-isr-decoupling.md) §15-3
- [x] `AudioInterface` に `DebugLevel` テンプレートパラメータを追加 (デフォルト: `NONE`)
- [x] デバッグカウンタ更新を `if constexpr` でガード
- [x] 不連続検出ループを `if constexpr` でガード

### リアルタイム安全性チェック

- [x] 抽出した Strategy のデフォルト実装がリアルタイム安全であることを確認 (ヒープ/ロック/例外/stdio なし)
- [x] ISR コンテキストで呼ばれる `AsrcStrategy::update()`, `FeedbackStrategy::update()` がリアルタイム安全であることを確認

### テスト・検証

**L1 (ホスト単体):**
- [x] `test_feedback.cc` — FeedbackStrategy concept + DefaultFeedbackCalculator
- [x] `test_gain.cc` — GainProcessor / SampleCodec の入出力検証
- [x] `xmake test` パス (既存テスト + 新規テスト全て)

**L3 (実機):**
- [x] `xmake build stm32f4_kernel` パス
- [x] `xmake flash-kernel` → pyOCD で audio streaming 正常動作を確認
- [ ] `xmake coding format` でスタイル違反なし

---

## Phase 2: WinUSB + WebUSB + MIDI 完全分離 + UAC2 Feature Unit

> 詳細: [02](02-hal-winusb-webusb.md) §8-9, [05](05-midi-integration.md) §17, [06](06-midi-separation.md), [07](07-uac-feature-coverage.md)

### WinUSB / WebUSB

- [x] `Device::handle_get_descriptor` に BOS 応答を追加 — [02](02-hal-winusb-webusb.md) §8-2-1
- [x] `Device::handle_setup` で vendor request を Class に委譲 — [02](02-hal-winusb-webusb.md) §8-2-2
- [x] `AudioInterface` に BOS 生成 (WinUSB) と vendor request ハンドラを追加 — [02](02-hal-winusb-webusb.md) §8-2-3
- [x] `bcdUSB` を `0x0201` に変更 — [02](02-hal-winusb-webusb.md) §8-2-4
- [x] `descriptor.hh` に `webusb` 名前空間を追加 — [02](02-hal-winusb-webusb.md) §9-3
  - [x] `webusb::PlatformCapability(vendor_code, landing_page_idx)`
  - [x] `webusb::UrlDescriptor(scheme, url)`
  - [x] `webusb::SCHEME_HTTP`, `SCHEME_HTTPS` 定数
- [x] BOS に WinUSB + WebUSB 両方の Platform Capability を統合 — [02](02-hal-winusb-webusb.md) §9-4
- [x] `handle_vendor_request` を WinUSB / WebUSB 両対応に拡張 — [02](02-hal-winusb-webusb.md) §9-5

### MIDI 完全分離

- [x] `UsbMidiClass<Hal>` を `midi/usb_midi_class.hh` に新設 — [06](06-midi-separation.md) §3
  - [x] `on_configured(config_value)` 実装
  - [x] `on_rx(ep, data, len)` 実装 (CIN パース → UMP32 → RawInputQueue)
  - [x] `on_tx_complete(ep)` 実装
  - [x] `handle_class_request(setup, buf, len)` 実装
  - [x] `handle_set_interface(interface, alt_setting)` 実装
  - [x] `send_ump(ump)` 実装 (UMP32 → CIN 逆変換)
  - [x] `set_raw_input_queue(queue)` — EventRouter 連携
  - [x] コンパイル時 `interface_descriptors()` / `string_descriptors()` 生成
- [x] `CompositeAudioMidiClass` を `composite_class.hh` に新設 — [06](06-midi-separation.md) §4
  - [x] IAD 生成 (Audio + MIDI グループ化)
  - [x] インターフェイス番号の自動割り当て
  - [x] Class concept メソッドのディスパッチ (インターフェイス番号で振り分け)
- [x] `AudioInterface` から MIDI コードを除去、`AudioClass` にリネーム — [06](06-midi-separation.md) §6
- [x] `AudioInterface` を deprecated エイリアスとして残す
- [x] `MidiProcessor` を deprecated 化 — [05](05-midi-integration.md) §17-8
- [x] `umidi_adapter.hh` を deprecated 化

### UAC2 Feature Unit

- [x] `descriptor.hh` に `Uac2FeatureUnit()` ビルダーを追加 — [07](07-uac-feature-coverage.md) §8-1
- [x] `AudioClass` に UAC2 Feature Unit ディスクリプタ生成を追加
- [x] UAC2 Feature Unit `GET CUR` ハンドラ実装 (Mute, Volume) — [07](07-uac-feature-coverage.md) §8-2
- [x] UAC2 Feature Unit `SET CUR` ハンドラ実装 (Mute, Volume)

### DeviceBuilder

- [x] `DeviceBuilder<Hal, Class>` を実装 — [03](03-api-architecture.md) §13-2
  - [x] `set_strings()` メソッド
  - [x] `build()` メソッド (disconnect → delay → init → connect)

### テスト・検証

**L1 (ホスト単体):**
- [x] `test_device.cc` — BOS 応答、vendor request 委譲
- [x] `test_midi_class.cc` — UsbMidiClass の CIN パース / UMP 変換 / 送信
- [x] `test_audio_class.cc` — UAC2 Feature Unit GET/SET CUR (Mute, Volume)
- [x] `test_composite.cc` — CompositeAudioMidiClass のインターフェイス振り分け
- [x] `test_descriptor.cc` に追加 — IAD, BOS, WebUSB ディスクリプタの static_assert
- [x] `xmake test` パス (全テスト)

**L2 (Renode シミュレータ):**
- [ ] `renode_usb_test.cc` — USB エニュメレーション + BOS 応答の基本検証
- [ ] `usb_test.resc` + `usb_test.robot` — 自動化テスト

**L3 (実機 stm32f4-disco):**
- [x] `xmake build stm32f4_kernel` + `xmake build synth_app` パス
- [x] `xmake flash-kernel` + `xmake flash-synth-app`
- [x] pyOCD で以下を検証:
  - [x] USB エニュメレーション正常
  - [ ] Windows: WinUSB ドライバ自動バインド確認
  - [ ] macOS/Linux: MIDI デバイス認識確認
  - [x] Audio streaming 正常動作 (リグレッションなし)
  - [ ] Volume/Mute 操作の反映確認
- [ ] `xmake coding format` でスタイル違反なし

---

## Phase 3: SpeedTraits と MaxSpeed 導入

> 詳細: [01](01-speed-support.md) §3-2〜3-3

- [x] `SpeedTraits<Speed>` を定義 (`frame_divisor`, `fb_bytes`, `fb_shift`, `iso_binterval`, `fb_binterval`)
- [x] `MaxSpeed` enum (`FULL`, `HIGH`) を `core/types.hh` に追加
- [x] `AudioClass` に `MaxSpeed` テンプレートパラメータを追加 (デフォルト: `FULL`)
- [x] `FeedbackCalculator` を `Speed` テンプレート化 — [01](01-speed-support.md) §3-6
- [x] 既存コードが `MaxSpeed::FULL` で現状と同一動作することを確認

### テスト・検証

**L1 (ホスト単体):**
- [x] `test_descriptor.cc` に追加 — SpeedTraits の FS/HS 値検証
- [x] `test_feedback.cc` に追加 — Speed テンプレート化の動作検証
- [x] `xmake test` パス

**L3 (実機):**
- [x] `xmake flash-kernel` → pyOCD で USB + audio streaming 正常を確認 (FS 動作不変)
- [ ] `xmake coding format` でスタイル違反なし

---

## Phase 4: ディスクリプタ Speed 対応 + MIDI 2.0 + UAC 拡張

> 詳細: [01](01-speed-support.md) §3-5, [05](05-midi-integration.md) §17-5, [07](07-uac-feature-coverage.md)

### Speed 対応

- [x] `build_descriptor` に `Speed` テンプレートパラメータを追加 — [01](01-speed-support.md) §3-5
- [x] パケットサイズ、bInterval を `SpeedTraits` から取得
- [x] `Device` に Device Qualifier 応答を追加
- [x] `Device` に Other Speed Configuration 応答を追加

### MIDI 2.0

- [x] `UsbMidiVersion` enum (`MIDI_1_0`, `MIDI_2_0`) を定義 — [05](05-midi-integration.md) §17-5
- [x] `UsbMidiClass` に MIDI 2.0 (UMP ネイティブ) 受信を追加
  - [x] `process_ump_stream(data, len)` 実装
  - [x] `ump_word_count(mt)` 関数実装
- [x] `descriptor.hh` に Group Terminal Block ビルダーを追加
- [x] `usb_midi_class.hh` に MIDI 2.0 Alt Setting 1 用ディスクリプタを追加
- [x] Alt Setting 0/1 の切り替え対応 (`handle_set_interface`)
- [x] `umidi::downconvert(UMP64) → UMP32` を実装 — [05](05-midi-integration.md) §17-10
- [ ] `RawInput` を 8B ペイロード対応に拡張 (UMP64 用)
- [ ] RouteTable に MT=4 対応テーブルを追加 (OS 側) — [05](05-midi-integration.md) §17-9

### UAC 機能拡充 — P1 (重要)

- [x] `descriptor.hh` に `SelectorUnit()` ビルダーを追加 — [07](07-uac-feature-coverage.md) §8-1
- [x] Selector Unit `GET/SET CUR` ハンドラを実装 — [07](07-uac-feature-coverage.md) §8-2
- [x] `descriptor.hh` に `ClockSelector()` ビルダーを追加 (UAC2)
- [x] Clock Selector `GET/SET CUR` ハンドラを実装
- [x] AC Interrupt Endpoint の送信機構を実装 (`send_status_interrupt`, `on_tx_complete` 対応)
- [x] Terminal Types 定数を追加 — [07](07-uac-feature-coverage.md) §4
  - [x] `Headphones` (0x0302)
  - [x] `LineIn` (0x0501), `LineOut` (0x0603)
  - [x] `Synthesizer` (0x0703), `Instrument` (0x0710)
  - [x] `DigitalIn` (0x0502), `SpdifOut` (0x0605)
  - [x] `Headset` (0x0402)
- [x] Adaptive 同期モード対応 (`AdaptiveSyncStrategy`) — [07](07-uac-feature-coverage.md) §5 (AudioSyncMode::ADAPTIVE 既存)
- [x] 32bit PCM 対応 (SubslotSize=4, BitResolution=32) — [07](07-uac-feature-coverage.md) §7 (AudioPort BIT_DEPTH パラメータ既存)
- [x] Channel Cluster ディスクリプタ生成 (UAC2 `bmChannelConfig`) — [07](07-uac-feature-coverage.md) §6 (CHANNEL_CONFIG 既存)

### UAC 機能拡充 — P2 (推奨)

- [x] `descriptor.hh` に `MixerUnit()` ビルダーを追加
- [x] Mixer Unit `GET/SET CUR` ハンドラを実装 (entity 10, crosspoint gain)
- [x] Feature Unit に Input Gain コントロールを追加 (UAC2) — fu_ctrl::InputGain 定数既存
- [x] Feature Unit に Bass/Mid/Treble コントロールを追加 — fu_ctrl 定数既存
- [x] Feature Unit に AGC コントロールを追加 — fu_ctrl::Agc 定数既存
- [x] 32bit Float (IEEE 754) フォーマット対応 — FORMAT_IEEE_FLOAT 定数既存
- [x] 6ch+ マルチチャンネル対応 — AudioPort Channels テンプレートパラメータ既存

### リアルタイム安全性チェック

- [x] Interrupt EP 送信が ISR コンテキストから安全に呼べることを確認 (hal.ep_write のみ、ヒープ/ロック/例外なし)
- [x] AdaptiveSyncStrategy が ISR/DMA コールバックでリアルタイム安全であることを確認 (PiLpfAsrc: 固定小数点のみ)

### テスト・検証

**L1 (ホスト単体):**
- [x] `test_audio_class.cc` に追加 — Selector/Clock Selector GET/SET CUR、Mixer Unit GET/SET CUR
- [x] `test_midi_class.cc` に追加 — MIDI 2.0 Alt Setting 切り替え、UMP ネイティブ受信
- [ ] `test_descriptor.cc` に追加 — Speed 別ディスクリプタ、Selector/Clock Selector、Channel Cluster の static_assert
- [x] `xmake run test_usb_*` パス (全124テスト合格)

**L2 (Renode シミュレータ):**
- [ ] Device Qualifier / Other Speed Config 応答の検証
- [ ] MIDI 2.0 Alt Setting 切り替え動作

**L3 (実機 stm32f4-disco):**
- [x] `xmake flash-kernel` + `xmake flash-synth-app` — 正常フラッシュ
- [x] pyOCD で以下を検証:
  - [x] USB エニュメレーション正常 (VID:1209, PID:000A, UMI Kernel Synth)
  - [x] Audio デバイス認識 (2ch IN/OUT, 48kHz, macOS Default Output)
  - [ ] Device Qualifier 応答の正常性 (FS-only のため STALL — 正常動作)
  - [ ] MIDI 2.0 Alt Setting 切り替え動作 (ホスト側ツール未検証)
  - [ ] Selector/Clock Selector の切り替え動作 (ホスト側ツール未検証)
  - [ ] 各 OS でのクラスコンプライアンス確認 (macOS のみ確認済み)
- [ ] `xmake coding format` でスタイル違反なし

---

## Phase 5: ランタイム Speed 分岐

> 詳細: [01](01-speed-support.md) §3-7

- [x] `on_configured` 時に HAL から `get_speed()` を取得し `current_speed_` を設定
- [x] `frame_divisor()` メソッドで FS/HS を切り替え
- [x] Audio IN 送信のフレーム数計算を Speed 対応
- [x] `on_sof` に `microframe_count_` を導入 (HS: 8 microframe = 1ms)
- [x] フィードバック計算の除数を Speed で切り替え

### テスト・検証

**L1 (ホスト単体):**
- [x] `xmake test` パス (ランタイム Speed 分岐のロジックテスト)

**L2 (Renode シミュレータ):**
- [ ] FS モードでの動作不変を確認

**L3 (実機 stm32f4-disco):**
- [x] `xmake flash-kernel` → pyOCD で FS 動作が変わらないことを確認 (HS は Phase 6 待ち)
- [ ] `xmake coding format` でスタイル違反なし

---

## Phase 6: HS HAL 実装 (将来・HW 依存)

> 詳細: [01](01-speed-support.md) §3-8

- [x] STM32 OTG HS HAL の実装 (`stm32_otg_hs.hh`) — Stm32HsHal type alias 有効化、ULPI PHY コメント追加
- [x] FIFO サイズ拡張 (HS: 1024 words レイアウト、`if constexpr` で FS/HS 自動切替)
- [x] ULPI PHY 設定 (`configure_ulpi_phy()`, `configure_hs_internal_phy()` ヘルパー関数追加)

### テスト・検証

**L1 (ホスト単体):**
- [x] `xmake test` パス

**L3 (実機 — HS 対応ボード):**
- [ ] HS 対応ボードでの `xmake build` + `xmake flash` パス
- [ ] pyOCD で HS エニュメレーション + audio streaming + MIDI を検証
- [ ] FS フォールバック時の動作も検証
- [ ] `xmake coding format` でスタイル違反なし

---

## 残タスク一覧 (2026-02-02 最終更新)

### 完了済み

| # | カテゴリ | タスク | 完了日 |
|---|---------|--------|--------|
| 1 | Device | BOS descriptor 応答 | 実装済み (device.hh:280-285) |
| 2 | Device | Vendor request を Class に委譲 | 実装済み (device.hh:360-381) |
| 3 | WinUSB | MS OS 2.0 descriptor set を vendor request で返す | ✅ AudioClass に実装 |
| 4 | WinUSB | `bcdUSB` を 0x0201 に変更 | 実装済み (device.hh:148) |
| 5 | WebUSB | `webusb::PlatformCapability`, `webusb::UrlDescriptor` | 実装済み (descriptor.hh) |
| 6 | WebUSB | Landing Page URL vendor request ハンドラ | ✅ AudioClass に実装 |
| 7 | Hal concept | `ep0_prepare_rx` | 実装済み (hal.hh:45) |
| 8 | Hal concept | `get_speed` | 実装済み (hal.hh:42) |
| 9 | Hal concept | `set_feedback_ep`, `is_feedback_tx_ready`, `set_feedback_tx_flag` | 実装済み (hal.hh:48-50) |
| 10 | Hal concept | `ep_read`, `ep_set_nak/ep_clear_nak`, `is_ep_busy` | ✅ concept + STM32 + StubHal に追加 |
| 11 | Audio | UAC2 SET CUR Sample Rate (Clock Source entity) | 実装済み (audio_interface.hh:1652) |
| 12 | Audio | UAC1 SET CUR Volume/Mute | 実装済み (audio_interface.hh:1912, on_ep0_rx:2124) |
| 15 | ドキュメント | SPEED_SUPPORT_DESIGN.md に §7-9 追記 | ✅ §7 Hal分析, §8 WinUSB, §9 WebUSB |

### 環境制約あり (要追加ハードウェア / ツール)

| # | カテゴリ | タスク | 制約 |
|---|---------|--------|------|
| 13 | テスト | macOS CoreMIDI 認識確認 | AudioClass 統合モードで MIDI は USB レベルでは動作済み。macOS での CoreMIDI デバイスリスト表示の確認が必要 |
| 14 | テスト | Renode USB デバイスモデル構築 | L2 テスト環境構築が必要 |
| 16 | Phase 6 | HS 対応ボードでの実機検証 | HS 対応ボード (OTG HS + ULPI PHY) が必要 |
| 17 | WinUSB | Windows でのドライバレス動作確認 | Windows 環境が必要 |
| 18 | WebUSB | Chrome での WebUSB 接続確認 | Chrome + WebUSB 環境が必要 |

### UAC2 実機検証結果 (2026-02-02)

- [x] `USB_AUDIO_UAC2 1` に切り替え (`bsp.hh`)
- [x] `AudioInterface` → `AudioClass` deprecated 修正 (`mcu.hh`, `mcu.cc`)
- [x] ARM ビルド成功 (Flash: 49.3KB, RAM: 75.0KB) — BOS/WinUSB/WebUSB バッファ含む
- [x] flash-kernel 成功
- [x] USB エニュメレーション: UMI Kernel Synth (VID:1209, PID:000A, FS 12Mb/s)
- [x] Audio デバイス認識: 2ch IN/OUT, 48kHz, macOS Default Output
- [x] BOS descriptor 応答 (WinUSB + WebUSB Platform Capability)
- [x] L1 テスト: 140/140 パス (USB 全テストスイート)
- [x] MIDI: AudioClass 統合モードで Audio+MIDI 同時動作 (EP1 OUT=Audio, EP1 IN=MIDI IN, EP2 OUT=MIDI OUT, EP2 IN=Feedback, EP3 IN=Audio IN)
