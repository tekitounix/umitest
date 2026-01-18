/**
 * Renode Audio Worklet Processor
 *
 * Receives audio samples from the main thread (via WebSocket from Renode)
 * and outputs them to the audio context.
 */

class RenodeWorkletProcessor extends AudioWorkletProcessor {
    constructor() {
        super();

        // Ring buffer for audio samples (interleaved stereo)
        // Renode simulation is much slower than real-time, need large buffer
        this.bufferSize = 131072;  // ~1.36s at 48kHz stereo
        this.buffer = new Float32Array(this.bufferSize);
        this.writePos = 0;
        this.readPos = 0;
        this.samplesAvailable = 0;

        // State
        this.channels = 2;
        this.underruns = 0;
        this.debugCount = 0;

        // Initial buffering: wait for enough data before starting playback
        // Renode simulation sends audio in bursts, not continuously
        this.initialBufferSize = 32768;  // ~340ms of stereo audio at 48kHz
        this.rebufferThreshold = 0;      // Disable rebuffering - just output silence on underrun
        this.buffering = true;

        // Handle messages from main thread
        this.port.onmessage = (e) => {
            if (e.data.type === 'samples') {
                // Support both old format (samples array) and new format (buffer)
                if (e.data.buffer) {
                    const samples = new Float32Array(e.data.buffer);
                    this.addSamples(samples);
                } else if (e.data.samples) {
                    this.addSamples(e.data.samples);
                }
            } else if (e.data.type === 'clear') {
                this.clear();
            }
        };

        // Signal ready
        this.port.postMessage({ type: 'ready' });
    }

    /**
     * Add samples to the ring buffer
     */
    addSamples(samples) {
        const len = samples.length;

        for (let i = 0; i < len; i++) {
            this.buffer[this.writePos] = samples[i];
            this.writePos = (this.writePos + 1) % this.bufferSize;
        }

        this.samplesAvailable += len;
        if (this.samplesAvailable > this.bufferSize) {
            // Overflow - drop oldest samples
            const overflow = this.samplesAvailable - this.bufferSize;
            this.readPos = (this.readPos + overflow) % this.bufferSize;
            this.samplesAvailable = this.bufferSize;
        }
    }

    /**
     * Clear the buffer
     */
    clear() {
        this.writePos = 0;
        this.readPos = 0;
        this.samplesAvailable = 0;
        this.buffering = true;
        this.underruns = 0;
    }

    /**
     * Process audio
     */
    process(inputs, outputs, parameters) {
        const output = outputs[0];
        const numChannels = output.length;
        const frameCount = output[0].length;  // Usually 128 frames
        const samplesNeeded = frameCount * this.channels;

        // Initial buffering: wait until we have enough data
        if (this.buffering) {
            if (this.samplesAvailable >= this.initialBufferSize) {
                this.buffering = false;
                this.port.postMessage({ type: 'buffering-complete', available: this.samplesAvailable });
            } else {
                // Output silence while buffering
                for (let ch = 0; ch < numChannels; ch++) {
                    output[ch].fill(0);
                }
                return true;
            }
        }

        if (this.samplesAvailable >= samplesNeeded) {
            // De-interleave from ring buffer to output channels
            for (let i = 0; i < frameCount; i++) {
                for (let ch = 0; ch < numChannels; ch++) {
                    const idx = (this.readPos + i * this.channels + ch) % this.bufferSize;
                    output[ch][i] = this.buffer[idx];
                }
            }

            this.readPos = (this.readPos + samplesNeeded) % this.bufferSize;
            this.samplesAvailable -= samplesNeeded;

            // Debug first few
            if (this.debugCount < 5) {
                this.debugCount++;
                this.port.postMessage({
                    type: 'debug',
                    available: this.samplesAvailable,
                    frameCount: frameCount
                });
            }
        } else {
            // Underrun - output what we have (partial) or silence
            if (this.samplesAvailable > 0) {
                // Output partial data - play what we have
                const availableFrames = Math.floor(this.samplesAvailable / this.channels);
                for (let i = 0; i < availableFrames; i++) {
                    for (let ch = 0; ch < numChannels; ch++) {
                        const idx = (this.readPos + i * this.channels + ch) % this.bufferSize;
                        output[ch][i] = this.buffer[idx];
                    }
                }
                // Fill rest with silence
                for (let i = availableFrames; i < frameCount; i++) {
                    for (let ch = 0; ch < numChannels; ch++) {
                        output[ch][i] = 0;
                    }
                }
                this.readPos = (this.readPos + availableFrames * this.channels) % this.bufferSize;
                this.samplesAvailable -= availableFrames * this.channels;
            } else {
                // No data at all - output silence
                for (let ch = 0; ch < numChannels; ch++) {
                    output[ch].fill(0);
                }
            }

            this.underruns++;
            if (this.underruns % 100 === 1) {
                this.port.postMessage({ type: 'underrun', count: this.underruns, available: this.samplesAvailable });
            }

            // Re-enter buffering mode if buffer is nearly empty
            // This helps recover from extended underruns
            if (this.samplesAvailable < this.rebufferThreshold && !this.buffering) {
                this.buffering = true;
                this.port.postMessage({ type: 'rebuffering', count: this.underruns });
            }
        }

        return true;  // Keep processor alive
    }
}

registerProcessor('renode-worklet-processor', RenodeWorkletProcessor);
