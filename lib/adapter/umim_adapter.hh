// =====================================================================
// UMIM WASM Adapter - Auto-generates WASM exports from a Processor
// =====================================================================
// Usage:
//   1. Define your Processor with: process(), set_param(), get_param()
//   2. Define params metadata array
//   3. Use UMIM_EXPORT(ProcessorType, params_array) macro
// =====================================================================

#pragma once

#include <cstdint>
#include <array>
#include <type_traits>
#include <cmath>
#include <algorithm>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define UMIM_EXPORT_ATTR __attribute__((used, visibility("default")))
#else
#define UMIM_EXPORT_ATTR
#endif

namespace umi {

// ============================================================================
// Common Types (self-contained)
// ============================================================================

using sample_t = float;

// ============================================================================
// Minimal Event Queue (stub for WASM)
// ============================================================================

template<size_t Capacity = 64>
struct EventQueue {
    struct Event {
        uint32_t offset;
        uint8_t type;
        uint8_t data[3];
    };
    
    Event events[Capacity];
    size_t count = 0;
    
    auto begin() const { return events; }
    auto end() const { return events + count; }
    bool empty() const { return count == 0; }
};

// ============================================================================
// Audio Context
// ============================================================================

struct AudioContext {
    const sample_t* const* inputs;
    sample_t* const* outputs;
    size_t num_inputs;
    size_t num_outputs;
    EventQueue<>& events;
    uint32_t sample_rate;
    uint32_t buffer_size;
    float dt;
    uint64_t sample_position;
    
    const sample_t* input(size_t ch) const {
        return ch < num_inputs ? inputs[ch] : nullptr;
    }
    
