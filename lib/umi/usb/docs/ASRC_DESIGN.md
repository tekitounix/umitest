# USB Audio ASRC (Asynchronous Sample Rate Conversion) Design

## Overview

This document describes the PI controller-based ASRC design for USB Audio Class
devices on microcontrollers without hardware PLL adjustment capability (e.g., STM32F4).

## Problem Statement

USB Audio Asynchronous mode requires the device to be the clock master. However:

1. **Host and device clocks drift independently** - USB spec allows ±500ppm
2. **No hardware PLL adjustment** - STM32F4 cannot dynamically adjust I2S clock
3. **Buffer overflow/underflow** - Clock drift causes buffer level changes over time

Solution: Software ASRC with PI control to maintain stable buffer levels.

## System Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Ring buffer size | 256 frames | 5.3ms @ 48kHz, absorbs USB jitter |
| DMA transfer size | 64 frames | 1.33ms, control loop update interval |
| Target buffer level | 128 frames (50%) | Equal margin for over/underflow |
| Sample rate | 48000 Hz | Standard USB audio rate |

## Clock Drift Analysis

```
USB Full Speed tolerance: ±500ppm (USB 2.0 spec)
Crystal oscillator: ±20-50ppm typical
Combined worst case: ~±550ppm

Drift rate @ 500ppm:
  48000 samples/sec × 0.0005 = 24 samples/sec
  = 0.024 frames/ms
  = ~1.5 frames per DMA period (64 frames @ 1.33ms)

Time to overflow (128 frame margin):
  128 frames / 1.5 frames per DMA = ~85 DMA cycles
  = ~113ms without correction
```

## PI Controller Design

### Parameters

```cpp
// Buffer level targets
TARGET_LEVEL = 128      // 50% of 256 frames
HYSTERESIS = 8          // ±8 frames dead zone

// Output limits
MAX_PPM_ADJUST = 1000   // ±1000ppm maximum adjustment

// PI gains
Kp = 2                  // Proportional gain
Ki = 0.02               // Integral gain (1/50)

// Anti-windup
INTEGRAL_MAX = 25000    // Limits I contribution to ±500ppm
```

### Gain Selection Rationale

**Kp = 2:**
- 1 frame error → 2 ppm adjustment
- Maximum error (64 frames) → 128 ppm correction
- Conservative value to avoid overshoot
- Sufficient response for 500ppm drift

**Ki = 0.02:**
- Slow integration for long-term drift correction
- 10 frame steady-state error → 500ppm correction in ~50 seconds
- Prevents hunting/oscillation
- Handles crystal drift over temperature changes

**HYSTERESIS = 8:**
- USB frame jitter causes ±1-2 frame variation
- 8 frames provides comfortable margin
- Prevents constant micro-adjustments
- Applied only to P term; I term always accumulates

### Stability Analysis

Discrete-time PI stability (simplified):
```
Kp × Ts < 2  (Ts = sampling period)

Current: Kp = 2, Ts = 1.33ms
Kp × Ts = 0.00266 << 2  ✓ Stable
```

### Transient Response

Recovery from empty buffer (worst case):
```
Initial error = -128 frames
P contribution = -128 × 2 = -256 ppm (slow down consumption)
→ Buffer fills toward target
→ Settles in ~1-2 seconds without overshoot
```

## Implementation

### Control Loop (called every DMA completion)

```cpp
int32_t PllRateController::update(int32_t buffer_level) {
    int32_t error = buffer_level - TARGET_LEVEL;

    // P term with dead zone
    int32_t p_contribution = 0;
    if (error < -HYSTERESIS || error > HYSTERESIS) {
        p_contribution = error * Kp;
    }

    // I term with trapezoidal integration
    integral_ += (error + prev_error_) / 2;
    prev_error_ = error;

    // Anti-windup clamp
    integral_ = clamp(integral_, -INTEGRAL_MAX, INTEGRAL_MAX);

    int32_t i_contribution = integral_ * Ki;

    // Total adjustment
    current_ppm_ = clamp(p_contribution + i_contribution,
                         -MAX_PPM_ADJUST, MAX_PPM_ADJUST);
    return current_ppm_;
}
```

### ASRC Interpolation

Cubic Hermite (Catmull-Rom) interpolation for high-quality resampling:

```cpp
// Phase accumulator in Q16.16 fixed-point
// rate = 0x10000 + (ppm * 65536 / 1000000)

int16_t interpolate(int16_t p0, int16_t p1, int16_t p2, int16_t p3,
                    uint32_t frac) {
    // Catmull-Rom coefficients
    int32_t a = -p0 + 3*p1 - 3*p2 + p3;
    int32_t b = 2*p0 - 5*p1 + 4*p2 - p3;
    int32_t c = -p0 + p2;
    int32_t d = 2*p1;

    // Evaluate polynomial: (a*t³ + b*t² + c*t + d) / 2
    int32_t t = frac >> 8;  // 8-bit fraction for efficiency
    return (((a*t/256 + b)*t/256 + c)*t/256 + d) / 2;
}
```

## Application to Audio IN vs Audio OUT

### Audio OUT (Playback: Host → Device)

```
USB RX → Ring Buffer → ASRC → DAC

Buffer high → increase consumption rate (positive ppm)
Buffer low  → decrease consumption rate (negative ppm)
```

### Audio IN (Recording: Device → Host)

```
ADC → Ring Buffer → ASRC → USB TX

Buffer high → decrease production rate (negative ppm)
Buffer low  → increase production rate (positive ppm)
```

**Key difference**: The PI controller output polarity is inverted for Audio IN.

For Audio IN:
```cpp
int32_t ppm_adjust = -pll_controller_.update(buffer_level);
```

Or equivalently, negate the error:
```cpp
int32_t error = TARGET_LEVEL - buffer_level;  // Note: reversed
```

### Feedback Endpoint

Both directions use the same feedback endpoint mechanism:
- Reports actual sample rate to host
- Host adjusts its send/receive rate accordingly
- Feedback format: 10.14 (UAC1) or 16.16 (UAC2) fixed-point

## Performance

Measured on STM32F4 @ 168MHz:

| Operation | Cycles | Time |
|-----------|--------|------|
| PI controller update | ~50 | 0.3μs |
| Cubic interpolation per sample | ~50 | 0.3μs |
| Full 64-frame ASRC | ~8,800 | 52μs |
| Available budget (1.33ms) | 224,000 | 1,333μs |
| **CPU usage** | | **~4%** |

## Tuning Guidelines

If experiencing issues:

1. **Oscillation/hunting**: Reduce Kp (try 1)
2. **Slow response**: Increase Kp (try 4) or Ki (try 0.05)
3. **Audible artifacts**: Increase HYSTERESIS (try 16)
4. **Buffer overflow/underflow**: Check MAX_PPM_ADJUST covers actual drift

### Diagnostic Information

Monitor these values for tuning:
- `buffer_level`: Should hover around TARGET_LEVEL (128)
- `current_ppm()`: Should stabilize to a constant value
- `integral()`: Should converge to a steady value

## Comparison with Hardware Approaches

| Approach | Pros | Cons |
|----------|------|------|
| **Software ASRC (this)** | Works on any MCU, flexible | CPU overhead, interpolation artifacts |
| **Hardware PLL** | Zero CPU, perfect quality | Requires PLL with fine adjustment |
| **XMOS hardware** | Precise MCLK counting | Requires dedicated hardware |
| **Adaptive mode** | Simple | Device follows host (not recommended) |

## References

- USB Audio Class 1.0/2.0 Specifications
- "Digital Audio Signal Processing" by Zölzer
- ARM Cortex-M4 Technical Reference Manual
