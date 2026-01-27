/**
 * AudioWorklet Processor for biquad lowpass filter
 */

class FilterProcessor extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return [
      { name: 'cutoff', defaultValue: 1200, minValue: 20, maxValue: 20000, automationRate: 'a-rate' },
      { name: 'resonance', defaultValue: 0.7, minValue: 0.1, maxValue: 20, automationRate: 'a-rate' },
      { name: 'preGain', defaultValue: 1.0, minValue: 0, maxValue: 4, automationRate: 'a-rate' },
      { name: 'outGain', defaultValue: 1.0, minValue: 0, maxValue: 4, automationRate: 'a-rate' },
    ];
  }

  constructor() {
    super();
    // Simple 2-pole lowpass filter state (per channel)
    this.state = [
      { y1: 0, y2: 0 }, // left
      { y1: 0, y2: 0 }, // right
    ];
    this.bypassed = false;
    this.port.onmessage = (e) => {
      if (e.data.type === 'bypass') {
        this.bypassed = e.data.value;
      }
    };
  }

  process(inputs, outputs, parameters) {
    const input = inputs[0];
    const output = outputs[0];
    if (!input || !input.length) return true;

    const cutoff = parameters.cutoff;
    const resonance = parameters.resonance;
    const preGain = parameters.preGain;
    const outGain = parameters.outGain;

    const sampleRate = globalThis.sampleRate || 44100;

    for (let ch = 0; ch < output.length; ch++) {
      const inp = input[ch] || input[0];
      const out = output[ch];
      const st = this.state[ch] || this.state[0];

      for (let i = 0; i < out.length; i++) {
        const fc = cutoff.length > 1 ? cutoff[i] : cutoff[0];
        const Q = resonance.length > 1 ? resonance[i] : resonance[0];
        const pre = preGain.length > 1 ? preGain[i] : preGain[0];
        const og = outGain.length > 1 ? outGain[i] : outGain[0];

        const x = inp[i] * pre;

        if (this.bypassed) {
          out[i] = x * og;
          continue;
        }

        // Compute biquad coefficients for lowpass
        const omega = 2 * Math.PI * Math.min(fc, sampleRate * 0.45) / sampleRate;
        const sinOmega = Math.sin(omega);
        const cosOmega = Math.cos(omega);
        const alpha = sinOmega / (2 * Q);

        const b0 = (1 - cosOmega) / 2;
        const b1 = 1 - cosOmega;
        const b2 = (1 - cosOmega) / 2;
        const a0 = 1 + alpha;
        const a1 = -2 * cosOmega;
        const a2 = 1 - alpha;

        // Normalize
        const nb0 = b0 / a0;
        const nb1 = b1 / a0;
        const nb2 = b2 / a0;
        const na1 = a1 / a0;
        const na2 = a2 / a0;

        // Direct Form II Transposed
        const y = nb0 * x + st.y1;
        st.y1 = nb1 * x - na1 * y + st.y2;
        st.y2 = nb2 * x - na2 * y;

        out[i] = y * og;
      }
    }
    return true;
  }
}

registerProcessor('filter-processor', FilterProcessor);
