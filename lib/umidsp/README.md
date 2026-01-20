# umidsp - Audio DSP Library

Audio synthesis, processing, and sample rate conversion for embedded and real-time use.

## Features

- No external dependencies (pure C++ standard library)
- Inlinable `tick()` methods for hot path optimization
- No virtual functions or heap allocations
- Production-quality ASRC with PI rate control

## Structure

```
include/
├── umidsp.hh              # Main header (includes everything)
├── core/                  # Primitives
│   ├── constants.hh       # Pi, TwoPi
│   ├── interpolate.hh     # Linear, Cubic, Sinc4
│   ├── phase.hh           # Phase accumulators
│   └── pi_controller.hh   # PI rate controller
├── synth/                 # Synthesis
│   ├── oscillator.hh      # Sine, Saw, Pulse, etc.
│   ├── filter.hh          # OnePole, Biquad
│   └── envelope.hh        # ADSR
└── rate/                  # Sample rate conversion
    └── asrc.hh            # ASRC processor
```

## Quick Start

```cpp
#include <umidsp/umidsp.hh>

using namespace umidsp;

SawBL osc;
ADSR env;
env.set_params(10, 100, 0.5f, 200);  // ms: attack, decay, sustain, release

float sample_rate = 48000.0f;
float freq_norm = 440.0f / sample_rate;

env.trigger();
float sample = soft_clip(osc.tick(freq_norm) * env.tick(1.0f / sample_rate));
```

## Build

```bash
xmake build umidsp_test
xmake run umidsp_test
```
