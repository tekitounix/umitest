# 01 — HW 分離分析と High Speed 対応設計

> 元ドキュメント: [SPEED_SUPPORT_DESIGN.md](../SPEED_SUPPORT_DESIGN.md) セクション 1-6

## 概要

umiusb は C++20 concepts による静的多態性で HW 依存層（HAL）と非依存層（Device, AudioInterface）を
分離する設計だが、現状は **Full Speed 専用**であり、層間の分離にも漏れがある。
本ドキュメントでは現状の問題を分析し、High Speed 対応の設計案を示す。

## 評価サマリ

| 観点 | 評価 | 備考 |
|------|------|------|
| HW 依存/非依存の分離 | **不完全** | Hal concept に漏れ、AudioInterface が HAL 固有メソッドを直接呼出 |
| USB 速度対応 | **FS のみ** | パケットサイズ、フィードバック形式、タイミングすべて 1ms フレーム固定 |
| FS のみ使用時のコスト | **最適** | 全て constexpr、ランタイムオーバーヘッドなし |
| HS 対応の拡張性 | **設計済み未実装** | Speed enum、HalBase に speed_ フィールドあり |

---

## 1. HW 依存/非依存層の分離: 現状分析

### 1-1. 層構成

```
非依存層   core/types.hh          USB プロトコル型定義
           core/descriptor.hh     コンパイル時ディスクリプタ生成
           core/device.hh         Device<Hal, Class> — 標準リクエスト処理
           audio/audio_types.hh   RingBuffer, FeedbackCalculator, MidiProcessor
           audio/audio_interface.hh  AudioInterface — USB Audio Class 実装

依存層     core/hal.hh            Hal concept 定義、HalBase CRTP
           hal/stm32_otg.hh       STM32 OTG FS HAL 実装
```

### 1-2. Hal concept の定義と漏れ

`core/hal.hh` で定義されている Hal concept:

```cpp
concept Hal = requires(T& hal, ...) {
    hal.init();
    hal.connect();
    hal.disconnect();
    hal.set_address(uint8_t{});
    hal.ep_configure(config);
    hal.ep_write(ep, data, len);
    hal.ep_stall(ep, bool{});
    hal.ep_unstall(ep, bool{});
    hal.poll();
    chal.is_connected();
};
```

**concept に含まれていないが AudioInterface が呼び出しているメソッド:**

| メソッド | 呼出元 | 用途 |
|---------|--------|------|
| `set_feedback_ep(ep)` | `audio_interface.hh:1440` | フィードバック EP 番号を HAL に通知 |
| `is_feedback_tx_ready()` | `audio_interface.hh:2158` | フィードバック送信可否判定（パリティチェック含む） |
| `set_feedback_tx_flag()` | `audio_interface.hh:1451, 2160` | フィードバック送信中フラグ設定 |
| `ep0_prepare_rx(len)` | `device.hh:327` | EP0 OUT データステージ受信準備 |
| `update_iso_out_ep(ep)` | SOF ハンドラ経由 | ISO OUT EP のフレームパリティ更新 |
| `rearm_out_ep(ep)` | XFRC 後 | OUT EP 再アーム |

これらは Stm32OtgHal 固有のメソッドだが、concept で要求されていないため、
**別の HAL を実装した場合にコンパイルエラーではなく暗黙的な結合として表面化する。**

### 1-3. AudioInterface 内の STM32 固有コメント/ロジック

`audio_interface.hh` に STM32 固有の記述が 4 箇所存在:

- 行 360: `Applied to both UAC1 and UAC2 per STM32 reference implementations`
- 行 2122: `STM32 OTG FS handles parity via rearm_out_ep after XFRC`
- 行 2157: `Reference: STM32F401_USB_AUDIO_DAC, STM32F411-usbaudio`
- 行 3004: `STM32 OTG FS has EP0-3, with IN and OUT being independent`

