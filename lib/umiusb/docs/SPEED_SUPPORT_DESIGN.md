# umiusb HW依存/非依存層の分離状況と High Speed 対応設計

> **注:** 本ドキュメントは分割済みです。各トピックの詳細は以下を参照してください。
> 本ファイルは全内容を含むマスタードキュメントとして残してあります。
>
> | # | ドキュメント | 内容 | セクション |
> |---|------------|------|-----------|
> | 01 | [01-speed-support.md](design/01-speed-support.md) | HW 分離分析と High Speed 対応設計 | 1-6 |
> | 02 | [02-hal-winusb-webusb.md](design/02-hal-winusb-webusb.md) | Hal/Class concept 拡張、WinUSB、WebUSB | 7-9 |
> | 03 | [03-api-architecture.md](design/03-api-architecture.md) | API アーキテクチャとドメイン規定 | 11-14 |
> | 04 | [04-isr-decoupling.md](design/04-isr-decoupling.md) | ISR 軽量化と外部処理の疎結合設計 | 15-16 |
> | 05 | [05-midi-integration.md](design/05-midi-integration.md) | MIDI 統合の再設計と MIDI 2.0 対応 | 17 |
> | 06 | [06-midi-separation.md](design/06-midi-separation.md) | MIDI 完全分離 | — |
> | 07 | [07-uac-feature-coverage.md](design/07-uac-feature-coverage.md) | UAC 機能網羅計画 | — |
> | — | [00-implementation-plan.md](design/00-implementation-plan.md) | 統合実装計画 (インデックス) | — |

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

## 7. Hal concept の網羅的不足分析

USB Audio/MIDI の完全実装に向けて、Hal concept に追加すべきメソッドを
3 カテゴリに整理する。

### 7-1. 必須追加 — 既に呼ばれているが concept にないもの

現在のコードで AudioInterface や Device が呼び出しているが、
`Hal` concept に定義されていないメソッド。別 HAL を実装すると
コンパイルエラーになる箇所。

| メソッド | 呼出元 | 用途 | 分類 |
|---------|--------|------|------|
| `get_speed()` | AudioInterface (HS 対応後) | ネゴシエーション後の実速度取得 | 基本 |
| `ep0_prepare_rx(len)` | `device.hh:327` | EP0 DATA OUT ステージ受信準備 | 制御転送 |
| `set_feedback_ep(ep)` | `audio_interface.hh:1440` | フィードバック EP 番号通知 | ISO |
| `is_feedback_tx_ready()` | `audio_interface.hh:2158` | フィードバック送信可否判定 | ISO |
| `set_feedback_tx_flag()` | `audio_interface.hh:1451,2160` | フィードバック送信中フラグ | ISO |

### 7-2. Audio/MIDI 完全実装に必要 — 将来追加候補

現在は HAL 内部で暗黙的に処理されているが、
汎用的な Audio/MIDI 実装には concept レベルで明示すべきもの。

| メソッド | 用途 | 備考 |
|---------|------|------|
| `ep_read(ep, buf, len)` | OUT EP データ読み出し | 現在は HAL 内部のコールバックで処理 |
| `ep_set_nak(ep)` / `ep_clear_nak(ep)` | エンドポイントフロー制御 | バッファ溢れ防止 |
| `is_ep_busy(ep)` | ISO IN 送信衝突防止 | `stm32_otg.hh:515` に実装あるが concept 外 |
| `update_iso_out_ep(ep)` | ISO OUT パリティ更新 | SOF 時に呼ぶ必要あり。DWC2 固有の可能性 |
| `get_frame_number()` | 現在のフレーム番号取得 | フィードバック計算、HS microframe 管理 |

### 7-3. Vendor/BOS リクエスト対応に必要

WinUSB/WebUSB 対応に必要な Device 層の拡張。HAL 側は変更不要だが、
Device → Class 間のインタフェースに影響。

| 要件 | 現状 | 必要な変更 |
|------|------|-----------|
| Vendor request ハンドリング | `device.hh:192` で全て STALL | Class に委譲する仕組み追加 |
| BOS descriptor 応答 | `handle_get_descriptor` に case なし | BOS case 追加 |
| Class concept 拡張 | `handle_request` のみ | `handle_vendor_request`, `bos_descriptor` 追加 |

### 7-4. 改善後の Hal concept (完全版)

