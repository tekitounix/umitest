// SPDX-License-Identifier: MIT
// UMI-USB: Gain Processor and Sample Codec Tests
#include "test_common.hh"
#include "audio/strategy/gain_processor.hh"
#include "audio/strategy/sample_codec.hh"

using namespace umiusb;

int main() {
    SECTION("GainProcessor concept");
    {
        static_assert(GainProcessor<DefaultGain<int32_t>, int32_t>);
        static_assert(GainProcessor<DefaultGain<int16_t>, int16_t>);
        CHECK(true, "DefaultGain satisfies GainProcessor concept");
    }

    SECTION("DefaultGain mute");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {1000, -2000, 3000, -4000};
        gain.set_mute(true);
        gain.apply(buf, 2, 2);
        CHECK_EQ(buf[0], int32_t{0}, "Muted sample 0");
        CHECK_EQ(buf[3], int32_t{0}, "Muted sample 3");
    }

    SECTION("DefaultGain unity (0 dB)");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {1000, -2000};
        gain.set_volume_db256(0);
        gain.apply(buf, 1, 2);
        CHECK_EQ(buf[0], int32_t{1000}, "Unity gain sample 0");
        CHECK_EQ(buf[1], int32_t{-2000}, "Unity gain sample 1");
    }

    SECTION("DefaultGain attenuation");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {10000, -10000};
        gain.set_volume_db256(-64);  // Some attenuation
        gain.apply(buf, 1, 2);
        CHECK(buf[0] < 10000 && buf[0] > 0, "Attenuated positive sample");
        CHECK(buf[1] > -10000 && buf[1] < 0, "Attenuated negative sample");
    }

    SECTION("DefaultGain deep attenuation -> silence");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {10000, -10000};
        gain.set_volume_db256(-32768);  // Very deep attenuation
        gain.apply(buf, 1, 2);
        CHECK_EQ(buf[0], int32_t{0}, "Deep attenuation -> silence 0");
        CHECK_EQ(buf[1], int32_t{0}, "Deep attenuation -> silence 1");
    }

    SECTION("SampleCodec concept");
    {
        static_assert(SampleCodec<DefaultSampleCodec<int32_t>, int32_t>);
        CHECK(true, "DefaultSampleCodec satisfies SampleCodec concept");
    }

    SECTION("DefaultSampleCodec i16 roundtrip");
    {
        DefaultSampleCodec<int32_t> codec;
        int32_t decoded = codec.decode_i16(256);
        CHECK_EQ(decoded, int32_t{256 << 8}, "decode_i16: 256 -> 65536");

        int16_t encoded = codec.encode_i16(65536);
        CHECK_EQ(encoded, int16_t{256}, "encode_i16: 65536 -> 256");

        // Roundtrip
        int16_t original = 1234;
        int16_t roundtrip = codec.encode_i16(codec.decode_i16(original));
        CHECK_EQ(roundtrip, original, "i16 roundtrip preserves value");
    }

    SECTION("DefaultSampleCodec i24");
    {
        DefaultSampleCodec<int32_t> codec;

        // Positive value: 0x123456 is not valid 24-bit signed, use 0x1234 instead
        uint8_t pos_data[] = {0x34, 0x12, 0x00};  // 0x001234 = 4660
        int32_t decoded_pos = codec.decode_i24(pos_data);
        CHECK_EQ(decoded_pos, int32_t{0x001234}, "decode_i24 positive");

        // Negative value: 0xFFF000 in 24-bit = -4096
        uint8_t neg_data[] = {0x00, 0xF0, 0xFF};
        int32_t decoded_neg = codec.decode_i24(neg_data);
        CHECK(decoded_neg < 0, "decode_i24 negative sign");
        CHECK_EQ(decoded_neg, int32_t{-4096}, "decode_i24 negative value");

        // Encode roundtrip
        uint8_t out[3]{};
        DefaultSampleCodec<int32_t>::encode_i24(4660, out);
        CHECK_EQ(out[0], uint8_t{0x34}, "encode_i24 byte 0");
        CHECK_EQ(out[1], uint8_t{0x12}, "encode_i24 byte 1");
        CHECK_EQ(out[2], uint8_t{0x00}, "encode_i24 byte 2");
    }

    TEST_SUMMARY();
}
