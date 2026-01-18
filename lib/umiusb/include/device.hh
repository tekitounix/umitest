// SPDX-License-Identifier: MIT
// UMI-USB: Device Core - Control Transfer and Descriptor Handling
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <functional>
#include "types.hh"
#include "hal.hh"

namespace umiusb {

// ============================================================================
// USB Class Concept
// ============================================================================

/// Concept for USB Class implementations (MIDI, CDC, HID, etc.)
template<typename T>
concept Class = requires(T& cls, const SetupPacket& setup) {
    // Get the complete configuration descriptor (including class-specific parts)
    { cls.config_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;

    // Called when device is configured/unconfigured
    { cls.on_configured(bool{}) } -> std::same_as<void>;

    // Handle class-specific SETUP request
    // Returns true if handled, fills response buffer if needed
    { cls.handle_request(setup, std::declval<std::span<uint8_t>&>()) }
        -> std::convertible_to<bool>;

    // Handle data received on non-EP0 endpoint
    { cls.on_rx(uint8_t{}, std::declval<std::span<const uint8_t>>()) }
        -> std::same_as<void>;
};

// ============================================================================
// Device Info
// ============================================================================

/// USB Device identification info
struct DeviceInfo {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version = 0x0100;  // BCD
    uint8_t manufacturer_idx = 1;      // String index
    uint8_t product_idx = 2;           // String index
    uint8_t serial_idx = 0;            // String index (0 = none)
};

// ============================================================================
// Device Core
// ============================================================================

/// USB Device Core
/// Handles standard requests, delegates class-specific to UsbClass
template<Hal HalT, Class ClassT>
class Device {
public:
    static constexpr uint16_t EP0_SIZE = 64;

private:
    HalT& hal_;
    ClassT& class_;
    DeviceInfo info_;

    // String descriptors (user provides array of pointers)
    std::span<const std::span<const uint8_t>> strings_{};

    // Device descriptor (built once)
    DeviceDescriptor device_desc_{};

    // State
    bool configured_ = false;

    // EP0 buffer for responses
    alignas(4) uint8_t ep0_buf_[EP0_SIZE];

public:
    Device(HalT& hal, ClassT& usb_class, const DeviceInfo& info)
        : hal_(hal), class_(usb_class), info_(info) {
        build_device_descriptor();
    }

    /// Set string descriptors (index 1, 2, 3, ...)
    void set_strings(std::span<const std::span<const uint8_t>> strings) {
        strings_ = strings;
    }

    /// Initialize the USB device
    void init() {
        // Wire up HAL callbacks
        hal_.callbacks.on_reset = [this]() { handle_reset(); };
        hal_.callbacks.on_setup = [this](const SetupPacket& s) { handle_setup(s); };
        hal_.callbacks.on_ep0_rx = [this](const uint8_t* d, uint16_t l) {
            handle_ep0_rx(d, l);
        };
        hal_.callbacks.on_rx = [this](uint8_t ep, const uint8_t* d, uint16_t l) {
            class_.on_rx(ep, std::span<const uint8_t>(d, l));
        };

        hal_.init();
    }

    /// Poll for USB events (call in main loop or from IRQ)
    void poll() { hal_.poll(); }

    /// Check if device is configured
    [[nodiscard]] bool is_configured() const { return configured_; }

    /// Direct HAL access (for class implementations)
    HalT& hal() { return hal_; }
    const HalT& hal() const { return hal_; }

private:
    void build_device_descriptor() {
        device_desc_.bcdUSB = 0x0200;  // USB 2.0
        device_desc_.bDeviceClass = bDeviceClass::PerInterface;
        device_desc_.bDeviceSubClass = 0;
        device_desc_.bDeviceProtocol = 0;
        device_desc_.bMaxPacketSize0 = EP0_SIZE;
        device_desc_.idVendor = info_.vendor_id;
        device_desc_.idProduct = info_.product_id;
        device_desc_.bcdDevice = info_.device_version;
        device_desc_.iManufacturer = info_.manufacturer_idx;
        device_desc_.iProduct = info_.product_idx;
        device_desc_.iSerialNumber = info_.serial_idx;
        device_desc_.bNumConfigurations = 1;
    }

    void handle_reset() {
        configured_ = false;
        class_.on_configured(false);
    }

    void handle_setup(const SetupPacket& setup) {
        switch (setup.type()) {
            case 0:  // Standard request
                handle_standard_request(setup);
                break;
            case 1:  // Class request
                handle_class_request(setup);
                break;
            case 2:  // Vendor request
            default:
                hal_.ep_stall(0, true);
                break;
        }
    }

    void handle_standard_request(const SetupPacket& setup) {
        switch (setup.bRequest) {
            case bRequest::GetDescriptor:
                handle_get_descriptor(setup);
                break;

            case bRequest::SetAddress:
                hal_.set_address(static_cast<uint8_t>(setup.wValue));
                send_zlp();
                break;

            case bRequest::SetConfiguration:
                configured_ = (setup.wValue != 0);
                class_.on_configured(configured_);
                send_zlp();
                break;

            case bRequest::GetConfiguration:
                ep0_buf_[0] = configured_ ? 1 : 0;
                send_response(ep0_buf_, 1, setup.wLength);
                break;

            case bRequest::GetStatus:
                ep0_buf_[0] = 0;  // Self-powered, no remote wakeup
                ep0_buf_[1] = 0;
                send_response(ep0_buf_, 2, setup.wLength);
                break;

            case bRequest::SetFeature:
            case bRequest::ClearFeature:
                handle_feature_request(setup);
                break;

            case bRequest::GetInterface:
                ep0_buf_[0] = 0;  // Alternate setting 0
                send_response(ep0_buf_, 1, setup.wLength);
                break;

            case bRequest::SetInterface:
                send_zlp();
                break;

            default:
                hal_.ep_stall(0, true);
                break;
        }
    }

    void handle_get_descriptor(const SetupPacket& setup) {
        const uint8_t* desc = nullptr;
        uint16_t len = 0;

        switch (setup.descriptor_type()) {
            case bDescriptorType::Device:
                desc = reinterpret_cast<const uint8_t*>(&device_desc_);
                len = sizeof(DeviceDescriptor);
                break;

            case bDescriptorType::Configuration: {
                auto cfg = class_.config_descriptor();
                desc = cfg.data();
                len = static_cast<uint16_t>(cfg.size());
                break;
            }

            case bDescriptorType::String:
                handle_string_descriptor(setup);
                return;

            default:
                break;
        }

        if (desc && len > 0) {
            send_response(desc, len, setup.wLength);
        } else {
            hal_.ep_stall(0, true);
        }
    }

    void handle_string_descriptor(const SetupPacket& setup) {
        uint8_t index = setup.descriptor_index();

        if (index == 0) {
            // Language ID descriptor
            static constexpr uint8_t lang_id[] = {4, bDescriptorType::String, 0x09, 0x04};
            send_response(lang_id, sizeof(lang_id), setup.wLength);
        } else if (index > 0 && static_cast<std::size_t>(index - 1) < strings_.size()) {
            auto str = strings_[index - 1];
            send_response(str.data(), static_cast<uint16_t>(str.size()), setup.wLength);
        } else {
            hal_.ep_stall(0, true);
        }
    }

    void handle_feature_request(const SetupPacket& setup) {
        if (setup.recipient() == 2) {  // Endpoint
            uint8_t ep = setup.wIndex & 0x0F;
            bool in = (setup.wIndex & 0x80) != 0;
            if (setup.bRequest == bRequest::SetFeature) {
                hal_.ep_stall(ep, in);
            } else {
                hal_.ep_unstall(ep, in);
            }
        }
        send_zlp();
    }

    void handle_class_request(const SetupPacket& setup) {
        std::span<uint8_t> response(ep0_buf_, EP0_SIZE);
        if (class_.handle_request(setup, response)) {
            if (setup.direction() == Direction::In && !response.empty()) {
                send_response(response.data(),
                              static_cast<uint16_t>(response.size()),
                              setup.wLength);
            } else {
                send_zlp();
            }
        } else {
            hal_.ep_stall(0, true);
        }
    }

    void handle_ep0_rx(const uint8_t* /*data*/, uint16_t /*len*/) {
        // DATA OUT stage - currently not used
    }

    void send_zlp() {
        hal_.ep_write(0, nullptr, 0);
    }

    void send_response(const uint8_t* data, uint16_t len, uint16_t max_len) {
        uint16_t send_len = (len < max_len) ? len : max_len;
        hal_.ep_write(0, data, send_len);
    }
};

}  // namespace umiusb