コメントだけでなく、フィードバック送信ロジック (`on_sof` 内) がパリティチェック付き
`is_feedback_tx_ready()` に依存しており、これは DWC2 (Synopsys) 固有の挙動。

### 1-4. 分離が正しい箇所

- `core/types.hh` — 純粋な USB プロトコル型。HW 依存なし
- `core/descriptor.hh` — constexpr ビルダー。HW 依存なし
- `core/device.hh` — `Device<Hal, Class>` テンプレートで HAL を抽象化。concept 内メソッドのみ使用（`ep0_prepare_rx` を除く）
- `audio/audio_types.hh` — RingBuffer, FeedbackCalculator。HW 依存なし
- MIDI/Audio データパス — `ep_write()`, `ep_configure()` 等の concept 内メソッドのみ使用

---

## 2. Full Speed 固定箇所の網羅的リスト

### 2-1. パケットサイズ計算 (`/1000` = 1ms フレーム前提)

| 箇所 | コード | 影響 |
|------|--------|------|
| `AudioPort::PACKET_SIZE` (行 63) | `(MaxSampleRate_ + 999) / 1000 + 1) * BYTES_PER_FRAME` | EP wMaxPacketSize |
| `build_descriptor` UAC2 OUT (行 642) | `(AudioOut::SAMPLE_RATE + 999) / 1000 + 1) * BYTES_PER_FRAME` | ディスクリプタ |
| `build_descriptor` UAC1 OUT (行 667) | `(Alt::MAX_RATE + 999) / 1000 + 1) * bytes_per_frame` | ディスクリプタ |
| `build_descriptor` UAC2 IN (行 794) | `(AudioIn::SAMPLE_RATE + 999) / 1000 + 1) * BYTES_PER_FRAME` | ディスクリプタ |
| `build_descriptor` UAC1 IN (行 811) | `(Alt::MAX_RATE + 999) / 1000 + 1) * bytes_per_frame` | ディスクリプタ |
| `make_alt_config` (行 1013) | 同上 | Alt 設定テーブル |
| `OUT_MAX_PACKET_FRAMES` (行 1291) | `AudioOut::MAX_SAMPLE_RATE / 1000 + 1` | バッファサイズ |
| `IN_MAX_PACKET_FRAMES` (行 1292) | `AudioIn::MAX_SAMPLE_RATE / 1000 + 1` | バッファサイズ |

### 2-2. Audio IN 送信のフレームレート計算

| 箇所 | コード |
|------|--------|
| `send_audio_in_now` (行 2531) | `current_sample_rate_ / 1000`, `% 1000` |
| テスト信号生成 (行 2232) | `current_sample_rate_ / 1000`, `% 1000` |
| `in_rate_accum_` (行 1184) | コメント: `Hz % 1000` |

### 2-3. フィードバック形式

| 箇所 | 値 | FS 仕様 | HS 仕様 |
|------|-----|---------|---------|
| `FB_PACKET_SIZE` (行 145) | `3` 固定 | 3 バイト (10.14) | 4 バイト (16.16) |
| `fb_last_bytes_` (行 1309) | `array<uint8_t, 3>` | 3 バイト | 4 バイト |
| `FeedbackCalculator::FEEDBACK_SHIFT` (`audio_types.hh`) | `14` 固定 | 10.14 | 16.16 |
| `FeedbackCalculator::FEEDBACK_BYTES` | `3` 固定 | 3 | 4 |
| `nominal_feedback_` 計算 | `(rate << 14) / 1000` | samples/ms | samples/125μs |
| `get_feedback_bytes()` | 3 バイト返却 | 3 | 4 |

### 2-4. ディスクリプタの bInterval

| 箇所 | 現在値 | FS 意味 | HS で必要な値 |
|------|--------|---------|--------------|
| Feedback EP (行 659, UAC2) | `1` | 1ms | `4` (2^(4-1)=8 μframe=1ms) |
| Feedback EP (行 721, UAC1) | `1` | 1ms | `4` |
| ISO データ EP | `1` | 1ms | `1` (125μs) |