```cpp
template<typename T>
concept Hal = requires(T& hal, const T& chal,
                       uint8_t ep, const uint8_t* data, uint16_t len,
                       const EndpointConfig& config) {
    // --- 基本 ---
    { hal.init() } -> std::same_as<void>;
    { hal.connect() } -> std::same_as<void>;
    { hal.disconnect() } -> std::same_as<void>;
    { chal.is_connected() } -> std::convertible_to<bool>;
    { chal.get_speed() } -> std::convertible_to<Speed>;

    // --- アドレス ---
    { hal.set_address(uint8_t{}) } -> std::same_as<void>;

    // --- エンドポイント操作 ---
    { hal.ep_configure(config) } -> std::same_as<void>;
    { hal.ep_write(ep, data, len) } -> std::same_as<void>;
    { hal.ep_stall(ep, bool{}) } -> std::same_as<void>;
    { hal.ep_unstall(ep, bool{}) } -> std::same_as<void>;

    // --- EP0 制御転送 ---
    { hal.ep0_prepare_rx(uint16_t{}) } -> std::same_as<void>;

    // --- Isochronous フィードバック ---
    { hal.set_feedback_ep(uint8_t{}) } -> std::same_as<void>;
    { chal.is_feedback_tx_ready() } -> std::convertible_to<bool>;
    { hal.set_feedback_tx_flag() } -> std::same_as<void>;

    // --- ポーリング ---
    { hal.poll() } -> std::same_as<void>;
};
```

### 7-5. 改善後の Class concept

```cpp
template<typename T>
concept Class = requires(T& cls, const SetupPacket& setup) {
    // 既存
    { cls.config_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;
    { cls.on_configured(bool{}) } -> std::same_as<void>;
    { cls.handle_request(setup, std::declval<std::span<uint8_t>&>()) }
        -> std::convertible_to<bool>;
    { cls.on_rx(uint8_t{}, std::declval<std::span<const uint8_t>>()) }
        -> std::same_as<void>;

    // 追加: Vendor request (WinUSB/WebUSB)
    { cls.handle_vendor_request(setup, std::declval<std::span<uint8_t>&>()) }
        -> std::convertible_to<bool>;

    // 追加: BOS descriptor
    { cls.bos_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;
};
```

---

## 8. WinUSB ドライバレス対応

### 8-1. 現状

Windows で USB Audio/MIDI デバイスをドライバレスで使用するには、
MS OS 2.0 ディスクリプタで WinUSB 互換 ID を通知する必要がある。

**実装済み:**

| コンポーネント | ファイル | 状態 |
|--------------|---------|------|
| MS OS 2.0 ディスクリプタビルダー | `core/descriptor.hh:337-463` | ✅ 完全 |
| `CompatibleIdWinUsb()` | `core/descriptor.hh:400` | ✅ |
| `RegistryPropertyGuid()` | `core/descriptor.hh:407` | ✅ |
| `BosHeader()` / `PlatformCapability()` | `core/descriptor.hh:362-376` | ✅ |
| WinUSB デバイス例 | `descriptor_examples.hh:48-120` | ✅ |
| Composite (MIDI+WinUSB) 例 | `descriptor_examples.hh:126-225` | ✅ |

**未実装:**

| コンポーネント | ファイル | 問題 |
|--------------|---------|------|
| BOS descriptor 応答 | `core/device.hh` | `handle_get_descriptor` に BOS case なし |
| Vendor request ハンドリング | `core/device.hh:192-196` | 全て STALL — Class に委譲されない |
| AudioInterface での BOS/MS OS 2.0 統合 | `audio/audio_interface.hh` | BOS 生成・vendor request 処理なし |

### 8-2. 必要な変更

#### 8-2-1. Device に BOS descriptor 応答を追加

```cpp
// device.hh: handle_get_descriptor 内
case bDescriptorType::BOS:
    if constexpr (requires { cls.bos_descriptor(); }) {
        auto bos = class_.bos_descriptor();
        if (!bos.empty()) {
            send_response(bos.data(), static_cast<uint16_t>(bos.size()), setup.wLength);
            return;
        }
    }
    hal_.ep_stall(0, true);
    break;
```

#### 8-2-2. Vendor request を Class に委譲

```cpp
// device.hh: handle_setup 内
case 2:  // Vendor request
    handle_vendor_request(setup);
    break;

// 新規メソッド
void handle_vendor_request(const SetupPacket& setup) {
    std::span<uint8_t> response(ep0_buf_, EP0_SIZE);
    if constexpr (requires { class_.handle_vendor_request(setup, response); }) {
        if (class_.handle_vendor_request(setup, response)) {
            if (setup.direction() == Direction::IN && !response.empty()) {
                send_response(response.data(),
                              static_cast<uint16_t>(response.size()),
                              setup.wLength);
            } else {
                send_zlp();
            }
            return;
        }
    }
    hal_.ep_stall(0, true);
}
```

#### 8-2-3. AudioInterface に WinUSB サポート追加

