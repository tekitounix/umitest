// SPDX-License-Identifier: MIT
// UMI-USB: UsbMidiClass Tests
#include <umitest.hh>
using namespace umitest;
#include "midi/usb_midi_class.hh"
#include "stub_hal.hh"

using namespace umiusb;

// Verify UsbMidiClass satisfies Class concept
using TestMidiClass = UsbMidiClass<MidiPort<1, 3>, MidiPort<1, 3>>;

static_assert(
    requires(TestMidiClass& c, const SetupPacket& s) {
        { c.config_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;
        { c.on_configured(bool{}) } -> std::same_as<void>;
        { c.handle_request(s, std::declval<std::span<uint8_t>&>()) } -> std::convertible_to<bool>;
        { c.on_rx(uint8_t{}, std::declval<std::span<const uint8_t>>()) } -> std::same_as<void>;
        { c.bos_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;
        { c.handle_vendor_request(s, std::declval<std::span<uint8_t>&>()) } -> std::convertible_to<bool>;
    }, "UsbMidiClass must satisfy Class concept");

int main() {
    Suite s("usb_midi_class");

    s.section("UsbMidiClass descriptor build");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);

        auto desc = midi.config_descriptor();
        s.check(desc.size() > 9, "Descriptor has content");

        // First 9 bytes = Configuration descriptor header
        s.check_eq(desc[0], uint8_t{9});
        s.check_eq(desc[1], uint8_t{0x02});
        // wTotalLength should match span size
        uint16_t total = static_cast<uint16_t>(desc[2]) | (static_cast<uint16_t>(desc[3]) << 8);
        s.check_eq(total, static_cast<uint16_t>(desc.size()));
        s.check_eq(desc[4], uint8_t{2});
    }

    s.section("UsbMidiClass MIDI rx callback");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        midi.on_configured(true);

        // Track MIDI callback
        static uint8_t last_cable = 0xFF;
        static uint8_t last_len = 0;
        static uint8_t last_data[3] = {};
        midi.set_midi_callback([](uint8_t cable, const uint8_t* data, uint8_t len) {
            last_cable = cable;
            last_len = len;
            for (uint8_t i = 0; i < len && i < 3; ++i)
                last_data[i] = data[i];
        });

        // Simulate receiving a Note On message (CIN=0x09, cable 0)
        uint8_t packet[] = {0x09, 0x90, 0x3C, 0x7F}; // Note On, C4, velocity 127
        midi.on_rx(3, std::span<const uint8_t>(packet, 4));

        s.check_eq(last_cable, uint8_t{0});
        s.check_eq(last_len, uint8_t{3});
        s.check_eq(last_data[0], uint8_t{0x90});
        s.check_eq(last_data[1], uint8_t{0x3C});
        s.check_eq(last_data[2], uint8_t{0x7F});
    }

    s.section("UsbMidiClass configure_endpoints");
    {
        TestMidiClass midi;
        StubHal hal;
        hal.init();
        midi.configure_endpoints(hal);

        s.check_eq(hal.num_configured_eps, uint8_t{2});
    }

    s.section("UsbMidiClass send_midi");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        midi.on_configured(true);

        StubHal hal;
        hal.init();

        uint8_t msg[] = {0x90, 0x3C, 0x7F}; // Note On
        bool sent = midi.send_midi(hal, 0, msg, 3);
        s.check(sent, "send_midi returns true");
        s.check_eq(hal.ep_buf_len[3], uint16_t{4});
        s.check_eq(hal.ep_buf[3][0], uint8_t{0x09});
        s.check_eq(hal.ep_buf[3][1], uint8_t{0x90});
    }

    s.section("ump_word_count returns correct sizes");
    {
        s.check_eq(ump_word_count(0), uint8_t{1});
        s.check_eq(ump_word_count(1), uint8_t{1});
        s.check_eq(ump_word_count(2), uint8_t{1});
        s.check_eq(ump_word_count(3), uint8_t{2});
        s.check_eq(ump_word_count(4), uint8_t{2});
        s.check_eq(ump_word_count(5), uint8_t{4});
    }

    s.section("MIDI 2.0 Alt Setting switch");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        StubHal hal;
        hal.init();

        s.check_eq(static_cast<uint8_t>(midi.active_version()), uint8_t{0});

        midi.set_interface(hal, 1, 1); // Alt Setting 1 = MIDI 2.0
        s.check_eq(static_cast<uint8_t>(midi.active_version()), uint8_t{1});

        midi.set_interface(hal, 1, 0); // Back to Alt Setting 0
        s.check_eq(static_cast<uint8_t>(midi.active_version()), uint8_t{0});
    }

    s.section("MIDI 2.0 UMP native receive");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        StubHal hal;
        hal.init();

        // Switch to MIDI 2.0 mode
        midi.set_interface(hal, 1, 1);

        static uint16_t rx_len = 0;
        static uint8_t rx_data[16]{};
        midi.set_ump_rx_callback([](const uint8_t* data, uint16_t len) {
            rx_len = len;
            for (uint16_t i = 0; i < len && i < 16; ++i)
                rx_data[i] = data[i];
        });

        // MT=2 (MIDI 1.0 CV) = 1 word = 4 bytes, little-endian
        // UMP32: 0x20903C7F (MT=2, group=0, status=0x90, note=0x3C, vel=0x7F)
        uint8_t ump_data[] = {0x7F, 0x3C, 0x90, 0x20};
        midi.on_rx(3, std::span<const uint8_t>(ump_data, 4)); // EP_MIDI_OUT = 3

        s.check_eq(rx_len, uint16_t{4});
        s.check_eq(rx_data[3], uint8_t{0x20});
    }

    return s.summary();
}
