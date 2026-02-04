// SPDX-License-Identifier: MIT
// UMI-USB: CompositeClass Tests — IAD, descriptor merging, dispatch
#include <umitest.hh>
using namespace umitest;
#include "core/composite_class.hh"
#include "midi/usb_midi_class.hh"
#include "stub_hal.hh"

using namespace umiusb;

// Minimal AudioClass stub for composite testing
struct StubAudioClass {
    static constexpr bool USES_IAD = true;

    uint8_t desc_buf[32]{};
    uint16_t desc_size = 0;

    void build_fake_descriptor(uint8_t num_ifaces) {
        // Build a minimal config descriptor
        desc_buf[0] = 9;
        desc_buf[1] = 0x02;
        desc_buf[2] = 18; // wTotalLength low
        desc_buf[3] = 0;  // wTotalLength high
        desc_buf[4] = num_ifaces;
        desc_buf[5] = 1;
        desc_buf[6] = 0;
        desc_buf[7] = 0xC0;
        desc_buf[8] = 50;
        // Fake interface descriptor
        desc_buf[9] = 9;
        desc_buf[10] = 0x04; // Interface
        desc_buf[11] = 0;    // iface number
        desc_size = 18;
    }

    std::span<const uint8_t> config_descriptor() const { return {desc_buf, desc_size}; }

    std::span<const uint8_t> bos_descriptor() const { return {}; }

    bool handle_vendor_request(const SetupPacket& /*setup*/, std::span<uint8_t>& /*response*/) { return false; }

    void on_configured(bool /*configured*/) {}

    bool handle_request(const SetupPacket& /*setup*/, std::span<uint8_t>& /*response*/) { return false; }

    void on_rx(uint8_t /*ep*/, std::span<const uint8_t> /*data*/) {}

    template <typename HalT>
    void configure_endpoints(HalT& /*hal*/) {}

    template <typename HalT>
    void on_sof(HalT& /*hal*/) {}

    template <typename HalT>
    void on_tx_complete(HalT& /*hal*/, uint8_t /*ep*/) {}
};

using TestMidiClass = UsbMidiClass<MidiPort<1, 3>, MidiPort<1, 3>>;

int main() {
    Suite s("usb_composite");

    s.section("CompositeClass merged descriptor with IAD");
    {
        StubAudioClass audio;
        audio.build_fake_descriptor(2);

        TestMidiClass midi;
        midi.build_descriptor(2, 3);

        CompositeClass<StubAudioClass, TestMidiClass> composite(audio, midi);
        composite.build_merged_descriptor(0, 4, 0x01, 0x00);

        auto desc = composite.config_descriptor();
        s.check(desc.size() > 9, "Merged descriptor has content");

        // Check config header
        s.check_eq(desc[0], uint8_t{9});
        s.check_eq(desc[1], uint8_t{0x02});

        // wTotalLength should match span size
        uint16_t total = static_cast<uint16_t>(desc[2]) | (static_cast<uint16_t>(desc[3]) << 8);
        s.check_eq(total, static_cast<uint16_t>(desc.size()));

        // bNumInterfaces = audio(2) + midi(2) = 4
        s.check_eq(desc[4], uint8_t{4});

        // IAD should be at offset 9
        s.check_eq(desc[9], uint8_t{8});
        s.check_eq(desc[10], uint8_t{0x0B});
        s.check_eq(desc[11], uint8_t{0});
        s.check_eq(desc[12], uint8_t{4});
        s.check_eq(desc[13], uint8_t{0x01});
    }

    s.section("CompositeClass dispatches on_configured to both");
    {
        StubAudioClass audio;
        TestMidiClass midi;
        midi.build_descriptor(0, 1);

        CompositeClass<StubAudioClass, TestMidiClass> composite(audio, midi);

        // Track state through callbacks
        static bool midi_rx_called = false;
        midi.set_midi_callback(
            [](uint8_t /*cable*/, const uint8_t* /*data*/, uint8_t /*len*/) { midi_rx_called = true; });

        // Send MIDI data to OUT endpoint
        uint8_t packet[] = {0x09, 0x90, 0x3C, 0x7F};
        composite.on_rx(3, std::span<const uint8_t>(packet, 4));
        s.check(midi_rx_called, "MIDI on_rx dispatched through composite");
    }

    s.section("CompositeClass configure_endpoints dispatches to both");
    {
        StubAudioClass audio;
        TestMidiClass midi;
        midi.build_descriptor(0, 1);

        CompositeClass<StubAudioClass, TestMidiClass> composite(audio, midi);
        StubHal hal;
        hal.init();

        composite.configure_endpoints(hal);
        s.check_eq(hal.num_configured_eps, uint8_t{2});
    }

    return s.summary();
}
