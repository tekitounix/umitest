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
#include <type_traits>

// Include shared DSP components
#include <dsp/dsp.hh>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define UMIM_EXPORT_ATTR __attribute__((used, visibility("default")))
#else
#define UMIM_EXPORT_ATTR
#endif

namespace umi {

// ============================================================================
// Common Types
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
