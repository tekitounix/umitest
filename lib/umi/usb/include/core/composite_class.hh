// SPDX-License-Identifier: MIT
// UMI-USB: Composite Class — combines multiple USB Classes into one
// Satisfies the Class concept by dispatching to sub-classes
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include "core/types.hh"
#include "core/descriptor.hh"

namespace umiusb {

/// Composite USB Class combining two sub-classes (e.g., Audio + MIDI).
/// Dispatches Class concept methods to the appropriate sub-class
/// based on endpoint numbers and interface numbers.
///
/// When UseIAD is true, generates an Interface Association Descriptor
/// grouping all interfaces under ClassA's function class.
///
/// Satisfies the Class concept.
template<typename ClassA, typename ClassB, bool UseIAD = true>
class CompositeClass {
public:
    static constexpr bool USES_IAD = UseIAD;

    CompositeClass(ClassA& a, ClassB& b) : a_(a), b_(b) {}

    /// Build the merged configuration descriptor.
    /// Must be called after both sub-classes have built their descriptors.
    /// @param iad_first_iface First interface number in the IAD group
    /// @param iad_iface_count Total number of interfaces in the IAD group
    /// @param iad_class Function class (e.g., 0x01 for Audio)
    /// @param iad_subclass Function subclass
    void build_merged_descriptor(uint8_t iad_first_iface, uint8_t iad_iface_count,
                                  uint8_t iad_class = 0x01, uint8_t iad_subclass = 0x00) {
        auto desc_a = a_.config_descriptor();
        auto desc_b = b_.config_descriptor();

        std::size_t pos = 0;

        // Skip config header from A (first 9 bytes) — we'll write our own
        const uint8_t* body_a = desc_a.data() + 9;
        std::size_t body_a_len = desc_a.size() - 9;

        // Skip config header from B (first 9 bytes)
        const uint8_t* body_b = desc_b.data() + 9;
        std::size_t body_b_len = desc_b.size() - 9;

        // IAD size
        constexpr std::size_t iad_size = UseIAD ? 8 : 0;

        // Total length
        std::size_t total = 9 + iad_size + body_a_len + body_b_len;
        if (total > merged_desc_.size()) total = merged_desc_.size();

        // Configuration descriptor header
        uint8_t num_ifaces_a = (desc_a.size() >= 5) ? desc_a[4] : 0;
        uint8_t num_ifaces_b = (desc_b.size() >= 5) ? desc_b[4] : 0;
        uint8_t total_ifaces = num_ifaces_a + num_ifaces_b;

        merged_desc_[pos++] = 9;
        merged_desc_[pos++] = 0x02;  // Configuration
        merged_desc_[pos++] = static_cast<uint8_t>(total & 0xFF);
        merged_desc_[pos++] = static_cast<uint8_t>((total >> 8) & 0xFF);
        merged_desc_[pos++] = total_ifaces;
        merged_desc_[pos++] = 1;     // bConfigurationValue
        merged_desc_[pos++] = 0;     // iConfiguration
        merged_desc_[pos++] = 0xC0;  // bmAttributes (self-powered)
        merged_desc_[pos++] = 50;    // bMaxPower (100mA)

        // IAD (if enabled)
        if constexpr (UseIAD) {
            merged_desc_[pos++] = 8;
            merged_desc_[pos++] = 0x0B;  // Interface Association
            merged_desc_[pos++] = iad_first_iface;
            merged_desc_[pos++] = iad_iface_count;
            merged_desc_[pos++] = iad_class;
            merged_desc_[pos++] = iad_subclass;
            merged_desc_[pos++] = 0x00;  // bFunctionProtocol
            merged_desc_[pos++] = 0;     // iFunction
        }

        // Body from A
        if (pos + body_a_len <= merged_desc_.size()) {
            std::memcpy(&merged_desc_[pos], body_a, body_a_len);
            pos += body_a_len;
        }

        // Body from B
        if (pos + body_b_len <= merged_desc_.size()) {
            std::memcpy(&merged_desc_[pos], body_b, body_b_len);
            pos += body_b_len;
        }

        merged_desc_size_ = static_cast<uint16_t>(pos);
    }

    // --- Class concept required methods ---

    [[nodiscard]] std::span<const uint8_t> config_descriptor() const {
        if (merged_desc_size_ > 0) {
            return {merged_desc_.data(), merged_desc_size_};
        }
        // Fallback: return class A's descriptor
        return a_.config_descriptor();
    }

    [[nodiscard]] std::span<const uint8_t> bos_descriptor() const {
        auto bos = a_.bos_descriptor();
        if (!bos.empty()) return bos;
        return b_.bos_descriptor();
    }

    bool handle_vendor_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        if (a_.handle_vendor_request(setup, response)) return true;
        return b_.handle_vendor_request(setup, response);
    }

    void on_configured(bool configured) {
        a_.on_configured(configured);
        b_.on_configured(configured);
    }

    bool handle_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        if (a_.handle_request(setup, response)) return true;
        return b_.handle_request(setup, response);
    }

    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        a_.on_rx(ep, data);
        b_.on_rx(ep, data);
    }

    // --- Optional Class concept methods ---

    template<typename HalT>
    void configure_endpoints(HalT& hal) {
        a_.configure_endpoints(hal);
        b_.configure_endpoints(hal);
    }

    template<typename HalT>
    void set_interface(HalT& hal, uint8_t interface, uint8_t alt_setting) {
        if constexpr (requires { a_.set_interface(hal, interface, alt_setting); }) {
            a_.set_interface(hal, interface, alt_setting);
        }
        if constexpr (requires { b_.set_interface(hal, interface, alt_setting); }) {
            b_.set_interface(hal, interface, alt_setting);
        }
    }

    void on_ep0_rx(std::span<const uint8_t> data) {
        if constexpr (requires { a_.on_ep0_rx(data); }) {
            a_.on_ep0_rx(data);
        }
        if constexpr (requires { b_.on_ep0_rx(data); }) {
            b_.on_ep0_rx(data);
        }
    }

    template<typename HalT>
    void on_sof(HalT& hal) {
        a_.on_sof(hal);
        b_.on_sof(hal);
    }

    template<typename HalT>
    void on_tx_complete(HalT& hal, uint8_t ep) {
        a_.on_tx_complete(hal, ep);
        b_.on_tx_complete(hal, ep);
    }

    // --- Sub-class access ---

    ClassA& class_a() { return a_; }
    ClassB& class_b() { return b_; }
    const ClassA& class_a() const { return a_; }
    const ClassB& class_b() const { return b_; }

private:
    ClassA& a_;
    ClassB& b_;

    static constexpr std::size_t MAX_MERGED_DESC = 512;
    std::array<uint8_t, MAX_MERGED_DESC> merged_desc_{};
    uint16_t merged_desc_size_ = 0;
};

}  // namespace umiusb
