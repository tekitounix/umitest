/**
 * Renode Audio Worklet Processor
 *
 * Receives audio samples from the main thread (via WebSocket from Renode)
 * and outputs them to the audio context.
 *
 * Uses simple FIFO queue - samples are played in order received.
 * Renode timestamps are used only for ordering, not scheduling.
 */

class RenodeWorkletProcessor extends AudioWorkletProcessor {
    constructor() {
        super();

        // Ring buffer for audio samples (interleaved stereo)
        this.bufferSize = 262144;  // ~2.7s at 48kHz stereo
        this.buffer = new Float32Array(this.bufferSize);
        this.writePos = 0;
        this.readPos = 0;
        this.samplesAvailable = 0;

        // State
        this.channels = 2;
        this.underruns = 0;
        this.debugCount = 0;

        // Initial buffering
        this.initialBufferSize = 16384;  // ~170ms initial buffer
        this.buffering = true;

        // Handle messages from main thread
        this.port.onmessage = (e) => {
            if (e.data.type === 'samples') {
                let samples;
                if (e.data.buffer) {
                    samples = new Float32Array(e.data.buffer);
                } else if (e.data.samples) {
                    samples = e.data.samples;
                }
                if (samples) {
                    this.addSamples(samples);
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
            this.port.postMessage({ type: 'overflow', dropped: overflow });
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
     * Process audio - simple FIFO playback
     */
    process(inputs, outputs, parameters) {
        const output = outputs[0];
        const numChannels = output.length;
        const frameCount = output[0].length;  // Usually 128 frames

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

        const samplesNeeded = frameCount * this.channels;

        if (this.samplesAvailable >= samplesNeeded) {
            // Direct copy from ring buffer
            for (let i = 0; i < frameCount; i++) {
                for (let ch = 0; ch < numChannels; ch++) {
                    const idx = (this.readPos + i * this.channels + ch) % this.bufferSize;
                    output[ch][i] = this.buffer[idx];
                }
            }

            this.readPos = (this.readPos + samplesNeeded) % this.bufferSize;
            this.samplesAvailable -= samplesNeeded;

            // Debug
            if (this.debugCount < 5) {
                this.debugCount++;
                this.port.postMessage({
                    type: 'debug',
                    available: this.samplesAvailable
                });
            }
        } else {
            // Underrun - output silence and wait for more data
            for (let ch = 0; ch < numChannels; ch++) {
                output[ch].fill(0);
            }

            this.underruns++;
            if (this.underruns % 50 === 1) {
                this.port.postMessage({ type: 'underrun', count: this.underruns, available: this.samplesAvailable });
            }

            // Re-enter buffering mode
            this.buffering = true;
            this.port.postMessage({ type: 'rebuffering', count: this.underruns });
        }

        return true;  // Keep processor alive
    }
}

registerProcessor('renode-worklet-processor', RenodeWorkletProcessor);
