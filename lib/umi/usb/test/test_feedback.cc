// SPDX-License-Identifier: MIT
// UMI-USB: Feedback Strategy Tests
#include <umitest.hh>
using namespace umitest;
#include "audio/strategy/feedback_strategy.hh"
#include "audio/strategy/asrc_strategy.hh"

using namespace umiusb;

int main() {
    Suite s("usb_feedback");

    s.section("FeedbackStrategy concept");
    {
        static_assert(FeedbackStrategy<DefaultFeedbackStrategy<UacVersion::UAC1>>);
        static_assert(FeedbackStrategy<DefaultFeedbackStrategy<UacVersion::UAC2>>);
        s.check(true, "DefaultFeedbackStrategy satisfies FeedbackStrategy concept");
    }

    s.section("DefaultFeedbackStrategy basic operation");
    {
        DefaultFeedbackStrategy<UacVersion::UAC1> fb;
        fb.reset(48000);
        fb.set_buffer_half_size(128);

        // Nominal feedback for 48kHz: 48 << 14 = 786432
        uint32_t nominal = 48U << 14;
        s.check_eq(fb.get_feedback(), nominal);

        // Balanced buffer -> feedback should stay near nominal
        fb.update_from_buffer_level(128);  // writable == half_size -> balanced
        uint32_t balanced = fb.get_feedback();
        s.check(balanced >= nominal - 100 && balanced <= nominal + 100,
              "Balanced buffer -> feedback near nominal");

        // Underfilled (more writable space) -> feedback should increase
        fb.update_from_buffer_level(200);
        uint32_t underfilled = fb.get_feedback();
        s.check(underfilled > nominal, "Underfilled -> feedback > nominal");

        // Overfilled (less writable space) -> feedback should decrease
        fb.update_from_buffer_level(50);
        uint32_t overfilled = fb.get_feedback();
        s.check(overfilled < nominal, "Overfilled -> feedback < nominal");
    }

    s.section("Feedback bytes encoding");
    {
        DefaultFeedbackStrategy<UacVersion::UAC1> fb;
        fb.reset(48000);
        auto bytes = fb.get_feedback_bytes();
        // 48 << 14 = 786432 = 0x0C0000
        s.check_eq(bytes[0], uint8_t{0x00});
        s.check_eq(bytes[1], uint8_t{0x00});
        s.check_eq(bytes[2], uint8_t{0x0C});
    }

    s.section("AsrcStrategy concept");
    {
        static_assert(AsrcStrategy<PiLpfAsrc>);
        s.check(true, "PiLpfAsrc satisfies AsrcStrategy concept");
    }

    s.section("PiLpfAsrc basic operation");
    {
        PiLpfAsrc asrc;
        asrc.reset();

        // With update_interval=1, every call updates
        uint32_t rate = asrc.update(0x10000, 1);  // 1.0x target
        s.check_eq(rate, uint32_t{0x10000});

        // Push toward 1.001x - converges slowly due to small alpha (2863/2^32)
        for (int i = 0; i < 100000; ++i) {
            rate = asrc.update(0x10042, 1);  // ~1.001x
        }
        s.check(rate > 0x10000, "Rate moved above 1.0 toward target");
        s.check(rate <= 0x10042, "Rate does not overshoot target");
    }

    return s.summary();
}