### 2-5. SOF ハンドラのタイミング前提

- `on_sof` (行 2180): コメント `every 1ms`
- FS: SOF = 1ms 間隔
- HS: SOF = 125μs 間隔（microframe）→ 呼び出し頻度が 8 倍に

### 2-6. Device descriptor

- `device.hh:141`: `bcdUSB = 0x0200` 固定（これ自体は正しい）
- Device Qualifier descriptor と Other Speed Configuration descriptor に未対応

### 2-7. STM32 HAL

- `stm32_otg.hh:404`: `DCFG_DSPD_FS` 固定
- `stm32_otg.hh:377,390`: `GUSBCFG_PHYSEL` (FS 埋め込み PHY 選択)
- FIFO サイズ: 320 ワード (OTG FS 制約。OTG HS は 1024+ ワード)

---

## 3. High Speed 対応設計

### 3-1. 設計方針

**テンプレートパラメータ `MaxSpeed` で最大対応速度を指定する。**

- `MaxSpeed::FULL` 指定時: 現状と同一のコード生成（HS コードは完全除去）
- `MaxSpeed::HIGH` 指定時: FS/HS 両方のディスクリプタとランタイム分岐を含む

```cpp
enum class MaxSpeed : uint8_t {
    FULL = 1,   // FS のみ (現状と同一バイナリ)
    HIGH = 2,   // FS + HS 対応
};
```

### 3-2. Speed 依存パラメータのまとめ

| パラメータ | Full Speed (12Mbps) | High Speed (480Mbps) |
|-----------|---------------------|----------------------|
| フレーム間隔 | 1ms | 125μs (microframe) |
| フレームレート除数 | 1000 | 8000 |
| ISO パケットサイズ上限 | 1023 バイト | 1024 × 3 バイト |
| フィードバック形式 | 10.14 (3 バイト) | 16.16 (4 バイト) |
| フィードバック bInterval | 1 (= 1ms) | 4 (= 2^3 × 125μs = 1ms) |
| ISO データ bInterval | 1 (= 1ms) | 1 (= 125μs) |
| EP0 最大パケット | 64 | 64 |

### 3-3. コンパイル時 / ランタイムの境界

**原則: FS のみ時はすべて constexpr。HS 対応時のみランタイム分岐を導入。**

#### constexpr のまま維持するもの (MaxSpeed 不問)

- ディスクリプタバイト列（FS/HS 各1セット、コンパイル時生成）
- AudioPort のバッファサイズ (BUFFER_FRAMES)
- Alt 設定テーブル (OUT_ALT_CONFIGS, IN_ALT_CONFIGS)

#### MaxSpeed::HIGH 時にランタイム化するもの

- アクティブなディスクリプタの選択（FS 用 / HS 用）
- フィードバックバイト数 (3 or 4)
- パケットあたりフレーム数の除数 (1000 or 8000)
- SOF 処理の頻度調整

#### 実装パターン

```cpp
// SpeedTraits: コンパイル時に FS/HS の定数を生成
template <Speed S>
struct SpeedTraits;

template <>
struct SpeedTraits<Speed::FULL> {
    static constexpr uint32_t frame_divisor = 1000;
    static constexpr uint8_t fb_bytes = 3;
    static constexpr uint8_t fb_shift = 14;
    static constexpr uint8_t iso_binterval = 1;
    static constexpr uint8_t fb_binterval = 1;
};

template <>
struct SpeedTraits<Speed::HIGH> {
    static constexpr uint32_t frame_divisor = 8000;
    static constexpr uint8_t fb_bytes = 4;
    static constexpr uint8_t fb_shift = 16;  // 16.16 format
    static constexpr uint8_t iso_binterval = 1;  // 125μs
    static constexpr uint8_t fb_binterval = 4;   // 2^3 × 125μs = 1ms
};
```

