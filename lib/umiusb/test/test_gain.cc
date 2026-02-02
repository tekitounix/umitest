// SPDX-License-Identifier: MIT
// UMI-USB: Gain Processor and Sample Codec Tests
#include <umitest.hh>
using namespace umitest;
#include "audio/strategy/gain_processor.hh"
#include "audio/strategy/sample_codec.hh"

using namespace umiusb;

int main() {
    Suite s("usb_gain");

    s.section("GainProcessor concept");
    {
        static_assert(GainProcessor<DefaultGain<int32_t>, int32_t>);
        static_assert(GainProcessor<DefaultGain<int16_t>, int16_t>);
        s.check(true, "DefaultGain satisfies GainProcessor concept");
    }

    s.section("DefaultGain mute");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {1000, -2000, 3000, -4000};
        gain.set_mute(true);
        gain.apply(buf, 2, 2);
        s.check_eq(buf[0], int32_t{0});
        s.check_eq(buf[3], int32_t{0});
    }

    s.section("DefaultGain unity (0 dB)");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {1000, -2000};
        gain.set_volume_db256(0);
        gain.apply(buf, 1, 2);
        s.check_eq(buf[0], int32_t{1000});
        s.check_eq(buf[1], int32_t{-2000});
    }

    s.section("DefaultGain attenuation");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {10000, -10000};
        gain.set_volume_db256(-64);  // Some attenuation
        gain.apply(buf, 1, 2);
        s.check(buf[0] < 10000 && buf[0] > 0, "Attenuated positive sample");
        s.check(buf[1] > -10000 && buf[1] < 0, "Attenuated negative sample");
    }

    s.section("DefaultGain deep attenuation -> silence");
    {
        DefaultGain<int32_t> gain;
        int32_t buf[] = {10000, -10000};
        gain.set_volume_db256(-32768);  // Very deep attenuation
        gain.apply(buf, 1, 2);
        s.check_eq(buf[0], int32_t{0});
        s.check_eq(buf[1], int32_t{0});
    }

    s.section("SampleCodec concept");
    {
        static_assert(SampleCodec<DefaultSampleCodec<int32_t>, int32_t>);
        s.check(true, "DefaultSampleCodec satisfies SampleCodec concept");
    }

    s.section("DefaultSampleCodec i16 roundtrip");
    {
        DefaultSampleCodec<int32_t> codec;
        int32_t decoded = codec.decode_i16(256);
        s.check_eq(decoded, int32_t{256 << 8});

        int16_t encoded = codec.encode_i16(65536);
        s.check_eq(encoded, int16_t{256});

        // Roundtrip
        int16_t original = 1234;
        int16_t roundtrip = codec.encode_i16(codec.decode_i16(original));
        s.check_eq(roundtrip, original);
    }

    s.section("DefaultSampleCodec i24");
    {
        DefaultSampleCodec<int32_t> codec;

        // Positive value: 0x123456 is not valid 24-bit signed, use 0x1234 instead
        uint8_t pos_data[] = {0x34, 0x12, 0x00};  // 0x001234 = 4660
        int32_t decoded_pos = codec.decode_i24(pos_data);
        s.check_eq(decoded_pos, int32_t{0x001234});

        // Negative value: 0xFFF000 in 24-bit = -4096
        uint8_t neg_data[] = {0x00, 0xF0, 0xFF};
        int32_t decoded_neg = codec.decode_i24(neg_data);
        s.check(decoded_neg < 0, "decode_i24 negative sign");
        s.check_eq(decoded_neg, int32_t{-4096});

        // Encode roundtrip
        uint8_t out[3]{};
        DefaultSampleCodec<int32_t>::encode_i24(4660, out);
        s.check_eq(out[0], uint8_t{0x34});
        s.check_eq(out[1], uint8_t{0x12});
        s.check_eq(out[2], uint8_t{0x00});
    }

    return s.summary();
}
