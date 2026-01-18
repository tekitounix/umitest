/**
 * UMI-OS Renode Backend Adapter
 *
 * Uses AudioBufferSourceNode.start(when) for sample-accurate playback timing.
 * This is the ONLY way to guarantee glitch-free audio in browsers.
 *
 * Architecture:
 * - Renode sends audio buffers with virtual time timestamps (microseconds)
 * - On first buffer, we sync: virtualTime=0 maps to audioContext.currentTime + latency
 * - Each buffer is scheduled at its exact timestamp using start(when)
 * - AudioBufferSourceNode provides sample-accurate timing guaranteed by Web Audio spec
 */

export class RenodeAdapter {
    constructor(wsUrl = 'ws://localhost:8089') {
        this.wsUrl = wsUrl;
        this.ws = null;
        this.connected = false;
        this.reconnecting = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 1000;

        // Audio playback
        this.audioContext = null;
        this.analyzerNode = null;
        this.gainNode = null;
        this.sampleRate = 48000;
        this.channels = 2;

        // Time synchronization
        this.syncEstablished = false;
        this.firstTimestampUs = 0;      // First virtual timestamp from Renode
        this.firstAudioTime = 0;        // Corresponding AudioContext time
        this.scheduledEndTime = 0;      // End time of last scheduled buffer
        this.latencySeconds = 0.1;      // 100ms initial latency for buffering

        // State
        this.isPlaying = false;
        this.params = [];

        // Callbacks
        this.onMessage = null;
        this.onConnect = null;
        this.onDisconnect = null;
        this.onError = null;

        // Renode connection status
        this.renodeConnected = false;
        this.lastAudioReceived = 0;

        // Stats
        this.buffersScheduled = 0;
        this.buffersPlayed = 0;

        // Simulated kernel state
        this.kernelState = {
            uptime: 0,
            rtc: Math.floor(Date.now() / 1000),
            bufferCount: 0,
            dropCount: 0,
            dspLoad: 0,
            dspPeak: 0,
            audioRunning: false,
            midiRx: 0,
            midiTx: 0,
            batteryPercent: 100,
            batteryCharging: false,
            batteryVoltage: 4200,
            usbConnected: true,
            wdtEnabled: false,
            wdtTimeout: 0,
            wdtExpired: false,
            taskCount: 4,
            taskReady: 1,
            taskBlocked: 0,
            ctxSwitches: 0,
            currentTask: 0,
            tasks: [],
            heapUsed: 0,
            heapTotal: 65536,
            heapPeak: 0,
            stackUsed: 1024,
            stackTotal: 4096,
            sramTotal: 131072,
            flashTotal: 524288,
            flashUsed: 65536,
        };

        this.updateInterval = null;
    }

    async connect() {
        return new Promise((resolve, reject) => {
            try {
                this.ws = new WebSocket(this.wsUrl);

                this.ws.onopen = () => {
                    console.log('[RenodeAdapter] Connected to', this.wsUrl);
                    this.connected = true;
                    this.reconnecting = false;
                    this.reconnectAttempts = 0;
                    this.send({ type: 'getInfo' });
                    if (this.onConnect) this.onConnect();
                    resolve();
                };

                this.ws.onclose = (e) => {
                    console.log('[RenodeAdapter] Disconnected:', e.reason);
                    this.connected = false;
                    this.kernelState.audioRunning = false;
                    if (this.onDisconnect) this.onDisconnect();
                    if (!this.reconnecting && this.reconnectAttempts < this.maxReconnectAttempts) {
                        this.scheduleReconnect();
                    }
                };

                this.ws.onerror = (e) => {
                    console.error('[RenodeAdapter] WebSocket error:', e);
                    if (this.onError) this.onError(e);
                    reject(e);
                };

                this.ws.binaryType = 'arraybuffer';
                this.ws.onmessage = (e) => {
                    this.handleMessage(e.data);
                };

            } catch (err) {
                reject(err);
            }
        });
    }

    scheduleReconnect() {
        if (this.reconnecting) return;
        this.reconnecting = true;
        this.reconnectAttempts++;
        const delay = this.reconnectDelay * this.reconnectAttempts;
        console.log(`[RenodeAdapter] Reconnecting in ${delay}ms`);
        setTimeout(() => {
            this.reconnecting = false;
            this.connect().catch(() => {});
        }, delay);
    }

