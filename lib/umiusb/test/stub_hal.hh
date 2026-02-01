// SPDX-License-Identifier: MIT
// UMI-USB: Stub HAL for host unit tests
#pragma once

#include <cstdint>
#include <cstring>
#include "core/hal.hh"

namespace umiusb {

/// Test stub satisfying the Hal concept.
/// Emulates EP buffers in memory for host-side unit testing.
struct StubHal : public HalBase<StubHal> {
    static constexpr uint8_t MAX_EP = 8;
    static constexpr uint16_t MAX_PKT = 512;

    // EP write capture (last ep_write call per endpoint)
    uint8_t ep_buf[MAX_EP][MAX_PKT]{};
    uint16_t ep_buf_len[MAX_EP]{};

    // Feedback state
    uint8_t fb_ep = 0;
    bool fb_tx_ready = true;
    bool fb_tx_flag = false;

    // EP0 rx state
    uint16_t ep0_rx_len = 0;

    // Stall state
    bool ep_stalled[MAX_EP]{};

    // Endpoint config capture
    EndpointConfig ep_configs[MAX_EP]{};
    uint8_t num_configured_eps = 0;

    // --- Hal concept required methods ---

    void init() {
        connected_ = true;
        speed_ = Speed::FULL;
    }

    void connect() { connected_ = true; }
    void disconnect() { connected_ = false; }

    void set_address(uint8_t addr) { address_ = addr; }

    void ep_configure(const EndpointConfig& config) {
        if (num_configured_eps < MAX_EP) {
            ep_configs[num_configured_eps++] = config;
        }
    }

    void ep_write(uint8_t ep, const uint8_t* data, uint16_t len) {
        if (ep < MAX_EP && len <= MAX_PKT) {
            if (data && len > 0) {
                std::memcpy(ep_buf[ep], data, len);
            }
            ep_buf_len[ep] = len;
        }
    }

    void ep_stall(uint8_t ep, bool /*in*/) {
        if (ep < MAX_EP) ep_stalled[ep] = true;
    }

    void ep_unstall(uint8_t ep, bool /*in*/) {
        if (ep < MAX_EP) ep_stalled[ep] = false;
    }

    void poll() {}

    void ep0_prepare_rx(uint16_t len) { ep0_rx_len = len; }

    void set_feedback_ep(uint8_t ep) { fb_ep = ep; }

    [[nodiscard]] bool is_feedback_tx_ready() const { return fb_tx_ready; }

    void set_feedback_tx_flag() { fb_tx_flag = true; }

    uint16_t ep_read(uint8_t ep, uint8_t* buf, uint16_t max_len) {
        if (ep < MAX_EP && ep_buf_len[ep] > 0) {
            uint16_t len = (ep_buf_len[ep] < max_len) ? ep_buf_len[ep] : max_len;
            std::memcpy(buf, ep_buf[ep], len);
            return len;
        }
        return 0;
    }

    void ep_set_nak(uint8_t /*ep*/) {}
    void ep_clear_nak(uint8_t /*ep*/) {}

    [[nodiscard]] bool is_ep_busy(uint8_t /*ep*/) const { return false; }

    // --- Test helpers ---

    void reset() {
        std::memset(ep_buf, 0, sizeof(ep_buf));
        std::memset(ep_buf_len, 0, sizeof(ep_buf_len));
        std::memset(ep_stalled, 0, sizeof(ep_stalled));
        num_configured_eps = 0;
        ep0_rx_len = 0;
        fb_ep = 0;
        fb_tx_ready = true;
        fb_tx_flag = false;
        address_ = 0;
        connected_ = false;
    }
};

// Verify StubHal satisfies the Hal concept
static_assert(Hal<StubHal>, "StubHal must satisfy the Hal concept");

}  // namespace umiusb