```cpp
// AudioInterface に MaxSpeed テンプレートパラメータを追加
template <UacVersion Version = UacVersion::UAC1,
          MaxSpeed MaxSpd = MaxSpeed::FULL,  // 追加
          typename AudioOut_ = AudioStereo48k,
          ...>
class AudioInterface {
    // FS のみ時: constexpr で確定
    // HS 対応時: 両方の定数を持ち、ランタイムで選択
    static constexpr bool SUPPORTS_HS = (MaxSpd == MaxSpeed::HIGH);

    // ディスクリプタは速度ごとに1セット
    static constexpr auto fs_descriptor_ = build_descriptor<Speed::FULL>();
    static constexpr auto hs_descriptor_ = []() {
        if constexpr (SUPPORTS_HS) return build_descriptor<Speed::HIGH>();
        else return std::array<uint8_t, 0>{};
    }();

    // ランタイムの速度 (HAL から取得)
    Speed current_speed_ = Speed::FULL;

    uint32_t frame_divisor() const {
        if constexpr (!SUPPORTS_HS) return 1000;
        return (current_speed_ == Speed::HIGH) ? 8000 : 1000;
    }
};
```

### 3-4. Hal concept の正規化

concept に不足しているメソッドを追加し、HW 依存の漏れを解消する:

```cpp
template<typename T>
concept Hal = requires(T& hal, const T& chal, ...) {
    // 既存 (変更なし)
    { hal.init() } -> std::same_as<void>;
    { hal.connect() } -> std::same_as<void>;
    { hal.disconnect() } -> std::same_as<void>;
    { hal.set_address(uint8_t{}) } -> std::same_as<void>;
    { hal.ep_configure(config) } -> std::same_as<void>;
    { hal.ep_write(ep, data, len) } -> std::same_as<void>;
    { hal.ep_stall(ep, bool{}) } -> std::same_as<void>;
    { hal.ep_unstall(ep, bool{}) } -> std::same_as<void>;
    { hal.poll() } -> std::same_as<void>;
    { chal.is_connected() } -> std::convertible_to<bool>;

    // 追加: 速度情報
    { chal.get_speed() } -> std::convertible_to<Speed>;

    // 追加: EP0 データステージ
    { hal.ep0_prepare_rx(uint16_t{}) } -> std::same_as<void>;

    // 追加: Isochronous フィードバック制御
    { hal.set_feedback_ep(uint8_t{}) } -> std::same_as<void>;
    { chal.is_feedback_tx_ready() } -> std::convertible_to<bool>;
    { hal.set_feedback_tx_flag() } -> std::same_as<void>;
};
```

### 3-5. Device Qualifier / Other Speed Configuration

`Device::handle_get_descriptor` に以下を追加:

```cpp
case bDescriptorType::DeviceQualifier:
    if constexpr (SUPPORTS_HS) {
        // 現在と反対の速度の能力を返す
        send_response(qualifier_desc, ...);
    } else {
        hal_.ep_stall(0, true);  // FS のみ → 非対応
    }
    break;

case bDescriptorType::OtherSpeedConfig:
    if constexpr (SUPPORTS_HS) {
        // 現在と反対の速度用ディスクリプタを返す
        auto desc = (current_speed == Speed::HIGH)
            ? class_.fs_config_descriptor()
            : class_.hs_config_descriptor();
        send_response(desc, ...);
    } else {
        hal_.ep_stall(0, true);
    }
    break;
```

### 3-6. FeedbackCalculator の Speed 対応

```cpp
template <Speed S = Speed::FULL>
class FeedbackCalculator {
    static constexpr auto Traits = SpeedTraits<S>;
    static constexpr uint32_t FEEDBACK_SHIFT = Traits::fb_shift;
    static constexpr uint32_t FEEDBACK_BYTES = Traits::fb_bytes;

    void reset(uint32_t nominal_rate) {
        nominal_feedback_ = (nominal_rate << FEEDBACK_SHIFT) / Traits::frame_divisor;
        // ...
    }

    std::array<uint8_t, FEEDBACK_BYTES> get_feedback_bytes() const;
};
```

