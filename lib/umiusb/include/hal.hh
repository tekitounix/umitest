// SPDX-License-Identifier: MIT
// UMI-USB: Hardware Abstraction Layer Concept
#pragma once

#include <cstdint>
#include <concepts>
#include <functional>
#include "types.hh"

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

/// Callbacks from HAL to Device core
struct HalCallbacks {
    // USB bus reset
    std::function<void()> on_reset;

    // SETUP packet received (EP0)
    std::function<void(const SetupPacket&)> on_setup;

    // Data received on EP0 (DATA OUT stage)
    std::function<void(const uint8_t*, uint16_t)> on_ep0_rx;

    // Data received on non-EP0 endpoint
    std::function<void(uint8_t ep, const uint8_t*, uint16_t)> on_rx;

    // Transmit complete on endpoint
    std::function<void(uint8_t ep)> on_tx_complete;

    // Suspend/resume
    std::function<void()> on_suspend;
    std::function<void()> on_resume;
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
        if (callbacks.on_reset) callbacks.on_reset();
    }

    void notify_setup(const SetupPacket& setup) {
        if (callbacks.on_setup) callbacks.on_setup(setup);
    }

    void notify_ep0_rx(const uint8_t* data, uint16_t len) {
        if (callbacks.on_ep0_rx) callbacks.on_ep0_rx(data, len);
    }

    void notify_rx(uint8_t ep, const uint8_t* data, uint16_t len) {
        if (callbacks.on_rx) callbacks.on_rx(ep, data, len);
    }

    void notify_tx_complete(uint8_t ep) {
        if (callbacks.on_tx_complete) callbacks.on_tx_complete(ep);
    }

    void notify_suspend() {
        if (callbacks.on_suspend) callbacks.on_suspend();
    }

    void notify_resume() {
        if (callbacks.on_resume) callbacks.on_resume();
    }
};

}  // namespace umiusb
