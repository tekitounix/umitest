// waveshaper_pure.cpp - Pure WASM (no emscripten runtime)
// WaveShaperTurbo: 2 iterations with E-B junction reuse (3 exp calls)
// + Oversampling support (1x, 2x, 4x) with polyphase decimation filter
//
// Build: emcc waveshaper_pure.cpp -O3 -s WASM=1 \
//   -s EXPORTED_FUNCTIONS="['_ws_init','_ws_set_sample_rate','_ws_set_c2','_ws_set_r36','_ws_set_oversampling','_ws_reset','_ws_get_input_ptr','_ws_get_output_ptr','_ws_process_block']" \
//   -s SIDE_MODULE=0 --no-entry -o waveshaper.wasm

#define WASM_EXPORT __attribute__((visibility("default")))

extern "C" {

// Circuit constants (TB-303 schematic)
constexpr float V_CC = 12.0f;
constexpr float V_BIAS = 5.33f;
constexpr float C10 = 10e-9f;     // Input coupling capacitor (0.01μF)

// 2SA733P transistor parameters (TB-303 service notes)
constexpr float V_T = 0.025865f;
constexpr float V_T_INV = 1.0f / V_T;
constexpr float V_CRIT = V_T * 40.0f;

constexpr float I_S = 5e-14f;
constexpr float BETA_F = 300.0f;
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);     // ≈ 0.9967
constexpr float BETA_R = 0.1f;
constexpr float ALPHA_R = BETA_R / (BETA_R + 1.0f);     // ≈ 0.0909

// Pre-computed conductances
constexpr float G34 = 1.0f / 10e3f;   // R34: 10kΩ
constexpr float G35 = 1.0f / 100e3f;  // R35: 100kΩ
constexpr float G45 = 1.0f / 22e3f;   // R45: 22kΩ