FS のみ時は `FeedbackCalculator<Speed::FULL>` で現状と同一コード。

### 3-7. SOF ハンドラの HS 対応

HS では SOF が 125μs ごとに発生する。Audio IN 送信やフィードバック更新を
毎 SOF で行うとオーバーヘッドが大きいため、8 microframe に 1 回の処理にする:

```cpp
void on_sof(HalT& hal) {
    ++microframe_count_;
    if constexpr (SUPPORTS_HS) {
        if (current_speed_ == Speed::HIGH) {
            // HS: 8 microframe = 1ms 相当でのみ処理
            if ((microframe_count_ & 7) != 0) return;
        }
    }
    // 以下、既存の 1ms 単位処理
}
```

---

## 4. Low Speed について

USB 1.x Low Speed (1.5Mbps) は **Isochronous 転送と Bulk 転送をサポートしない**
（USB 仕様: Control と Interrupt のみ）。USB Audio は Isochronous、
USB MIDI は Bulk を使用するため、**Low Speed での動作は仕様上不可能**。

umiusb は Audio/MIDI に特化しているため、Low Speed 対応は不要。
`Speed::LOW` は型定義として存在するが、AudioInterface での使用は想定外。

---

## 5. 段階的実装計画

### Phase 1: Hal concept の正規化 (非破壊的)

**目的:** HW 依存/非依存の分離を完全にする

1. `Hal` concept に `get_speed`, `ep0_prepare_rx`, `set_feedback_ep`,
   `is_feedback_tx_ready`, `set_feedback_tx_flag` を追加
2. `Stm32OtgHal` が concept を満たすことを `static_assert` で検証
3. 既存動作に影響なし

### Phase 2: SpeedTraits と MaxSpeed テンプレートパラメータ導入

**目的:** Speed 依存定数をテンプレート化

1. `SpeedTraits<Speed>` 定義
2. `AudioInterface` に `MaxSpeed` テンプレートパラメータ追加（デフォルト `FULL`）
3. `FeedbackCalculator` を Speed テンプレート化
4. 既存コードは `MaxSpeed::FULL` で現状と同一動作

### Phase 3: ディスクリプタの Speed 対応

**目的:** FS/HS 別ディスクリプタのコンパイル時生成

1. `build_descriptor` に Speed テンプレートパラメータ追加
2. パケットサイズ、bInterval を SpeedTraits から取得
3. Device Qualifier / Other Speed Configuration 応答を Device に追加

### Phase 4: ランタイム Speed 分岐

**目的:** HS 接続時の動作を実装

1. `on_configured` 時に HAL から `get_speed()` を取得し `current_speed_` を設定
2. フィードバック計算の除数を `frame_divisor()` で切り替え
3. Audio IN 送信のフレーム数計算を Speed 対応
4. SOF ハンドラの microframe カウント導入

### Phase 5: HS HAL 実装 (将来・HW 依存)

**目的:** 実際の HS ハードウェアで動作

1. STM32 OTG HS HAL（または他プラットフォーム HAL）の実装
2. FIFO サイズ拡張、ULPI PHY 設定
3. 実機テスト

---

## 6. FS のみ使用時のコスト分析

`MaxSpeed::FULL` (デフォルト) 指定時:

| 項目 | コスト |
|------|--------|
| HS ディスクリプタ | `std::array<uint8_t, 0>` — ゼロバイト |
| `frame_divisor()` | constexpr `1000` — 関数呼出なし |
| SOF microframe カウント | `if constexpr` で除去 |
| FeedbackCalculator | `Speed::FULL` 特殊化 — 現状と同一 |
| Device Qualifier 応答 | STALL — 分岐なし |

**結論: FS のみ使用時のバイナリは現状と同一サイズ・同一性能。**

---
