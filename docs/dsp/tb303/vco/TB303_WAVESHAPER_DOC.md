# TB-303 Wave Shaper - Technical Documentation

## Overview

This is a SPICE-accurate implementation of the Roland TB-303 bass synthesizer's wave shaper circuit. The implementation uses Newton-Raphson nodal analysis with a full Ebers-Moll PNP transistor model, matching the behavior of Falstad circuit simulator.

## Circuit Topology

```
                      v_in (12V → 5.5V sawtooth)
                        │
            ┌───────────┼───────────┐
            │           │           │
           R1          R2          C1
          10kΩ        100kΩ        10nF
            │           │           │
           GND      v_base ◄───────R3 (10kΩ)───── v_cap
                       │
                    [PNP B]
                       │
    V_CC (+12V)───R4 (22kΩ)───[E]      [C]───R5 (10kΩ)───V_COLL (+5.33V)
                              │         │
                             C2       v_out
                             1µF
                              │
                             GND
```

### Component Values

| Component | Value | Function |
|-----------|-------|----------|
| R1 | 10kΩ | Input load to GND |
| R2 | 100kΩ | Direct path: input → base |
| R3 | 10kΩ | Capacitor path: C1 → base |
| R4 | 22kΩ | Emitter resistor to +12V |
| R5 | 10kΩ | Collector resistor to +5.33V |
| C1 | 10nF | Memory capacitor (high-shelf filter) |
| C2 | 1µF | Emitter bypass capacitor |

### Power Supplies

- **V_CC**: +12V (emitter supply)
- **V_COLL**: +5.33V (collector supply)

## Operating Principle

### Input Signal
- Falling sawtooth wave: 12V → 5.5V
- Frequency range: typically 40Hz - 400Hz

### Wave Shaping Mechanism

1. **High-Shelf Filter (C1, R2, R3)**
   - C1 creates a differentiating network
   - Sharp transitions at sawtooth reset
   - Adds "edge" to the waveform

2. **PNP Transistor Switching**
   - When v_base < v_emitter - 0.6V: transistor ON
   - When v_base ≈ v_emitter: transistor OFF
   - Creates asymmetric duty cycle

3. **Emitter Capacitor (C2)**
   - 1µF provides slow voltage changes
   - Smooths emitter voltage transitions
   - Creates exponential decay curves

### Output Characteristics

| Frequency | V_min | V_max |
|-----------|-------|-------|
| 40Hz | 5.33V | ~8.8V |
| 80Hz | 5.33V | ~8.9V |
| 160Hz | 5.33V | ~8.8V |

## Mathematical Model

### Ebers-Moll PNP Transistor

Terminal currents (defined as flowing INTO terminal):

```
I_E = I_EF - α_R × I_CR
I_C = α_F × I_EF - I_CR
I_B = I_E - I_C
```

Where:
- `I_EF = I_S × (exp(V_EB/V_T) - 1)` (E-B junction current)
- `I_CR = I_S × (exp(V_CB/V_T) - 1)` (C-B junction current)
- `α_F = β_F / (β_F + 1) ≈ 0.99` (forward alpha)
- `α_R = β_R / (β_R + 1) ≈ 0.33` (reverse alpha)

### Transistor Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| V_T | 25.865mV | Thermal voltage (kT/q at 300K) |
| I_S | 1×10⁻¹³ A | Saturation current |
| β_F | 100 | Forward current gain |
| β_R | 0.5 | Reverse current gain |

### KCL Equations

Four node equations solved simultaneously:

1. **v_cap node**: `i_C1 = i_R3`
2. **v_base node**: `i_R2 + i_R3 + i_B = 0`
3. **v_emitter node**: `i_R4 - i_E - i_C2 = 0`
4. **v_collector node**: `i_R5 + i_C = 0`

### Capacitor Companion Model (Backward Euler)

```
i_C = G_eq × (v - v_prev)
G_eq = C / Δt
```

## Implementation Details

### Newton-Raphson Solver

1. **Initial guess**: Use previous timestep values
2. **Iterate**: Compute Jacobian, solve 4×4 linear system
3. **Damping**: Limit step size to 0.1V for stability
4. **Convergence**: |f₁| + |f₂| + |f₃| + |f₄| < 10⁻¹²

### Jacobian Matrix

