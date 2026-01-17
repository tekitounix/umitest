// =====================================================================
// UMI-OS Synth - Web Simulation Entry Point
// =====================================================================
//
// This file provides the WASM entry point for simulating the embedded
// synthesizer in a web browser. Uses umios types directly with WebHwImpl
// providing the hardware abstraction layer simulation.
//
// Build:
//   xmake build synth_sim
//
// =====================================================================

#include "synth_processor.hh"
#include <umim/web_sim.hh>

// Export the SynthProcessor for web simulation
UMI_WEB_SIM_EXPORT_NAMED(umi::synth::SynthProcessor,
                          "UMI Synth Simulator",
                          "UMI-OS",
                          "1.0.0")

// Export kernel state access functions
UMI_WEB_SIM_EXPORT_KERNEL()

// Also provide UMIM-compatible exports for backward compatibility
extern "C" {

// UMIM process API (for AudioWorklet)
UMI_WEB_EXPORT void umi_create(void) {
    umi_sim_init();
}

UMI_WEB_EXPORT void umi_destroy(void) {
    umi_sim_reset();
}

UMI_WEB_EXPORT void umi_process(const float* input, float* output,
                                 uint32_t frames, uint32_t sample_rate) {
    umi_sim_process(input, output, frames, sample_rate);
}

UMI_WEB_EXPORT void umi_note_on(uint8_t note, uint8_t velocity) {
    umi_sim_note_on(note, velocity);
}

UMI_WEB_EXPORT void umi_note_off(uint8_t note) {
    umi_sim_note_off(note);
}

// Plugin info
UMI_WEB_EXPORT const char* umi_get_processor_name(void) {
    return "umi-synth-sim-processor";
}

UMI_WEB_EXPORT const char* umi_get_name(void) {
    return umi_sim_get_name();
}

UMI_WEB_EXPORT const char* umi_get_vendor(void) {
    return umi_sim_get_vendor();
}

UMI_WEB_EXPORT const char* umi_get_version(void) {
    return umi_sim_get_version();
}

UMI_WEB_EXPORT uint32_t umi_get_type(void) {
    return 1;  // Instrument
}

// Parameters - delegate to web_sim.hh generated functions
UMI_WEB_EXPORT uint32_t umi_get_param_count(void) {
    return umi::synth::SynthProcessor::param_count();
}
UMI_WEB_EXPORT void umi_set_param(uint32_t id, float value) {
    g_web_sim.processor().set_param(id, value);
}
UMI_WEB_EXPORT float umi_get_param(uint32_t id) {
    return g_web_sim.processor().get_param(id);
}
UMI_WEB_EXPORT const char* umi_get_param_name(uint32_t id) {
    auto* desc = umi::synth::SynthProcessor::get_param_descriptor(id);
    return desc ? desc->name.data() : "";
}
UMI_WEB_EXPORT float umi_get_param_min(uint32_t id) {
    auto* desc = umi::synth::SynthProcessor::get_param_descriptor(id);
    return desc ? desc->min_value : 0.0f;
}
UMI_WEB_EXPORT float umi_get_param_max(uint32_t id) {
    auto* desc = umi::synth::SynthProcessor::get_param_descriptor(id);
    return desc ? desc->max_value : 1.0f;
}
UMI_WEB_EXPORT float umi_get_param_default(uint32_t id) {
    auto* desc = umi::synth::SynthProcessor::get_param_descriptor(id);
    return desc ? desc->default_value : 0.0f;
}
UMI_WEB_EXPORT uint8_t umi_get_param_curve(uint32_t) { return 0; }  // Linear by default
UMI_WEB_EXPORT uint16_t umi_get_param_id(uint32_t id) {
    auto* desc = umi::synth::SynthProcessor::get_param_descriptor(id);
    return desc ? static_cast<uint16_t>(desc->id) : 0;
}
UMI_WEB_EXPORT const char* umi_get_param_unit(uint32_t) { return ""; }  // No units in current descriptors
UMI_WEB_EXPORT void umi_process_cc(uint8_t channel, uint8_t cc, uint8_t value) {
    g_web_sim.control_change(cc, value, channel);
}

} // extern "C"