```cpp
// AudioInterface 内
static constexpr uint8_t WINUSB_VENDOR_CODE = 0x01;

// BOS ディスクリプタ (コンパイル時生成)
static constexpr auto bos_desc_ = []() {
    auto ms_compat = winusb::CompatibleIdWinUsb();
    auto ms_registry = winusb::RegistryPropertyGuid(DEVICE_GUID);
    auto ms_func = winusb::FunctionSubsetHeader(0, 8 + ms_compat.size + ms_registry.size);
    auto ms_config = winusb::ConfigSubsetHeader(0,
        8 + ms_func.size + ms_compat.size + ms_registry.size);
    auto ms_set = winusb::DescriptorSetHeader(
        10 + ms_config.size + ms_func.size + ms_compat.size + ms_registry.size)
        + ms_config + ms_func + ms_compat + ms_registry;
    auto bos_cap = winusb::PlatformCapability(
        static_cast<uint16_t>(ms_set.size), WINUSB_VENDOR_CODE);
    return winusb::BosHeader(static_cast<uint16_t>(5 + bos_cap.size), 1) + bos_cap;
}();

std::span<const uint8_t> bos_descriptor() const {
    return {bos_desc_.data, bos_desc_.size};
}

bool handle_vendor_request(const SetupPacket& setup, std::span<uint8_t>& response) {
    if (setup.bRequest == WINUSB_VENDOR_CODE) {
        // MS OS 2.0 descriptor set を返す
        // ...
        return true;
    }
    return false;
}
```

#### 8-2-4. bcdUSB の変更

BOS ディスクリプタを使用する場合、`bcdUSB` を `0x0201` に設定することで
ホストに BOS 対応を示唆する（USB 2.01 以降の仕様）。

```cpp
// device.hh: build_device_descriptor 内
device_desc_.bcdUSB = 0x0201;  // USB 2.01 (BOS 対応)
```

### 8-3. Windows ドライバレス動作フロー

```
1. デバイス接続
2. Windows が BOS descriptor を要求 (GET_DESCRIPTOR, type=0x0F)
3. Device が BOS を返す → MS OS 2.0 Platform Capability 含む
4. Windows が Vendor request (bRequest=0x01) で MS OS 2.0 descriptor set を要求
5. Device が descriptor set を返す → CompatibleId "WINUSB" 含む
6. Windows が WinUSB ドライバを自動バインド
7. ドライバインストール不要で動作開始
```

---

## 9. WebUSB 対応設計

### 9-1. 概要

WebUSB は Web ブラウザから USB デバイスにアクセスするための W3C 仕様。
デバイスが BOS 内に WebUSB Platform Capability を持つと、Chrome 等の対応
ブラウザがデバイス接続時に Landing Page URL のポップアップ通知を表示する。

### 9-2. 必要なコンポーネント

#### WebUSB Platform Capability UUID

```
{3408b638-09a9-47a0-8bfd-a0768815b665}
```

#### BOS 内の WebUSB Platform Capability

```
bLength             = 24
bDescriptorType     = 0x10 (Device Capability)
bDevCapabilityType  = 0x05 (Platform)
bReserved           = 0x00
PlatformCapabilityUUID = {3408b638-09a9-47a0-8bfd-a0768815b665}
bcdVersion          = 0x0100 (WebUSB 1.0)
bVendorCode         = 0x02  (Vendor request code for WebUSB)
iLandingPage        = 0x01  (URL descriptor index)
```

#### URL ディスクリプタ (Vendor request で返却)

```
bLength         = 3 + strlen(url)
bDescriptorType = 0x03 (WebUSB URL)
bScheme         = 0x01 (https://)
URL             = "example.com/device"  (ASCII, スキーム除く)
```

### 9-3. descriptor.hh への追加設計

```cpp
namespace webusb {

// WebUSB Platform Capability UUID
// {3408b638-09a9-47a0-8bfd-a0768815b665}
constexpr auto WebUsbPlatformCapabilityUuid() {
    return bytes(
        0x38, 0xB6, 0x08, 0x34,  // DWORD (little-endian)
        0xA9, 0x09,              // WORD
        0xA0, 0x47,              // WORD
        0x8B, 0xFD,              // 2 bytes
        0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65  // 6 bytes
    );
}

/// WebUSB Platform Capability descriptor (BOS 内に配置)
constexpr auto PlatformCapability(uint8_t vendor_code, uint8_t landing_page_idx = 1) {
    return bytes(24, dtype::DeviceCapability, 0x05, 0x00) +
           WebUsbPlatformCapabilityUuid() +
           le16(0x0100) +  // bcdVersion: WebUSB 1.0
           bytes(vendor_code, landing_page_idx);
}

/// URL scheme
inline constexpr uint8_t SCHEME_HTTP = 0x00;
inline constexpr uint8_t SCHEME_HTTPS = 0x01;

/// URL ディスクリプタ (vendor request で返却)
template<std::size_t N>
constexpr auto UrlDescriptor(uint8_t scheme, const char (&url)[N]) {
    constexpr std::size_t url_len = N - 1;  // exclude null
    constexpr std::size_t total = 3 + url_len;
    Bytes<total> result{};
    result[0] = total;
    result[1] = 0x03;   // bDescriptorType: WebUSB URL
    result[2] = scheme;
    for (std::size_t i = 0; i < url_len; ++i) {
        result[3 + i] = static_cast<uint8_t>(url[i]);
    }
    return result;
}

}  // namespace webusb
```

