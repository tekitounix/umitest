// SPDX-License-Identifier: MIT
// UMI-USB: Feedback Strategy Tests
#include "test_common.hh"
#include "audio/strategy/feedback_strategy.hh"
#include "audio/strategy/asrc_strategy.hh"

using namespace umiusb;

int main() {
    SECTION("FeedbackStrategy concept");
    {
        static_assert(FeedbackStrategy<DefaultFeedbackStrategy<UacVersion::UAC1>>);
        static_assert(FeedbackStrategy<DefaultFeedbackStrategy<UacVersion::UAC2>>);
        CHECK(true, "DefaultFeedbackStrategy satisfies FeedbackStrategy concept");
    }

    SECTION("DefaultFeedbackStrategy basic operation");
    {
        DefaultFeedbackStrategy<UacVersion::UAC1> fb;
        fb.reset(48000);
        fb.set_buffer_half_size(128);

        // Nominal feedback for 48kHz: 48 << 14 = 786432
        uint32_t nominal = 48U << 14;
        CHECK_EQ(fb.get_feedback(), nominal, "Initial feedback = nominal 48kHz");

        // Balanced buffer -> feedback should stay near nominal
        fb.update_from_buffer_level(128);  // writable == half_size -> balanced
        uint32_t balanced = fb.get_feedback();
        CHECK(balanced >= nominal - 100 && balanced <= nominal + 100,
              "Balanced buffer -> feedback near nominal");

        // Underfilled (more writable space) -> feedback should increase
        fb.update_from_buffer_level(200);
        uint32_t underfilled = fb.get_feedback();
        CHECK(underfilled > nominal, "Underfilled -> feedback > nominal");

        // Overfilled (less writable space) -> feedback should decrease
        fb.update_from_buffer_level(50);
        uint32_t overfilled = fb.get_feedback();
        CHECK(overfilled < nominal, "Overfilled -> feedback < nominal");
    }

    SECTION("Feedback bytes encoding");
    {
        DefaultFeedbackStrategy<UacVersion::UAC1> fb;
        fb.reset(48000);
        auto bytes = fb.get_feedback_bytes();
        // 48 << 14 = 786432 = 0x0C0000
        CHECK_EQ(bytes[0], uint8_t{0x00}, "Feedback byte 0");
        CHECK_EQ(bytes[1], uint8_t{0x00}, "Feedback byte 1");
        CHECK_EQ(bytes[2], uint8_t{0x0C}, "Feedback byte 2");
    }

    SECTION("AsrcStrategy concept");
    {
        static_assert(AsrcStrategy<PiLpfAsrc>);
        CHECK(true, "PiLpfAsrc satisfies AsrcStrategy concept");
    }

    SECTION("PiLpfAsrc basic operation");
    {
        PiLpfAsrc asrc;
        asrc.reset();

        // With update_interval=1, every call updates
        uint32_t rate = asrc.update(0x10000, 1);  // 1.0x target
        CHECK_EQ(rate, uint32_t{0x10000}, "Initial rate = 1.0");

        // Push toward 1.001x - converges slowly due to small alpha (2863/2^32)
        for (int i = 0; i < 100000; ++i) {
            rate = asrc.update(0x10042, 1);  // ~1.001x
        }
        CHECK(rate > 0x10000, "Rate moved above 1.0 toward target");
        CHECK(rate <= 0x10042, "Rate does not overshoot target");
    }

    TEST_SUMMARY();
}
