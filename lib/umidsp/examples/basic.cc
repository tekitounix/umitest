// SPDX-License-Identifier: MIT
// Basic umidsp example - Simple synth voice

#include <umidsp/umidsp.hh>
#include <cstdio>

using namespace umi::dsp;

int main() {
    constexpr float sample_rate = 48000.0f;
    constexpr float dt = 1.0f / sample_rate;

    // Simple synth voice
    SawBL osc;
    Biquad filter;
    ADSR env;

    // Setup
    float freq = midi_to_freq(60);  // C4
    float freq_norm = freq / sample_rate;
    filter.set_lowpass(0.1f, 2.0f);
    env.set_params(10, 100, 0.5f, 200);

    // Trigger note
    env.trigger();

    // Generate 100ms of audio
    for (int i = 0; i < 4800; ++i) {
        float sample = osc.tick(freq_norm);
        sample = filter.tick(sample);
        sample = soft_clip(sample * env.tick(dt));

        if (i % 480 == 0) {
            std::printf("Sample %d: %.3f\n", i, sample);
        }
    }

    return 0;
}
