// SPDX-License-Identifier: MIT
// UMI-OS Synth - ProcessorLike Wrapper
//
// Wraps PolySynth to satisfy the ProcessorLike interface required by
// umios embedded adapter and web simulation adapter.
//
// Uses umios types directly (AudioContext, Event, EventType)
// Implements HasParams concept for parameter discovery

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "synth.hh"

// Include umios types
#include <umi/core/audio_context.hh>
#include <umi/core/event.hh>
#include <umi/core/processor.hh>

namespace umi::synth {

// ============================================================================
// Parameter Descriptors
// ============================================================================

// Parameter descriptors - curves are auto-inferred from name and range:
//   Attack/Decay/Release: Log (time params with >10x range)
//   Cutoff: Log (frequency param)
//   Sustain/Resonance/Volume: Linear (0-1 normalized)
inline constexpr std::array<umi::ParamDescriptor, PARAM_COUNT> kParamDescriptors = {{
    {static_cast<uint32_t>(ParamId::ATTACK), "Attack", 10.0f, 1.0f, 1000.0f},
    {static_cast<uint32_t>(ParamId::DECAY), "Decay", 100.0f, 1.0f, 2000.0f},
    {static_cast<uint32_t>(ParamId::SUSTAIN), "Sustain", 0.7f, 0.0f, 1.0f},
    {static_cast<uint32_t>(ParamId::RELEASE), "Release", 200.0f, 1.0f, 3000.0f},
    {static_cast<uint32_t>(ParamId::CUTOFF), "Cutoff", 2000.0f, 20.0f, 20000.0f},
    {static_cast<uint32_t>(ParamId::RESONANCE), "Resonance", 0.3f, 0.0f, 1.0f},
    {static_cast<uint32_t>(ParamId::VOLUME), "Volume", 1.0f, 0.0f, 1.0f},
}};

/// ProcessorLike wrapper for PolySynth.
/// Provides the standard process(AudioContext&) interface.
/// Satisfies HasParams concept for parameter discovery.
class SynthProcessor {
  public:
    SynthProcessor() = default;

    /// Initialize with sample rate
    void init(float sample_rate) {
        synth_.init(sample_rate);
        sample_rate_ = sample_rate;
    }

    /// Process audio block (ProcessorLike interface)
    void process(umi::AudioContext& ctx) {
        // Process input events (MIDI)
        for (const auto& ev : ctx.input_events) {
            if (ev.type == umi::EventType::MIDI) {
                const auto& midi = ev.midi;

                if (midi.is_note_on()) {
                    synth_.note_on(midi.note(), midi.velocity());
                } else if (midi.is_note_off()) {
                    synth_.note_off(midi.note());
                }
            }
        }

        // Get output buffer
        auto* out = ctx.output(0);
        if (!out)
            return;

        // Generate audio samples
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = synth_.process_sample();
        }
    }

    /// Direct MIDI access (for non-event-driven use)
    void note_on(uint8_t note, uint8_t velocity) { synth_.note_on(note, velocity); }

    void note_off(uint8_t note) { synth_.note_off(note); }

    /// Access underlying synth
    PolySynth& synth() { return synth_; }
    const PolySynth& synth() const { return synth_; }

    /// Get number of active voices (for DSP load simulation)
    uint32_t active_voice_count() const { return synth_.active_voice_count(); }

    // === Parameter Interface (HasParams concept) ===

    /// Get parameter descriptors
    static std::span<const umi::ParamDescriptor> params() { return kParamDescriptors; }

    /// Get parameter count
    static constexpr uint32_t param_count() { return PARAM_COUNT; }

    /// Set parameter value
    void set_param(uint32_t id, float value) {
        if (id < PARAM_COUNT) {
            synth_.set_param(static_cast<ParamId>(id), value);
        }
    }

    /// Get parameter value
    float get_param(uint32_t id) const {
        if (id < PARAM_COUNT) {
            return synth_.get_param(static_cast<ParamId>(id));
        }
        return 0.0f;
    }

    /// Get parameter descriptor by index
    static const umi::ParamDescriptor* get_param_descriptor(uint32_t id) {
        if (id < PARAM_COUNT) {
            return &kParamDescriptors[id];
        }
        return nullptr;
    }

  private:
    PolySynth synth_{};
    float sample_rate_ = 48000.0f;
};

} // namespace umi::synth