    sample_t* output(size_t ch) const {
        return ch < num_outputs ? outputs[ch] : nullptr;
    }
};

// ============================================================================
// DSP Utilities (self-contained)
// ============================================================================

namespace dsp {

inline constexpr float PI = 3.14159265358979323846f;

// MIDI note to frequency
inline float midi_to_freq(uint8_t note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

// Soft clip distortion
inline float soft_clip(float x) {
    if (x > 1.0f) return 1.0f - 1.0f / (1.0f + (x - 1.0f));
    if (x < -1.0f) return -1.0f + 1.0f / (1.0f - (x + 1.0f));
    return x;
}

// ============================================================================
// ADSR Envelope
// ============================================================================

class ADSR {
public:
    enum class State { Idle, Attack, Decay, Sustain, Release };
    
    void set_params(float attack_ms, float decay_ms, float sustain, float release_ms) {
        attack_ms_ = attack_ms;
        decay_ms_ = decay_ms;
        sustain_ = sustain;
        release_ms_ = release_ms;
    }
    
    void trigger() {
        state_ = State::Attack;
        level_ = 0.0f;
    }
    
    void release() {
        if (state_ != State::Idle) state_ = State::Release;
    }
    
    float tick(float dt) {
        float rate;
        switch (state_) {
            case State::Attack:
                rate = 1000.0f / (attack_ms_ + 0.01f) * dt;
                level_ += rate;
                if (level_ >= 1.0f) { level_ = 1.0f; state_ = State::Decay; }
                break;
            case State::Decay:
                rate = 1000.0f / (decay_ms_ + 0.01f) * dt;
                level_ -= rate * (1.0f - sustain_);
                if (level_ <= sustain_) { level_ = sustain_; state_ = State::Sustain; }
                break;
            case State::Sustain:
                level_ = sustain_;
                break;
            case State::Release:
                rate = 1000.0f / (release_ms_ + 0.01f) * dt;
                level_ -= rate * sustain_;
                if (level_ <= 0.0f) { level_ = 0.0f; state_ = State::Idle; }
                break;
            default:
                break;
        }
        return level_;
    }
    
    State state() const { return state_; }
    
private:
    State state_ = State::Idle;
    float level_ = 0.0f;
    float attack_ms_ = 10.0f;
    float decay_ms_ = 100.0f;
    float sustain_ = 0.6f;
    float release_ms_ = 200.0f;
};

// ============================================================================
// State Variable Filter (SVF)
// ============================================================================

class SVF {
public:
    void set_params(float cutoff_norm, float resonance) {
        g_ = std::tan(PI * std::clamp(cutoff_norm, 0.001f, 0.49f));
        k_ = 2.0f - 2.0f * std::clamp(resonance, 0.0f, 0.99f);
    }
    
    void tick(float in) {
        float hp = (in - k_ * s1_ - s2_) / (1.0f + k_ * g_ + g_ * g_);
        float bp = g_ * hp + s1_;
        lp_ = g_ * bp + s2_;
        s1_ = g_ * hp + bp;
        s2_ = g_ * bp + lp_;
    }
    
    float lp() const { return lp_; }
    float bp() const { return s1_; }
    float hp() const { return (lp_ - s1_ * k_ - s2_) / (1.0f + k_ * g_ + g_ * g_); }
    
private:
    float g_ = 0.0f, k_ = 2.0f;
    float s1_ = 0.0f, s2_ = 0.0f;
    float lp_ = 0.0f;
};

// ============================================================================
// Band-Limited Sawtooth Oscillator (PolyBLEP)
// ============================================================================

class SawBL {
public:
    float tick(float freq_norm) {
        phase_ += freq_norm;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        
        float t = phase_;
        float saw = 2.0f * t - 1.0f;
        
        // PolyBLEP correction at discontinuity
        if (t < freq_norm) {
            float x = t / freq_norm;
            saw += (2.0f * x - x * x - 1.0f);
        } else if (t > 1.0f - freq_norm) {
            float x = (t - 1.0f) / freq_norm + 1.0f;
            saw += (2.0f * x - x * x - 1.0f);
        }
        
        return saw;
    }
    
private:
    float phase_ = 0.0f;
};

} // namespace dsp

} // namespace umi

namespace umi::umim {

// ============================================================================
// Parameter Metadata (common format for all adapters)
// ============================================================================

struct Param {
    const char* name;
    float min;
    float max;
    float default_value;
    uint8_t curve;      // 0=linear, 1=log
    const char* unit;
};

// ============================================================================
// Processor Concept (what user must implement)
// ============================================================================
// struct MyProcessor {
//     void process(AudioContext& ctx);
//     void set_param(uint32_t id, float value);
//     float get_param(uint32_t id) const;
//     
//     // Optional:
//     void note_on(uint8_t note, uint8_t velocity);
//     void note_off(uint8_t note);
// };

// ============================================================================
// SFINAE helpers for optional methods
// ============================================================================

template<typename T, typename = void>
struct has_note_on : std::false_type {};

template<typename T>
struct has_note_on<T, std::void_t<decltype(std::declval<T>().note_on(0, 0))>> : std::true_type {};

template<typename T, typename = void>
struct has_note_off : std::false_type {};

template<typename T>
struct has_note_off<T, std::void_t<decltype(std::declval<T>().note_off(0))>> : std::true_type {};

// ============================================================================
// WASM Adapter Template
// ============================================================================

template<typename Processor, const Param* Params, size_t ParamCount>
class WasmAdapter {
public:
    static Processor& instance() {
        static Processor proc;
        return proc;
    }
    
    static float& sample_rate() {
        static float sr = 48000.0f;
        return sr;
    }
    
    static void create(float sr) {
        sample_rate() = sr;
    }
    
    static void process(const float* input, float* output, uint32_t frames) {
        const umi::sample_t* inputs_arr[] = {input};
        umi::sample_t* outputs_arr[] = {output};
        umi::EventQueue<> events;
        
        umi::AudioContext ctx{
            .inputs = inputs_arr,
            .outputs = outputs_arr,
            .num_inputs = 1,
            .num_outputs = 1,
            .events = events,
            .sample_rate = static_cast<uint32_t>(sample_rate()),
            .buffer_size = frames,
            .dt = 1.0f / sample_rate(),
            .sample_position = 0,
        };
        
        instance().process(ctx);
    }
    
