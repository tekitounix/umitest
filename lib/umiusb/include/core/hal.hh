// SPDX-License-Identifier: MIT
// UMI-USB: Hardware Abstraction Layer Concept
#pragma once

#include <cstdint>
#include <concepts>
#include "core/types.hh"

namespace umiusb {

// ============================================================================
// USB HAL Concept
// ============================================================================

/// Concept for USB Hardware Abstraction Layer
/// HAL backends (STM32, ESP32, etc.) must satisfy this interface
template<typename T>
concept Hal = requires(T& hal, const T& chal,
                       uint8_t ep, const uint8_t* data, uint16_t len,
                       const EndpointConfig& config) {
    // Initialization
    { hal.init() } -> std::same_as<void>;

    // Connection control
    { hal.connect() } -> std::same_as<void>;
    { hal.disconnect() } -> std::same_as<void>;

    // Address management
    { hal.set_address(uint8_t{}) } -> std::same_as<void>;

    // Endpoint operations
    { hal.ep_configure(config) } -> std::same_as<void>;
    { hal.ep_write(ep, data, len) } -> std::same_as<void>;
    { hal.ep_stall(ep, bool{}) } -> std::same_as<void>;
    { hal.ep_unstall(ep, bool{}) } -> std::same_as<void>;

    // Interrupt/polling
    { hal.poll() } -> std::same_as<void>;

    // Status
    { chal.is_connected() } -> std::convertible_to<bool>;
};

// ============================================================================
// HAL Event Callbacks
// ============================================================================

/// Callbacks from HAL to Device core (function pointers with context for embedded)
struct HalCallbacks {
    // Context pointer passed to all callbacks
    void* context = nullptr;

    // USB bus reset
    void (*on_reset)(void* ctx) = nullptr;

    // SETUP packet received (EP0)
    void (*on_setup)(void* ctx, const SetupPacket&) = nullptr;

    // Data received on EP0 (DATA OUT stage)
    void (*on_ep0_rx)(void* ctx, const uint8_t*, uint16_t) = nullptr;

    // Data received on non-EP0 endpoint
    void (*on_rx)(void* ctx, uint8_t endpoint, const uint8_t*, uint16_t) = nullptr;

    // Transmit complete on endpoint
    void (*on_tx_complete)(void* ctx, uint8_t endpoint) = nullptr;

    // Suspend/resume
    void (*on_suspend)(void* ctx) = nullptr;
    void (*on_resume)(void* ctx) = nullptr;

    // Start of Frame (for isochronous timing)
    void (*on_sof)(void* ctx) = nullptr;
};

// ============================================================================
// HAL Base (CRTP helper)
// ============================================================================

/// CRTP base class providing common HAL functionality
template<typename Derived>
class HalBase {
protected:
    uint8_t address_ = 0;
    bool connected_ = false;
    Speed speed_ = Speed::Full;

public:
    HalCallbacks callbacks{};

    [[nodiscard]] bool is_connected() const { return connected_; }
    [[nodiscard]] Speed get_speed() const { return speed_; }

protected:
    // Event notification helpers for derived classes
    void notify_reset() {
        address_ = 0;
        if (callbacks.on_reset != nullptr) {
            callbacks.on_reset(callbacks.context);
        }
    }

    void notify_setup(const SetupPacket& setup) {
        if (callbacks.on_setup != nullptr) {
            callbacks.on_setup(callbacks.context, setup);
        }
    }

    void notify_ep0_rx(const uint8_t* data, uint16_t len) {
        if (callbacks.on_ep0_rx != nullptr) {
            callbacks.on_ep0_rx(callbacks.context, data, len);
        }
    }

    void notify_rx(uint8_t endpoint, const uint8_t* data, uint16_t len) {
        if (callbacks.on_rx != nullptr) {
            callbacks.on_rx(callbacks.context, endpoint, data, len);
        }
    }

    void notify_tx_complete(uint8_t endpoint) {
        if (callbacks.on_tx_complete != nullptr) {
            callbacks.on_tx_complete(callbacks.context, endpoint);
        }
    }

    void notify_suspend() {
        if (callbacks.on_suspend != nullptr) {
            callbacks.on_suspend(callbacks.context);
        }
    }

    void notify_resume() {
        if (callbacks.on_resume != nullptr) {
            callbacks.on_resume(callbacks.context);
        }
    }
};

}  // namespace umiusb