inline float clamp(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

inline float fabsf(float x) {
    return x < 0 ? -x : x;
}

inline float maxf(float a, float b) {
    return a > b ? a : b;
}

inline float fast_exp(float x) {
    x = clamp(x, -87.0f, 88.0f);
    union { float f; int i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;
    u.i = (int)(SCALE * x + SHIFT);
    float t = x - (float)(u.i - (int)SHIFT) / SCALE;
    u.f *= 1.0f + t * (1.0f + t * 0.5f);
    return u.f;
}

inline void diode_iv(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = fast_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = fast_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// =============================================================================
// Simple 2:1 Decimation Filter (moving average + sample)
// For oversampling of nonlinear processors, simple averaging works well
// =============================================================================
struct DecimationFilter {
    float z_prev;  // Previous output for continuity

    void reset() {
        z_prev = 0.0f;
    }

    // Process 2 samples, output 1 sample (2:1 decimation)
    // Simple average of two consecutive samples
    float process(float x0, float x1) {
        return 0.5f * (x0 + x1);
    }
};

// =============================================================================
// WaveShaperTurbo: 2 iterations with E-B junction reuse (3 exp calls)
// =============================================================================
struct WaveShaperTurbo {
    // State variables
    float v_c1, v_c2, v_b, v_e, v_c;
    // Coefficients (sample rate dependent)
    float dt, g_c1, g_c2;
    float c2;      // C11 capacitance
    float g36;     // R36 conductance
    // Schur complement pre-computed values
    float inv_j11, schur_j11_factor, schur_f1_factor;
    // Oversampling
    int os_factor;
    float os_dt;
    DecimationFilter decim1, decim2;  // For 2x and 4x

    void init() {
        v_c1 = 0.0f; v_c2 = 8.0f;
        v_b = 8.0f; v_e = 8.0f; v_c = V_BIAS;
        dt = 1.0f / 48000.0f;
        c2 = 1e-6f;
        g36 = 1.0f / 10e3f;
        os_factor = 1;
        os_dt = dt;
        decim1.reset();
        decim2.reset();
        updateCoeffs();
    }

    void updateCoeffs() {
        os_dt = dt / static_cast<float>(os_factor);
        g_c1 = C10 / os_dt;
        g_c2 = c2 / os_dt;

        float j11 = -g_c1 - G34;
        inv_j11 = 1.0f / j11;
        schur_j11_factor = G34 * G34 * inv_j11;
        schur_f1_factor = G34 * inv_j11;
    }

    void setSampleRate(float sr) {
        dt = 1.0f / sr;
        updateCoeffs();
    }

    void setC2(float c2_uF) {
        c2 = c2_uF * 1e-6f;
        updateCoeffs();
    }

    void setR36(float r36_kohm) {
        g36 = 1.0f / (r36_kohm * 1e3f);
    }

    void setOversampling(int factor) {
        if (factor != 1 && factor != 2 && factor != 4) factor = 1;
        os_factor = factor;
        decim1.reset();
        decim2.reset();
        updateCoeffs();
    }

    void reset() {
        v_c1 = 0.0f; v_c2 = 8.0f;
        v_b = 8.0f; v_e = 8.0f; v_c = V_BIAS;
        decim1.reset();
        decim2.reset();
    }

    // Single sample processing (Turbo algorithm: 3 exp calls for 2 iterations)
    float processSample(float v_in) {
        const float v_c1_prev = v_c1;
        const float v_c2_prev = v_c2;

        float v_cap = v_in - v_c1_prev;
        float vb = v_b;
        float ve = v_c2_prev;
        float vc = v_c;

        // === Iteration 1 (full evaluation) ===
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(ve - vb, i_ef, g_ef);
        diode_iv(vc - vb, i_cr, g_cr);

        {
            const float i_e = i_ef - ALPHA_R * i_cr;
            const float i_c = ALPHA_F * i_ef - i_cr;
            const float i_b = i_e - i_c;

            const float f1 = g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - vb);
            const float f2 = G35 * (v_in - vb) + G34 * (v_cap - vb) + i_b;
            const float f3 = G45 * (V_CC - ve) - i_e - g_c2 * (ve - v_c2_prev);
            const float f4 = g36 * (V_BIAS - vc) + i_c;

            const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            const float j23 = (1.0f - ALPHA_F) * g_ef;
            const float j24 = (1.0f - ALPHA_R) * g_cr;
            const float j32 = g_ef - ALPHA_R * g_cr;
            const float j33 = -G45 - g_ef - g_c2;
            const float j34 = ALPHA_R * g_cr;
            const float j42 = -ALPHA_F * g_ef + g_cr;
            const float j43 = ALPHA_F * g_ef;
            const float j44 = -g36 - g_cr;

            const float j22_p = j22 - schur_j11_factor;
            const float f2_p = f2 - schur_f1_factor * f1;

            const float inv_j22_p = 1.0f / j22_p;
            const float m32 = j32 * inv_j22_p;
            const float m42 = j42 * inv_j22_p;

            const float j33_p = j33 - m32 * j23;
            const float j34_p = j34 - m32 * j24;
            const float f3_p = f3 - m32 * f2_p;

            const float j43_p = j43 - m42 * j23;
            const float j44_p = j44 - m42 * j24;
            const float f4_p = f4 - m42 * f2_p;

            const float det = j33_p * j44_p - j34_p * j43_p;
            const float inv_det = 1.0f / det;

            const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
            const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
            const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
            const float dv_cap = (-f1 - G34 * dv_b) * inv_j11;

            float max_dv = maxf(maxf(fabsf(dv_cap), fabsf(dv_b)),
                               maxf(fabsf(dv_e), fabsf(dv_c)));
            float damp = (max_dv > 0.7f) ? 0.7f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            vb += damp * dv_b;
            ve = clamp(ve + damp * dv_e, 0.0f, V_CC + 0.5f);
            vc = clamp(vc + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        // === Iteration 2 (E-B reuse, B-C only) ===
        diode_iv(vc - vb, i_cr, g_cr);

        {
            const float i_e = i_ef - ALPHA_R * i_cr;
            const float i_c = ALPHA_F * i_ef - i_cr;
            const float i_b = i_e - i_c;

            const float f1 = g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - vb);
            const float f2 = G35 * (v_in - vb) + G34 * (v_cap - vb) + i_b;
            const float f3 = G45 * (V_CC - ve) - i_e - g_c2 * (ve - v_c2_prev);
            const float f4 = g36 * (V_BIAS - vc) + i_c;

            const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            const float j23 = (1.0f - ALPHA_F) * g_ef;
            const float j24 = (1.0f - ALPHA_R) * g_cr;
            const float j32 = g_ef - ALPHA_R * g_cr;
            const float j33 = -G45 - g_ef - g_c2;
            const float j34 = ALPHA_R * g_cr;
            const float j42 = -ALPHA_F * g_ef + g_cr;
            const float j43 = ALPHA_F * g_ef;
            const float j44 = -g36 - g_cr;

            const float j22_p = j22 - schur_j11_factor;
            const float f2_p = f2 - schur_f1_factor * f1;

            const float inv_j22_p = 1.0f / j22_p;
            const float m32 = j32 * inv_j22_p;
            const float m42 = j42 * inv_j22_p;

            const float j33_p = j33 - m32 * j23;
            const float j34_p = j34 - m32 * j24;
            const float f3_p = f3 - m32 * f2_p;

            const float j43_p = j43 - m42 * j23;
            const float j44_p = j44 - m42 * j24;
            const float f4_p = f4 - m42 * f2_p;

            const float det = j33_p * j44_p - j34_p * j43_p;
            const float inv_det = 1.0f / det;

            const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
            const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
            const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
            const float dv_cap = (-f1 - G34 * dv_b) * inv_j11;

            float max_dv = maxf(maxf(fabsf(dv_cap), fabsf(dv_b)),
                               maxf(fabsf(dv_e), fabsf(dv_c)));
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            vb += damp * dv_b;
            ve = clamp(ve + damp * dv_e, 0.0f, V_CC + 0.5f);
            vc = clamp(vc + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        // Update state
        v_c1 = v_in - v_cap;
        v_c2 = ve;
        v_b = vb;
        v_e = ve;
        v_c = vc;

        return vc;
    }

    // Process with oversampling (linear interpolation for upsampling)
    float process(float v_in, float v_in_prev) {
        if (os_factor == 1) {
            return processSample(v_in);
        }

        if (os_factor == 2) {
            // 2x oversampling: interpolate input, decimate output
            float mid = 0.5f * (v_in_prev + v_in);
            float y0 = processSample(mid);
            float y1 = processSample(v_in);
            return decim1.process(y0, y1);
        }

        // 4x oversampling
        float d = (v_in - v_in_prev) * 0.25f;
        float y0 = processSample(v_in_prev + d);
        float y1 = processSample(v_in_prev + d * 2.0f);
        float y2 = processSample(v_in_prev + d * 3.0f);
        float y3 = processSample(v_in);

        // Two-stage decimation: 4x -> 2x -> 1x
        float z0 = decim1.process(y0, y1);
        float z1 = decim1.process(y2, y3);
        return decim2.process(z0, z1);
    }
};

// Global instance and buffers
static WaveShaperTurbo ws;
static float inputBuf[128];
static float outputBuf[128];
static float lastInputSample = 8.0f;  // Previous block's last sample for interpolation

WASM_EXPORT void ws_init() {
    ws.init();
}

WASM_EXPORT void ws_set_sample_rate(float sr) {
    ws.setSampleRate(sr);
}

WASM_EXPORT void ws_set_c2(float c2_uF) {
    ws.setC2(c2_uF);
}

WASM_EXPORT void ws_set_r36(float r36_kohm) {
    ws.setR36(r36_kohm);
}

WASM_EXPORT void ws_set_oversampling(int factor) {
    ws.setOversampling(factor);
}

WASM_EXPORT void ws_reset() {
    ws.reset();
    lastInputSample = 8.0f;
}

WASM_EXPORT float* ws_get_input_ptr() {
    return inputBuf;
}

WASM_EXPORT float* ws_get_output_ptr() {
    return outputBuf;
}

WASM_EXPORT void ws_process_block(int len) {
    if (ws.os_factor == 1) {
        // No oversampling: direct processing
        for (int i = 0; i < len; i++) {
            outputBuf[i] = ws.processSample(inputBuf[i]);
        }
        if (len > 0) {
            lastInputSample = inputBuf[len - 1];
        }
    } else {
        // With oversampling: need previous sample for interpolation
        float prev = lastInputSample;
        for (int i = 0; i < len; i++) {
            outputBuf[i] = ws.process(inputBuf[i], prev);
            prev = inputBuf[i];
        }
        if (len > 0) {
            lastInputSample = inputBuf[len - 1];
        }
    }
}

} // extern "C"
