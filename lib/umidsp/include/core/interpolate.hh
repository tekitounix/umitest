// SPDX-License-Identifier: MIT
// UMI-DSP: Interpolation Functions
//
// Primitive interpolation algorithms for sample rate conversion.
// All functions are stateless and can be inlined.
#pragma once

#include <cstdint>
#include <climits>

namespace umidsp {

// ============================================================================
// Interpolation Quality Levels
// ============================================================================

enum class InterpolateQuality {
    LINEAR,       // 2-point linear (fastest, lowest quality)
    CUBIC_HERMITE, // 4-point Catmull-Rom (balanced, recommended)
    SINC4,        // 4-point windowed sinc (highest quality)
};

// ============================================================================
// Interpolation Functions
// ============================================================================

namespace interpolate {

/// Linear interpolation (2-point) - fastest, lowest quality
/// y0, y1: two consecutive samples
/// t_q16: fractional position [0, 65535] between y0 and y1
inline int16_t linear_i16(int16_t y0, int16_t y1, uint32_t t_q16) {
    int32_t diff = y1 - y0;
    return static_cast<int16_t>(y0 + ((diff * static_cast<int32_t>(t_q16)) >> 16));
}

inline int32_t linear_i32(int32_t y0, int32_t y1, uint32_t t_q16) {
    int64_t diff = static_cast<int64_t>(y1) - y0;
    return static_cast<int32_t>(y0 + ((diff * static_cast<int64_t>(t_q16)) >> 16));
}

inline float linear_f(float y0, float y1, float t) {
    return y0 + (y1 - y0) * t;
}

/// Cubic Hermite (Catmull-Rom) - balanced quality/performance
/// y0, y1, y2, y3: four consecutive samples
/// t_q16: fractional position between y1 and y2 (Q0.16, 0-65535)
/// Returns: interpolated sample between y1 and y2
inline int16_t cubic_i16(int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                         uint32_t t_q16) {
    // Use Q8.8 for t to avoid overflow
    int32_t t = static_cast<int32_t>(t_q16 >> 8);

    // Catmull-Rom coefficients scaled by 2 to avoid 0.5 fractions
    int32_t a2 = -y0 + 3*y1 - 3*y2 + y3;
    int32_t b2 = 2*y0 - 5*y1 + 4*y2 - y3;
    int32_t c2 = -y0 + y2;
    int32_t d = y1;

    // Horner's method: ((a*t + b)*t + c)*t + d
    int32_t result = a2;
    result = (result * t) >> 8;
    result = result + b2;
    result = (result * t) >> 8;
    result = result + c2;
    result = (result * t) >> 9;  // Extra /2 for 2x scaling
    result = result + d;

    // Clamp to int16 range
    if (result > 32767) result = 32767;
    if (result < -32768) result = -32768;

    return static_cast<int16_t>(result);
}

inline int32_t cubic_i32(int32_t y0, int32_t y1, int32_t y2, int32_t y3,
                         uint32_t t_q16) {
    // Use full Q16 precision for smooth interpolation (was Q8.8 causing glitches)
    // Catmull-Rom: p(t) = 0.5*(-y0 + 3*y1 - 3*y2 + y3)*t^3
    //                + (y0 - 2.5*y1 + 2*y2 - 0.5*y3)*t^2
    //                + 0.5*(-y0 + y2)*t
    //                + y1

    // Fixed-point implementation: t is in Q0.16 format [0, 65535]
    // We use 32.64 arithmetic to maintain precision throughout

    // Pre-compute common terms
    const int64_t y0_64 = static_cast<int64_t>(y0);
    const int64_t y1_64 = static_cast<int64_t>(y1);
    const int64_t y2_64 = static_cast<int64_t>(y2);
    const int64_t y3_64 = static_cast<int64_t>(y3);
    const int64_t t_64 = static_cast<int64_t>(t_q16);

    // Catmull-Rom coefficients (scaled by 2 for fixed-point)
    // a = -y0 + 3*y1 - 3*y2 + y3
    // b = 2*y0 - 5*y1 + 4*y2 - y3
    // c = -y0 + y2
    // d = y1
    int64_t a = -y0_64 + 3*y1_64 - 3*y2_64 + y3_64;
    int64_t b = 2*y0_64 - 5*y1_64 + 4*y2_64 - y3_64;
    int64_t c = -y0_64 + y2_64;
    int64_t d = y1_64;

    // Horner's method with full Q16 precision
    // result = (((a * t / 65536) + b) * t / 65536 + c) * t / 65536 + d) / 2
    // Note: the /2 at the end accounts for the 2x scaling of coefficients

    int64_t result = a;
    result = (result * t_64) >> 16;
    result = result + b;
    result = (result * t_64) >> 16;
    result = result + c;
    result = (result * t_64) >> 16;
    result = result + d;

    // Final divide by 2 (from 2x coefficient scaling)
    result = result >> 1;

    if (result > INT32_MAX) result = INT32_MAX;
    if (result < INT32_MIN) result = INT32_MIN;

    return static_cast<int32_t>(result);
}

inline float cubic_f(float y0, float y1, float y2, float y3, float t) {
    float a = -0.5f*y0 + 1.5f*y1 - 1.5f*y2 + 0.5f*y3;
    float b = y0 - 2.5f*y1 + 2.0f*y2 - 0.5f*y3;
    float c = -0.5f*y0 + 0.5f*y2;
    float d = y1;
    return ((a*t + b)*t + c)*t + d;
}

/// 4-point windowed sinc - highest quality
/// y0, y1, y2, y3: four consecutive samples (window centered at y1-y2)
/// t_q16: fractional position between y1 and y2 (Q0.16, 0-65535)
inline int16_t sinc4_i16(int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                         uint32_t t_q16) {
    float t = static_cast<float>(t_q16) / 65536.0f;
    
    // Polynomial approximation of windowed sinc
    float t2 = t * t;
    float t3 = t2 * t;
    
    float c0 = -0.1667f*t + 0.1667f*t3;
    float c1 = 1.0f - 0.5f*t2 - 0.5f*t3 + 0.5f*t;
    float c2 = 0.5f*t + 0.5f*t2 + 0.3333f*t3;
    float c3 = -0.1667f*t3;
    
    float result = c0*y0 + c1*y1 + c2*y2 + c3*y3;
    
    if (result > 32767.0f) result = 32767.0f;
    if (result < -32768.0f) result = -32768.0f;
    
    return static_cast<int16_t>(result);
}

inline int32_t sinc4_i32(int32_t y0, int32_t y1, int32_t y2, int32_t y3,
                         uint32_t t_q16) {
    float t = static_cast<float>(t_q16) / 65536.0f;

    float t2 = t * t;
    float t3 = t2 * t;

    float c0 = -0.1667f * t + 0.1667f * t3;
    float c1 = 1.0f - 0.5f * t2 - 0.5f * t3 + 0.5f * t;
    float c2 = 0.5f * t + 0.5f * t2 + 0.3333f * t3;
    float c3 = -0.1667f * t3;

    float result = c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3;

    if (result > static_cast<float>(INT32_MAX)) result = static_cast<float>(INT32_MAX);
    if (result < static_cast<float>(INT32_MIN)) result = static_cast<float>(INT32_MIN);

    return static_cast<int32_t>(result);
}

inline float sinc4_f(float y0, float y1, float y2, float y3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    
    float c0 = -0.1667f*t + 0.1667f*t3;
    float c1 = 1.0f - 0.5f*t2 - 0.5f*t3 + 0.5f*t;
    float c2 = 0.5f*t + 0.5f*t2 + 0.3333f*t3;
    float c3 = -0.1667f*t3;
    
    return c0*y0 + c1*y1 + c2*y2 + c3*y3;
}

/// Dispatch interpolation by quality level
inline int16_t dispatch_i16(InterpolateQuality quality,
                            int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                            uint32_t t_q16) {
    switch (quality) {
        case InterpolateQuality::LINEAR:
            return linear_i16(y1, y2, t_q16);
        case InterpolateQuality::CUBIC_HERMITE:
            return cubic_i16(y0, y1, y2, y3, t_q16);
        case InterpolateQuality::SINC4:
            return sinc4_i16(y0, y1, y2, y3, t_q16);
    }
    return cubic_i16(y0, y1, y2, y3, t_q16);
}

inline int32_t dispatch_i32(InterpolateQuality quality,
                            int32_t y0, int32_t y1, int32_t y2, int32_t y3,
                            uint32_t t_q16) {
    switch (quality) {
        case InterpolateQuality::LINEAR:
            return linear_i32(y0, y1, t_q16);
        case InterpolateQuality::CUBIC_HERMITE:
            return cubic_i32(y0, y1, y2, y3, t_q16);
        case InterpolateQuality::SINC4:
            return sinc4_i32(y0, y1, y2, y3, t_q16);
    }
    return cubic_i32(y0, y1, y2, y3, t_q16);
}

}  // namespace interpolate

// Legacy namespace for backward compatibility
namespace cubic_hermite {

inline int16_t interpolate_i16(int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                                uint32_t t_q16) {
    return interpolate::cubic_i16(y0, y1, y2, y3, t_q16);
}

inline int32_t interpolate_i32(int32_t y0, int32_t y1, int32_t y2, int32_t y3,
                                uint32_t t_q16) {
    return interpolate::cubic_i32(y0, y1, y2, y3, t_q16);
}

inline int16_t interpolate_f(int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                              float t) {
    float result = interpolate::cubic_f(
        static_cast<float>(y0), static_cast<float>(y1),
        static_cast<float>(y2), static_cast<float>(y3), t);
    if (result > 32767.0f) result = 32767.0f;
    if (result < -32768.0f) result = -32768.0f;
    return static_cast<int16_t>(result);
}

inline void interpolate_stereo_i16(const int16_t* y0, const int16_t* y1,
                                    const int16_t* y2, const int16_t* y3,
                                    uint32_t t_q16, int16_t* out) {
    out[0] = interpolate_i16(y0[0], y1[0], y2[0], y3[0], t_q16);
    out[1] = interpolate_i16(y0[1], y1[1], y2[1], y3[1], t_q16);
}

}  // namespace cubic_hermite

}  // namespace umidsp
