// =====================================================================
// UMI-OS Volume - Minimal UMIM (Single file)
// =====================================================================

#include <umim_adapter.hh>

using namespace umi;
using namespace umi::umim;

// ============================================================================
// Processor
// ============================================================================

class Volume {
public:
    void process(AudioContext& ctx) {
        const sample_t* in = ctx.input(0);
        sample_t* out = ctx.output(0);
        if (!in || !out) return;
        
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * volume_;
        }
    }
    
    void set_param(uint32_t id, float value) {
        if (id == 0) volume_ = value;
    }
    
    float get_param(uint32_t id) const {
        return (id == 0) ? volume_ : 0.0f;
    }
    
private:
    float volume_ = 1.0f;
};

// ============================================================================
// Params & Export
// ============================================================================

inline constexpr std::array<Param, 1> kParams = {{
    {"Volume", 0.0f, 1.0f, 1.0f, 0, ""},
}};

UMIM_EXPORT_NAMED(Volume, kParams, "umi-volume-processor");