    static void note_on(uint8_t note, uint8_t velocity) {
        if constexpr (has_note_on<Processor>::value) {
            instance().note_on(note, velocity);
        }
    }
    
    static void note_off(uint8_t note) {
        if constexpr (has_note_off<Processor>::value) {
            instance().note_off(note);
        }
    }
    
    static void set_param(uint32_t index, float value) {
        instance().set_param(index, value);
    }
    
    static float get_param(uint32_t index) {
        return instance().get_param(index);
    }
    
    static uint32_t get_param_count() { return ParamCount; }
    
    static const char* get_param_name(uint32_t i) {
        return i < ParamCount ? Params[i].name : "";
    }
    
    static float get_param_min(uint32_t i) {
        return i < ParamCount ? Params[i].min : 0.0f;
    }
    
    static float get_param_max(uint32_t i) {
        return i < ParamCount ? Params[i].max : 1.0f;
    }
    
    static float get_param_default(uint32_t i) {
        return i < ParamCount ? Params[i].default_value : 0.0f;
    }
    
    static uint8_t get_param_curve(uint32_t i) {
        return i < ParamCount ? Params[i].curve : 0;
    }
    
    static const char* get_param_unit(uint32_t i) {
        return i < ParamCount ? Params[i].unit : "";
    }
};

} // namespace umi::umim

// ============================================================================
// UMIM_EXPORT Macro - Generates all WASM entry points
// ============================================================================

#define UMIM_EXPORT(ProcessorType, params_array) \
    using _UmiAdapter = umi::umim::WasmAdapter< \
        ProcessorType, \
        params_array.data(), \
        params_array.size() \
    >; \
    \
    extern "C" { \
        UMIM_EXPORT_ATTR void umi_create(float sr) { _UmiAdapter::create(sr); } \
        UMIM_EXPORT_ATTR void umi_process(const float* in, float* out, uint32_t frames) { \
            _UmiAdapter::process(in, out, frames); \
        } \
        UMIM_EXPORT_ATTR void umi_note_on(uint8_t n, uint8_t v) { _UmiAdapter::note_on(n, v); } \
        UMIM_EXPORT_ATTR void umi_note_off(uint8_t n) { _UmiAdapter::note_off(n); } \
        UMIM_EXPORT_ATTR void umi_set_param(uint32_t i, float v) { _UmiAdapter::set_param(i, v); } \
        UMIM_EXPORT_ATTR float umi_get_param(uint32_t i) { return _UmiAdapter::get_param(i); } \
        UMIM_EXPORT_ATTR uint32_t umi_get_param_count() { return _UmiAdapter::get_param_count(); } \
        UMIM_EXPORT_ATTR const char* umi_get_param_name(uint32_t i) { return _UmiAdapter::get_param_name(i); } \
        UMIM_EXPORT_ATTR float umi_get_param_min(uint32_t i) { return _UmiAdapter::get_param_min(i); } \
        UMIM_EXPORT_ATTR float umi_get_param_max(uint32_t i) { return _UmiAdapter::get_param_max(i); } \
        UMIM_EXPORT_ATTR float umi_get_param_default(uint32_t i) { return _UmiAdapter::get_param_default(i); } \
        UMIM_EXPORT_ATTR uint8_t umi_get_param_curve(uint32_t i) { return _UmiAdapter::get_param_curve(i); } \
        UMIM_EXPORT_ATTR const char* umi_get_param_unit(uint32_t i) { return _UmiAdapter::get_param_unit(i); } \
        UMIM_EXPORT_ATTR void umi_process_cc(uint8_t, uint8_t, uint8_t) {} \
    }

// UMIM_EXPORT with processor name (for AudioWorklet registration)
#define UMIM_EXPORT_NAMED(ProcessorType, params_array, processor_name) \
    UMIM_EXPORT(ProcessorType, params_array) \
    extern "C" { \
        UMIM_EXPORT_ATTR const char* umi_get_processor_name() { return processor_name; } \
    }