### 9-4. BOS への WinUSB + WebUSB 統合

```cpp
// WinUSB + WebUSB の両方を BOS に含める
static constexpr auto bos_desc_ = []() {
    // WinUSB
    auto winusb_cap = winusb::PlatformCapability(ms_desc_set_size, WINUSB_VENDOR_CODE);
    // WebUSB
    auto webusb_cap = webusb::PlatformCapability(WEBUSB_VENDOR_CODE, 1);

    uint16_t total = 5 + winusb_cap.size + webusb_cap.size;
    return winusb::BosHeader(total, 2) + winusb_cap + webusb_cap;
}();
```

### 9-5. Vendor request ハンドラの拡張

```cpp
bool handle_vendor_request(const SetupPacket& setup, std::span<uint8_t>& response) {
    if (setup.bRequest == WINUSB_VENDOR_CODE) {
        // MS OS 2.0 descriptor set を返す
        return handle_winusb_request(setup, response);
    }
    if (setup.bRequest == WEBUSB_VENDOR_CODE) {
        // WebUSB URL ディスクリプタを返す
        return handle_webusb_request(setup, response);
    }
    return false;
}

bool handle_webusb_request(const SetupPacket& setup, std::span<uint8_t>& response) {
    if (setup.descriptor_type() == 0x03 && setup.descriptor_index() == 1) {
        // Landing Page URL を返す
        std::memcpy(response.data(), url_desc_.data, url_desc_.size);
        response = response.first(url_desc_.size);
        return true;
    }
    return false;
}
```

### 9-6. ブラウザでの動作フロー

```
1. USB デバイス接続
2. Chrome が BOS descriptor を要求
3. BOS 内の WebUSB Platform Capability を検出
4. Chrome がアドレスバー付近に通知ポップアップを表示:
   「[デバイス名] が example.com/device に移動するよう提案しています」
5. ユーザーがクリック → Landing Page URL が開く
6. Web アプリが navigator.usb.requestDevice() でデバイスにアクセス可能
```

### 9-7. 制約と注意事項

| 項目 | 内容 |
|------|------|
| ブラウザ対応 | Chrome/Edge/Opera のみ。Firefox/Safari は未対応 |
| HTTPS 必須 | Landing Page は HTTPS でホストする必要あり (localhost 除く) |
| ユーザー操作必須 | `navigator.usb.requestDevice()` はユーザージェスチャーが必要 |
| Audio クラスとの共存 | WebUSB は Audio Class デバイスに対しても動作する |
| WinUSB との共存 | BOS 内に両方の Platform Capability を共存可能 |

---

## 10. 改訂段階的実装計画

セクション 5 の計画を WinUSB/WebUSB を含めて改訂する。

### Phase 1: Hal concept + Class concept の正規化

1. `Hal` concept にセクション 7-4 のメソッドを追加
2. `Class` concept にセクション 7-5 の `handle_vendor_request`, `bos_descriptor` を追加
3. `Stm32OtgHal` と `AudioInterface` が concept を満たすことを `static_assert` で検証
4. 既存動作に影響なし

### Phase 2: WinUSB + WebUSB ディスクリプタ対応

1. `descriptor.hh` に `webusb` 名前空間を追加
2. `Device::handle_get_descriptor` に BOS 応答を追加
3. `Device::handle_setup` で vendor request を Class に委譲
4. `AudioInterface` に BOS 生成 (WinUSB + WebUSB) と vendor request ハンドラ追加
5. `bcdUSB` を `0x0201` に変更

### Phase 3: SpeedTraits と MaxSpeed テンプレートパラメータ導入

(セクション 5 の Phase 2 と同一)

### Phase 4: ディスクリプタの Speed 対応

(セクション 5 の Phase 3 と同一 + BOS/WinUSB/WebUSB も Speed 対応)

### Phase 5: ランタイム Speed 分岐

(セクション 5 の Phase 4 と同一)

### Phase 6: HS HAL 実装 (将来・HW 依存)

(セクション 5 の Phase 5 と同一)

---

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

## 17. MIDI 統合の再設計 — EventRouter 連携と MIDI 2.0 対応

### 17-1. 問題: 現状の MIDI API は直接操作型

現在の umiusb の MIDI API は AudioInterface に直接的な送受信メソッドを持つ:

```
現状のデータフロー:

受信:
  USB EP → MidiProcessor::process_packet()
       → on_midi callback (生バイト: cable, data, len)
       → UmidiUsbMidiAdapter → umidi::Parser → UMP32 → EventQueue

送信:
  アプリ → AudioInterface::send_midi(hal, cable, status, d1, d2)
       → USB MIDI 1.0 パケット構築 → hal.ep_write()
```

**問題点:**

| 問題 | 詳細 |
|------|------|
| EventRouter と無関係 | USB MIDI がイベントシステムの外側で処理されている |
| umios アーキテクチャと不整合 | RawInputQueue への投入が OS 側でなく adapter 側の責務 |
| MIDI 1.0 固定 | MidiProcessor が CIN ベースの USB MIDI 1.0 パケットのみ対応 |
| 送信 API が USB 固有 | `send_midi(hal, cable, status, d1, d2)` は USB MIDI 1.0 の生パケット構築 |
| 二重パース | MidiProcessor が CIN→バイト、UmidiUsbMidiAdapter が再度バイト→UMP32 と2段変換 |
| SysEx が別系統 | SysEx は MidiProcessor 内で再組み立て、独自コールバック経由 |

### 17-2. あるべき姿: EventRouter 統合

umios-architecture で定義された設計に従い、USB MIDI は
**トランスポート層の一つとして RawInputQueue にイベントを投入するだけ** にすべき。

```
あるべきデータフロー:

受信 (Host → Device):
  USB EP_MIDI_OUT → [UsbMidiTransport] → RawInputQueue
                                              │
                                              ▼
                                         EventRouter
                                         ├→ AudioEventQueue (ROUTE_AUDIO)
                                         ├→ SharedParamState (ROUTE_PARAM)
                                         ├→ ControlEventQueue (ROUTE_CONTROL)
                                         └→ Stream (ROUTE_STREAM)

送信 (Device → Host):
  Processor::output_events → EventRouter
                                 │
                                 ▼
                            [UsbMidiTransport]
                            ├→ USB: ep_write()
                            ├→ UART: DMA 送信
                            └→ (BLE: 将来)
```

### 17-3. USB MIDI トランスポートの責務分離

#### umiusb 側の責務 (トランスポート層)

USB パケットの送受信 **のみ**。メッセージの意味を解釈しない。

```cpp
/// USB MIDI トランスポート — umiusb が提供
/// umidi::MidiInput / MidiOutput concept を満たす
template<UsbMidiVersion MaxVersion = UsbMidiVersion::MIDI_2_0>
class UsbMidiTransport {
public:
    // --- MidiInput concept ---
    void poll() {}  // コールバック駆動、ポーリング不要
    bool is_connected() const { return configured_; }

    // --- MidiOutput concept ---
    // UMP32 → USB MIDI 1.0 パケットに変換して送信
    // UMP64 → USB MIDI 2.0 パケットに変換して送信 (MIDI 2.0 mode)
    template<Hal HalT>
    bool send(HalT& hal, const umidi::UMP32& ump);

    template<Hal HalT>
    bool send(HalT& hal, const umidi::UMP64& ump);  // MIDI 2.0

    // --- USB コールバック (AudioInterface/Device から呼ばれる) ---
    // USB MIDI 1.0 パケット受信時
    void on_usb_midi_rx(const uint8_t* data, uint16_t len);

    // USB MIDI 2.0 パケット受信時 (将来)
    void on_usb_midi2_rx(const uint8_t* data, uint16_t len);

    // --- OS 接続 ---
    void set_raw_input_queue(RawInputQueue* q) { queue_ = q; }
    void set_source_id(uint8_t id) { source_id_ = id; }

private:
    RawInputQueue* queue_ = nullptr;
    uint8_t source_id_ = 0;
    bool configured_ = false;
    UsbMidiVersion active_version_ = UsbMidiVersion::MIDI_1_0;
};
```

#### OS 側の責務 (EventRouter)

ルーティング、タイムスタンプ確定、値変換。umiusb を知らない。

#### アプリ側の責務

`ROUTE_CONTROL_RAW` で受け取った UMP32 を `umidi::Decoder` で解釈。
または `output_events` に UMP32/UMP64 を投入して EventRouter 経由で送信。

### 17-4. AudioInterface からの MIDI 分離

現在 AudioInterface が持つ MIDI 関連コードを分離する:

| 現在の場所 | 分離先 | 理由 |
|-----------|--------|------|
| `MidiProcessor` (audio_types.hh:507-615) | `UsbMidiTransport` | USB パケット解析はトランスポートの責務 |
| `send_midi/send_note_on/send_cc/send_sysex` (audio_interface.hh:2589-2681) | `UsbMidiTransport::send()` | 送信もトランスポートの責務 |
| `set_midi_callback/set_sysex_callback` | 不要 (RawInputQueue 経由) | OS が EventRouter でルーティング |
| `UmidiUsbMidiAdapter` (umidi_adapter.hh) | 不要 (RawInputQueue 直接投入) | 二重パースの解消 |
| MIDI EP の configure/on_rx 分岐 | AudioInterface 内に残す (EP 管理) | USB EP 構成は Audio Class の一部 |

