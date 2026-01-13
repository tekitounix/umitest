# UMI-OS DSP Library

Dependency-free DSP building blocks for audio synthesis and processing.

## Design Principles

- **No UMI-OS dependencies**: Pure C++ standard library only
- **No assert/log calls**: Hot path optimization
- **Inlinable `tick()` methods**: Minimal function call overhead
- **Concrete classes**: No virtual functions
- **Template-based polymorphism**: When abstraction is needed

## Components

### Oscillators (`oscillator.hh`)

| Class | Description |
|-------|-------------|
| `Phase` | Phase accumulator (0.0 to 1.0) |
| `Sine` | Pure sine wave |
| `SawNaive` | Naive sawtooth (aliases at high frequencies) |
| `SquareNaive` | Naive square wave with pulse width |
| `Triangle` | Triangle wave |
| `SawBL` | Bandlimited saw (PolyBLEP) |
| `SquareBL` | Bandlimited square (PolyBLEP) |

### Filters (`filter.hh`)

| Class | Description |
|-------|-------------|
| `OnePole` | Simple one-pole lowpass |
| `Biquad` | Transposed direct form II (LP/HP/BP/Notch) |
| `SVF` | State Variable Filter (simultaneous LP/HP/BP/Notch outputs) |

### Envelopes (`envelope.hh`)

| Class | Description |
|-------|-------------|
| `ADSR` | Attack-Decay-Sustain-Release envelope |
| `Ramp` | Linear ramp with target and rate |

### Utilities (`dsp.hh`)

| Function | Description |
|----------|-------------|
| `midi_to_freq(note)` | MIDI note to frequency (A4 = 440Hz) |
| `normalize_freq(freq, sr)` | Convert Hz to normalized frequency |
| `soft_clip(x)` | Soft clipping (tanh-like) |
| `hard_clip(x, limit)` | Hard clipping |
| `lerp(a, b, t)` | Linear interpolation |
| `db_to_gain(db)` | Decibels to linear gain |
| `gain_to_db(gain)` | Linear gain to decibels |

## Usage

```cpp
#include <dsp/dsp.hh>

using namespace umi::dsp;

// Simple synth voice
class Voice {
public:
    void set_frequency(float freq, float sample_rate) {
        freq_norm_ = freq / sample_rate;
    }

    float tick() {
        float osc = osc_.tick(freq_norm_);
        float env = env_.tick();
        return soft_clip(osc * env);
    }

    void note_on() { env_.gate(true); }
    void note_off() { env_.gate(false); }

private:
    SawBL osc_;
    ADSR env_;
    float freq_norm_ = 0.0f;
};
```

## Constants

Mathematical constants are defined in `constants.hh`:

```cpp
namespace umi::dsp {
    inline constexpr float kPi = 3.14159265358979323846f;  // or std::numbers::pi_v<float>
    inline constexpr float k2Pi = kPi * 2.0f;
}
```

## Thread Safety

DSP components are **not thread-safe**. Each audio thread should have its own instances.

## Performance Notes

- All `tick()` methods are designed to be inlined
- No heap allocations in hot paths
- No virtual function calls
- Use `-O2` or higher for best performance