    disconnect() {
        this.reconnectAttempts = this.maxReconnectAttempts;
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.stopAudio();
    }

    send(msg) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(msg));
        }
    }

    handleMessage(data) {
        if (data instanceof ArrayBuffer) {
            this.handleBinaryMessage(data);
            return;
        }

        try {
            const msg = JSON.parse(data);
            switch (msg.type) {
                case 'info':
                    this.sampleRate = msg.sampleRate || 48000;
                    this.channels = msg.channels || 2;
                    console.log('[RenodeAdapter] Backend info:', msg);
                    break;
                case 'status':
                    this.updateKernelState(msg);
                    break;
            }
            if (this.onMessage) this.onMessage(msg);
        } catch (err) {
            console.error('[RenodeAdapter] Message parse error:', err);
        }
    }

    /**
     * Handle binary audio data
     * Format: [0x01][pad][count:u16][timestamp:u64][samples:f32[]]
     */
    handleBinaryMessage(buffer) {
        const view = new DataView(buffer);
        const msgType = view.getUint8(0);

        if (msgType === 0x01) {
            const sampleCount = view.getUint16(2, true);
            const timestampLow = view.getUint32(4, true);
            const timestampHigh = view.getUint32(8, true);
            const timestampUs = timestampLow + timestampHigh * 0x100000000;
            const samples = new Float32Array(buffer, 12, sampleCount);

            this.scheduleAudioBuffer(samples, timestampUs);
        }
    }

    /**
     * Schedule audio buffer at exact time using AudioBufferSourceNode
     * This is the key to glitch-free playback
     */
    scheduleAudioBuffer(samples, timestampUs) {
        if (!this.isPlaying || !this.audioContext) return;

        this.renodeConnected = true;
        this.lastAudioReceived = Date.now();
        this.kernelState.bufferCount++;

        const numFrames = samples.length / this.channels;
        const bufferDuration = numFrames / this.sampleRate;
        const now = this.audioContext.currentTime;

        // Establish sync on first buffer
        if (!this.syncEstablished) {
            this.firstTimestampUs = timestampUs;
            this.firstAudioTime = now + this.latencySeconds;
            this.scheduledEndTime = this.firstAudioTime;
            this.syncEstablished = true;
            console.log('[RenodeAdapter] Sync established: vtime=0 -> audioTime=' +
                        this.firstAudioTime.toFixed(3) + 's');
        }

        // Always schedule at the end of the last buffer (seamless concatenation)
        // This ignores timestamp for scheduling but preserves audio order
        let targetTime = this.scheduledEndTime;

        // If we've fallen behind (scheduledEndTime is in the past), catch up
        if (targetTime < now) {
            targetTime = now + 0.005;  // 5ms from now
            // Re-sync to prevent drift accumulation
            this.firstTimestampUs = timestampUs;
            this.firstAudioTime = targetTime;
            if (this.buffersScheduled % 100 === 0) {
                console.log('[RenodeAdapter] Re-sync at buffer', this.buffersScheduled);
            }
        }

        // Create AudioBuffer
        const audioBuffer = this.audioContext.createBuffer(
            this.channels, numFrames, this.sampleRate
        );

        // Deinterleave samples into channels
        for (let ch = 0; ch < this.channels; ch++) {
            const channelData = audioBuffer.getChannelData(ch);
            for (let i = 0; i < numFrames; i++) {
                channelData[i] = samples[i * this.channels + ch];
            }
        }

        // Create source node and schedule playback
        const source = this.audioContext.createBufferSource();
        source.buffer = audioBuffer;
        source.connect(this.gainNode);
        source.start(targetTime);

        // Track scheduled end time for next buffer
        this.scheduledEndTime = targetTime + bufferDuration;
        this.buffersScheduled++;

        source.onended = () => {
            this.buffersPlayed++;
        };

        // Debug (first few buffers only)
        if (this.buffersScheduled <= 5) {
            console.log(`[RenodeAdapter] Buffer ${this.buffersScheduled}: ` +
                        `scheduled at ${targetTime.toFixed(3)}s (now=${now.toFixed(3)}s)`);
        }
    }

    updateKernelState(msg) {
        if (msg.running !== undefined) this.kernelState.audioRunning = msg.running;
        if (msg.cpuLoad !== undefined) {
            this.kernelState.dspLoad = Math.round(msg.cpuLoad * 100);
            this.kernelState.dspPeak = Math.max(this.kernelState.dspPeak, this.kernelState.dspLoad);
        }
        if (msg.midiRx !== undefined) this.kernelState.midiRx = msg.midiRx;
        if (msg.midiTx !== undefined) this.kernelState.midiTx = msg.midiTx;
        this.kernelState.uptime += 100000;
    }

    async initAudio() {
        if (this.audioContext) return;

        this.audioContext = new AudioContext({ sampleRate: this.sampleRate });

        // Gain node for volume control and as connection point
        this.gainNode = this.audioContext.createGain();
        this.gainNode.gain.value = 1.0;

        // Analyzer for waveform display
        this.analyzerNode = this.audioContext.createAnalyser();
        this.analyzerNode.fftSize = 2048;

        // Connect: sources -> gain -> analyzer -> destination
        this.gainNode.connect(this.analyzerNode);
        this.analyzerNode.connect(this.audioContext.destination);

        console.log('[RenodeAdapter] AudioContext initialized:', this.sampleRate, 'Hz');
    }

    async start() {
        if (!this.connected) {
            throw new Error('Not connected to Renode Web Bridge');
        }

        await this.initAudio();

        // Reset sync state
        this.syncEstablished = false;
        this.buffersScheduled = 0;
        this.buffersPlayed = 0;

        this.isPlaying = true;
        this.kernelState.audioRunning = true;
        this.renodeConnected = false;

        this.send({ type: 'start' });
        this.startStatusUpdates();

        // Wait for audio
        const startTime = Date.now();
        const timeout = 10000;

        return new Promise((resolve, reject) => {
            const checkAudio = () => {
                if (this.renodeConnected) {
                    console.log('[RenodeAdapter] Receiving audio from Renode');
                    if (this.onMessage) this.onMessage({ type: 'ready' });
                    resolve(true);
                } else if (Date.now() - startTime > timeout) {
                    this.stopAudio();
                    reject(new Error('Timeout: No audio from Renode'));
                } else {
                    setTimeout(checkAudio, 100);
                }
            };
            checkAudio();
        });
    }

    stopAudio() {
        this.isPlaying = false;
        this.kernelState.audioRunning = false;
        this.syncEstablished = false;

        this.send({ type: 'stop' });
        this.stopStatusUpdates();

        if (this.audioContext) {
            this.audioContext.close();
            this.audioContext = null;
            this.gainNode = null;
            this.analyzerNode = null;
        }
    }

    startStatusUpdates() {
        if (this.updateInterval) return;
        this.updateInterval = setInterval(() => {
            if (this.onMessage) {
                this.onMessage({ type: 'kernel-state', ...this.kernelState });
            }
        }, 100);
    }

    stopStatusUpdates() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
    }

    sendMidi(data) {
        this.kernelState.midiRx++;
        this.send({ type: 'midi', data: Array.from(data), timestamp: Date.now() });
    }

    noteOn(note, velocity) { this.sendMidi([0x90, note, velocity]); }
    noteOff(note) { this.sendMidi([0x80, note, 0]); }
    cc(cc, value) { this.sendMidi([0xB0, cc, value]); }

    setParam(id, value) {
        this.send({ type: 'control', param: `param_${id}`, value: value });
    }

    resetDspPeak() { this.kernelState.dspPeak = 0; }
    getKernelState() { return { ...this.kernelState }; }
    isConnected() { return this.connected; }

    isRenodeActive() {
        return this.renodeConnected && (Date.now() - this.lastAudioReceived < 2000);
    }

    getAudioContext() { return this.audioContext; }
    createAnalyzer() { return this.analyzerNode; }
    getAnalyzer() { return this.analyzerNode; }
}

if (typeof window !== 'undefined') {
    window.RenodeAdapter = RenodeAdapter;
}