#### 分離後の AudioInterface

```cpp
template <..., typename MidiTransport_ = NullMidiTransport>
class AudioInterface {
    MidiTransport_ midi_transport_;

    // on_rx での MIDI 処理
    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if constexpr (HAS_MIDI_OUT) {
            if (ep == EP_MIDI_OUT) {
                // トランスポートに丸投げ — パース・ルーティングは OS の責務
                midi_transport_.on_usb_midi_rx(data.data(), data.size());
                return;
            }
        }
        // Audio OUT 処理...
    }

    // 送信 — EventRouter 経由で呼ばれる
    template<typename HalT>
    void send_midi(HalT& hal, const umidi::UMP32& ump) {
        midi_transport_.send(hal, ump);
    }
};
```

### 17-5. MIDI 2.0 対応設計

#### USB MIDI 2.0 の要件

| 項目 | USB MIDI 1.0 | USB MIDI 2.0 |
|------|-------------|-------------|
| パケット形式 | 4 bytes (CIN + 3 data) | 4/8/12/16 bytes (UMP 直接) |
| 速度 | Bulk | Bulk (同一) |
| ディスクリプタ | Audio Class 1.0 MS | Audio Class 2.0 MS + Group Terminal Block |
| Alt Setting | Alt 0 のみ | Alt 0 = MIDI 1.0, Alt 1 = MIDI 2.0 |
| 内部表現 | CIN ベース | UMP ネイティブ |
| 解像度 | velocity 7bit, CC 7bit | velocity 16bit, CC 32bit |

#### UMP サイズと Message Type

```
MT=0: Utility           (32bit)  — JR Timestamp, NoOp
MT=1: System Common     (32bit)  — Timing Clock, Start/Stop 等
MT=2: MIDI 1.0 CV       (32bit)  — 従来の NoteOn/Off, CC (7bit)
MT=3: SysEx7            (64bit)  — 7bit SysEx データ
MT=4: MIDI 2.0 CV       (64bit)  — NoteOn/Off (16bit vel), CC (32bit val)
MT=5: SysEx8/MDS        (128bit) — 8bit SysEx, Mixed Data Set
```

#### UsbMidiTransport の MIDI 2.0 処理

```cpp
enum class UsbMidiVersion : uint8_t {
    MIDI_1_0 = 0,  // Alt Setting 0: USB MIDI 1.0 (CIN パケット)
    MIDI_2_0 = 1,  // Alt Setting 1: USB MIDI 2.0 (UMP ネイティブ)
};

void on_usb_midi_rx(const uint8_t* data, uint16_t len) {
    if (active_version_ == UsbMidiVersion::MIDI_2_0) {
        // UMP ネイティブ — パース不要、直接 RawInputQueue へ
        process_ump_stream(data, len);
    } else {
        // USB MIDI 1.0 — CIN パケットをパースして UMP32 に変換
        process_midi1_packets(data, len);
    }
}

void process_ump_stream(const uint8_t* data, uint16_t len) {
    uint16_t pos = 0;
    while (pos < len) {
        uint32_t word0 = read_le32(data + pos);
        uint8_t mt = (word0 >> 28) & 0x0F;
        uint8_t ump_size = ump_word_count(mt);  // MT から UMP サイズを決定

        if (ump_size == 1) {
            // 32bit UMP (MT=0,1,2)
            push_ump32_to_queue({word0});
        } else if (ump_size == 2) {
            // 64bit UMP (MT=3,4)
            uint32_t word1 = read_le32(data + pos + 4);
            push_ump64_to_queue(word0, word1);
        }
        // MT=5 (128bit) は将来対応
        pos += ump_size * 4;
    }
}

// MT → UMP ワード数
static constexpr uint8_t ump_word_count(uint8_t mt) {
    constexpr uint8_t table[] = {1,1,1,2,2,4,1,1,2,2,2,3,3,4,4,4};
    return table[mt & 0x0F];
}
```

#### RawInput の拡張 (MIDI 2.0 対応)

```cpp
struct RawInput {
    uint32_t hw_timestamp;
    uint8_t source_id;
    uint8_t size;           // 4 (UMP32) or 8 (UMP64)
    uint8_t payload[8];     // UMP32 (4B) or UMP64 (8B)
};
// sizeof = 16B (UMP64 対応)
```

#### ディスクリプタ追加

descriptor.hh に MIDI 2.0 用のディスクリプタビルダーを追加:

```cpp
namespace midi2 {

/// Group Terminal Block Descriptor (USB MIDI 2.0 仕様)
constexpr auto GroupTerminalBlock(
    uint8_t block_id,
    uint8_t direction,        // 0=Input, 1=Output, 2=Bidirectional
    uint8_t first_group,
    uint8_t num_groups,
    uint8_t protocol,         // 0x01=MIDI1.0, 0x02=MIDI2.0
    uint16_t max_sysex_size = 256
);

/// Alternate Setting 1 の Interface Descriptor (MIDI 2.0)
/// Alt 0 = MIDI 1.0 (従来), Alt 1 = MIDI 2.0 (UMP ネイティブ)
constexpr auto MidiStreamingInterface2();

}  // namespace midi2
```

### 17-6. EventRouter との接続性

#### 受信パス (USB → EventRouter)

```
USB MIDI EP (Bulk OUT)
    │
    ▼
UsbMidiTransport::on_usb_midi_rx()
    │
    ├─ MIDI 1.0: CIN パケット → parse → UMP32
    │                                      │
    ├─ MIDI 2.0: UMP ストリーム → 直接       │
    │                                      ▼
    └──────────────────────→ RawInputQueue.push({
                                hw_timestamp,     // os::timer::now_us()
                                source_id,        // USB_MIDI = 0
                                size,             // 4 or 8
                                payload           // UMP32 or UMP64
                             })
                                      │
                                      ▼
                                 EventRouter
                                 (以降は 03-event-system.md の通り)
```

#### 送信パス (EventRouter → USB)

```
Processor::output_events
    │ (sample_pos 付き UMP32/UMP64)
    ▼
EventRouter (逆方向処理)
    │ sample_pos → hw_timestamp 変換
    ▼
UsbMidiTransport::send(hal, ump)
    │
    ├─ MIDI 1.0 mode: UMP32 → CIN パケット → ep_write
    │
    └─ MIDI 2.0 mode: UMP → USB パケット直接 → ep_write
```

#### SysEx の統合

SysEx も RawInputQueue 経由に統一する:

```
USB SysEx 受信:
  MIDI 1.0: CIN 0x04-0x07 → UsbMidiTransport 内で
            UMP SysEx7 (MT=3, 64bit) に変換 → RawInputQueue
  MIDI 2.0: MT=3/5 UMP → そのまま RawInputQueue

EventRouter:
  SysExAssembler で再組み立て → 完了後にルーティング
  (05-midi.md の設計通り)
```

### 17-7. umidi ライブラリとの関係整理

```
┌─────────────────────────────────────────────────────────┐
│ umidi (lib/umidi/)                                        │
│                                                           │
│  core/ump.hh      — UMP32, UMP64 型定義                   │
│  core/parser.hh   — バイトストリーム → UMP32 パーサ         │
│  core/sysex.hh    — SysEx バッファ                         │
│  event.hh         — Event (sample_pos + UMP), EventQueue  │
│  codec/decoder.hh — テンプレート型デコーダ                   │
│  messages/        — メッセージ型定義                        │
│                                                           │
│  ※ トランスポート非依存。USB/UART/BLE を知らない            │
└──────────────────┬──────────────────────────────────────┘
                   │ uses
┌──────────────────▼──────────────────────────────────────┐
│ umiusb (lib/umiusb/)                                      │
│                                                           │
│  midi/usb_midi_transport.hh  — USB MIDI トランスポート     │
│    - USB MIDI 1.0 パケット ↔ UMP32 変換                    │
│    - USB MIDI 2.0 UMP ネイティブ転送                        │
│    - RawInputQueue への投入                                │
│    - MidiOutput concept 実装                               │
│                                                           │
│  ※ umidi::Parser, umidi::UMP32 を使用                     │
│  ※ EventRouter は知らない (RawInputQueue のみ)             │
└──────────────────┬──────────────────────────────────────┘
                   │ pushes to
┌──────────────────▼──────────────────────────────────────┐
│ umios EventRouter                                         │
│                                                           │
│  RawInputQueue → ルーティング → AudioEventQueue 等         │
│  output_events → 逆ルーティング → UsbMidiTransport::send() │
│                                                           │
│  ※ umidi::UMP32 型を使用するが、umidi のデコーダは使わない  │
│  ※ RouteTable でメッセージを分類するだけ                    │
└─────────────────────────────────────────────────────────┘
```

### 17-8. MidiProcessor の廃止と移行

現在の `MidiProcessor` (audio_types.hh:507-615) は以下の理由で廃止する:

| MidiProcessor の機能 | 移行先 | 理由 |
|---------------------|--------|------|
| CIN パース (process_packet) | UsbMidiTransport | USB 固有のパケット解析 |
| SysEx 再組み立て (sysex_buf_) | UsbMidiTransport → EventRouter の SysExAssembler | OS 側で統一処理 |
| on_midi コールバック | RawInputQueue.push() | EventRouter 経由に変更 |
| on_sysex コールバック | EventRouter → umi::read_sysex() | OS 側で統一処理 |
| status_to_cin() | UsbMidiTransport (送信用) | USB MIDI 1.0 送信に必要 |