```
J = [∂f₁/∂v_cap  ∂f₁/∂v_b   ∂f₁/∂v_e   ∂f₁/∂v_c ]
    [∂f₂/∂v_cap  ∂f₂/∂v_b   ∂f₂/∂v_e   ∂f₂/∂v_c ]
    [∂f₃/∂v_cap  ∂f₃/∂v_b   ∂f₃/∂v_e   ∂f₃/∂v_c ]
    [∂f₄/∂v_cap  ∂f₄/∂v_b   ∂f₄/∂v_e   ∂f₄/∂v_c ]
```

Transistor current derivatives:

```
∂i_E/∂v_E = g_ef
∂i_E/∂v_B = -g_ef + α_R × g_cr
∂i_E/∂v_C = -α_R × g_cr

∂i_C/∂v_E = α_F × g_ef
∂i_C/∂v_B = -α_F × g_ef + g_cr
∂i_C/∂v_C = -g_cr
```

Where:
- `g_ef = (I_S/V_T) × exp(V_EB/V_T)` (E-B diode conductance)
- `g_cr = (I_S/V_T) × exp(V_CB/V_T)` (C-B diode conductance)

### Numerical Protection

**Diode Current Limiting:**
- For V > 40×V_T (~1V): Linear extrapolation
- For V < -10×V_T: Return -I_S
- Prevents exp() overflow

**Conductance Floor:**
- Minimum conductance: 10⁻¹² S
- Prevents division by zero in Jacobian

## Usage

### Basic Example

```cpp
#include "tb303_waveshaper.hpp"

constexpr double SAMPLE_RATE = 48000.0;
constexpr double DT = 1.0 / SAMPLE_RATE;

tb303::WaveShaper waveshaper;

// Generate falling sawtooth 12V → 5.5V
double phase = 0.0;
double freq = 80.0;  // Hz

for (int i = 0; i < num_samples; ++i) {
    double v_in = 12.0 - phase * (12.0 - 5.5);
    double v_out = waveshaper.process(v_in, DT);

    // Use v_out...

    phase += freq / SAMPLE_RATE;
    if (phase >= 1.0) phase -= 1.0;
}
```

### Debugging

```cpp
// Access internal node voltages
double v_cap = waveshaper.getCapNodeVoltage();
double v_base = waveshaper.getBaseVoltage();
double v_emitter = waveshaper.getEmitterVoltage();
double v_collector = waveshaper.getCollectorVoltage();
```

## Performance Considerations

- **Iterations**: Typically 3-10 per sample (more during transitions)
- **Operations per iteration**: ~100 floating-point operations
- **Memory**: 8 double state variables (64 bytes)

### Optimization Opportunities

1. Use `float` instead of `double` (with adjusted tolerances)
2. Pre-compute constant conductances
3. Use SIMD for 4×4 matrix operations
4. Reduce iterations with better initial guesses

## References

1. Falstad Circuit Simulator: http://www.falstad.com/circuit/
2. Roland TB-303 Service Manual
3. "The Art of VA Filter Design" - Vadim Zavalishin

## Falstad Circuit URL

```
http://www.falstad.com/circuit/circuitjs.html?cct=$+1+0.000005+16.817414165184545+50+5+43+R+336+96+400+96+0+4+33+-3+8+0+0.5+r+336+96+336+176+0+10000+g+336+176+336+192+0+r+288+192+240+192+0+100000+r+240+256+288+256+0+10000+c+288+192+288+256+0+1e-8+-1.0098888049016868+w+336+96+288+96+0+w+288+96+288+192+0+t+240+256+192+256+0+-1+-0.4751313109046924+-0.5422257203342431+100+w+240+192+240+256+0+c+144+240+144+304+0+0.000001+7.8184554689121715+g+144+304+144+320+0+w+192+240+144+240+0+w+192+272+192+288+0+w+192+288+224+288+0+w+224+288+416+288+0+r+224+288+224+352+0+10000+r+192+240+192+192+0+22000+R+64+192+32+192+0+0+40+5.33+0+0+0.5+w+224+352+64+352+0+w+64+192+64+352+0+R+192+192+192+160+0+0+40+12+0+0+0.5+o+15+64+0+551+9.353610478917778+9.765625e-55+0+-1+o+0+64+0+34+20+0.00078125+1+-1
```

## License

MIT License
