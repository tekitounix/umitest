# 02 — Hal/Class concept 拡張と WinUSB/WebUSB 対応

> 元ドキュメント: [SPEED_SUPPORT_DESIGN.md](../SPEED_SUPPORT_DESIGN.md) セクション 7-9

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