`UmidiUsbMidiAdapter` (umidi_adapter.hh) も同時に廃止。
これにより二重パース (CIN→バイト→UMP32) が解消され、
CIN→UMP32 の1段変換になる。

### 17-9. MIDI 2.0 で必要な RouteTable の拡張

05-midi.md に記載のある方針に沿って、RouteTable に MT=4 用のテーブルを追加:

```cpp
struct RouteTable {
    // --- 既存: MIDI 1.0 (MT=2) ---
    RouteFlags channel_voice[8][16];   // command × channel
    RouteFlags control_change[128];    // CC 番号ごとのオーバーライド
    RouteFlags system[16];             // System メッセージ

    // --- 追加: MIDI 2.0 (MT=4) ---
    RouteFlags channel_voice_2[8][16]; // MIDI 2.0 Channel Voice
    RouteFlags control_change_2[128];  // MIDI 2.0 CC (7bit index、値は 32bit)
};
```

MIDI 2.0 の CC は cc_index が 7bit のままなので、テーブルサイズは同一。
値の解像度 (32bit) は SharedParamState への書き込み時に反映される。

### 17-10. ダウンコンバート方針

MIDI 2.0 メッセージ (UMP64 MT=4) が入力された場合の処理:

```
UMP64 (MT=4, MIDI 2.0 CV)
    │
    ├─ RouteTable に MT=4 用エントリがある
    │   → MT=4 のまま処理 (高解像度値を保持)
    │
    └─ RouteTable に MT=4 用エントリがない (フォールバック)
        → umidi::downconvert(UMP64) → UMP32 (MT=2)
        → MT=2 用テーブルで処理
```

ダウンコンバートは umidi ライブラリが提供する:

```cpp
namespace umidi {
/// MIDI 2.0 Channel Voice (UMP64 MT=4) → MIDI 1.0 (UMP32 MT=2) 変換
/// velocity: 16bit → 7bit, CC value: 32bit → 7bit
constexpr UMP32 downconvert(const UMP64& ump64);
}
```

### 17-11. 改訂ファイル構成 (MIDI 関連)

```
lib/umiusb/include/
├── midi/
│   ├── usb_midi_transport.hh     # UsbMidiTransport<MaxVersion>  [新規]
│   │                             #   - USB MIDI 1.0/2.0 パケット処理
│   │                             #   - RawInputQueue 投入
│   │                             #   - MidiInput/MidiOutput concept 実装
│   ├── usb_midi_descriptors.hh   # MIDI 2.0 ディスクリプタビルダー [新規]
│   │                             #   - GroupTerminalBlock
│   │                             #   - Alt Setting 1 (MIDI 2.0)
│   └── umidi_adapter.hh          # [廃止予定 → 移行期間のみ残す]

lib/umidi/include/
├── core/
│   ├── ump.hh                    # UMP32, UMP64 (既存)
│   ├── parser.hh                 # Parser, Serializer (既存)
│   └── convert.hh                # downconvert(UMP64→UMP32) [新規/拡張]
├── event.hh                      # Event, EventQueue (既存)
└── codec/
    └── decoder.hh                # テンプレートデコーダ (既存)
```

### 17-12. 実装計画への追加

セクション 10 の Phase 2 に MIDI 関連を統合:

#### Phase 2 (改訂): WinUSB + WebUSB + MIDI 再設計

1. `UsbMidiTransport` を `midi/usb_midi_transport.hh` に新規作成
   - USB MIDI 1.0 パケット → UMP32 変換 (MidiProcessor の CIN パースを移植)
   - UMP32 → USB MIDI 1.0 パケット変換 (send_midi の逆変換を移植)
   - RawInputQueue への投入 API
2. AudioInterface から MIDI 直接操作 API を段階的に除去
   - `MidiTransport_` テンプレートパラメータ追加 (デフォルト: NullMidiTransport)
   - on_rx の MIDI 分岐を MidiTransport_ に委譲
3. `umidi_adapter.hh` を非推奨化 (deprecated 警告)
4. WinUSB / WebUSB ディスクリプタ対応 (セクション 8-9)

#### Phase 4 に MIDI 2.0 を追加:

1. `UsbMidiTransport` に MIDI 2.0 (UMP ネイティブ) 受信を追加
2. `midi/usb_midi_descriptors.hh` に Group Terminal Block 等を追加
3. Alt Setting 0/1 の切り替え対応 (set_interface)
4. `umidi::downconvert()` の実装/拡張
5. RouteTable の MT=4 対応 (OS 側)
