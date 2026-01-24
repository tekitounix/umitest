/**
 * UMI Web App - Entry Point
 *
 * Simplified application with unified TargetSelector
 */

import {
  BackendManager,
  BackendType,
  Keyboard,
  DEFAULT_KEY_MAP,
  MidiMonitor,
  ParamControl,
  TargetSelector,
  darkTheme,
  lightTheme,
  applyTheme,
} from './lib/umi_web/index.js';

// ========== State ==========
const state = {
  backend: null,
  backendManager: new BackendManager(),
  targetSelector: null,
  isPlaying: false,
  theme: 'dark',
  midiRxCount: 0,
  audioContext: null,
  params: [],
  keyboard: null,
  midiAccess: null,
  midiInputs: new Map(),
  midiOutputs: new Map(),
};

// ========== DOM References ==========
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const els = {
  targetSelect: $('#target-select'),
  startBtn: $('#start-btn'),
  stopBtn: $('#stop-btn'),
  statusBadge: $('#status-badge'),
  audioIndicator: $('#audio-indicator'),
  midiIndicator: $('#midi-indicator'),
  themeToggle: $('#theme-toggle'),
  themeIcon: $('#theme-icon'),
  waveformCanvas: $('#waveform-canvas'),
  keyboard: $('#keyboard'),
  paramGrid: $('#param-grid'),
  dspLoad: $('#dsp-load'),
  dspLoadBar: $('#dsp-load-bar'),
  sampleRate: $('#sample-rate'),
  bufferSize: $('#buffer-size'),
  dropCount: $('#drop-count'),
  uptime: $('#uptime'),
  kernelState: $('#kernel-state'),
  midiMonitor: $('#midi-monitor'),
  midiRxCount: $('#midi-rx-count'),
  shellInput: $('#shell-input'),
  shellOutput: $('#shell-output'),
  footerDsp: $('#footer-dsp'),
  footerBuf: $('#footer-buf'),
  footerPos: $('#footer-pos'),
  audioOutputSelect: $('#audio-output-select'),
  audioInputSelect: $('#audio-input-select'),
  midiInputList: $('#midi-input-list'),
  midiOutputList: $('#midi-output-list'),
};

// ========== Initialization ==========

async function init() {
  console.log('[App] Initializing UMI Web App');

  // Load theme
  loadTheme();

  // Initialize TargetSelector component and wait for it to be ready
  state.targetSelector = new TargetSelector(els.targetSelect, {
    backendManager: state.backendManager,
    onChange: onTargetChange,
  });

  // Wait for TargetSelector to finish loading
  await state.targetSelector.ready;

  // Setup event listeners
  setupEventListeners();

  // Initialize components
  initKeyboard();

  // Initialize audio and MIDI devices (non-blocking)
  initAudioDevices();
  initWebMIDI();

  // Enable start button
  els.startBtn.disabled = false;

  // Update status
  setStatus('ready', 'Ready');

  console.log('[App] Initialization complete');
}

// ========== Target Management ==========

/**
 * Handle target change from TargetSelector
 * @param {object} target - Selected target
 */
async function onTargetChange(target) {
  console.log('[App] Selected target:', target.name);

  // If playing, switch backend
  if (state.isPlaying) {
    await stopAudio();
    await startAudio();
  }
}

/**
 * Get currently selected target
 */
function getCurrentTarget() {
  return state.targetSelector?.getSelected();
}

// ========== Audio Control ==========

async function startAudio() {
  const target = getCurrentTarget();
  if (!target) {
    console.warn('[App] No target selected');
    return;
  }

  try {
    setStatus('loading', 'Starting...');

    // Create backend based on target
    let backend;
    if (target.app) {
      backend = await state.backendManager.switchToApp(target.app.id);
    } else {
      backend = await state.backendManager.switchBackend(
        target.backend,
        target.deviceName ? { deviceName: target.deviceName } : {}
      );
    }

    state.backend = backend;

    // Setup callbacks - backend uses onMessage for all events
    backend.onMessage = (msg) => {
      switch (msg.type) {
        case 'ready':
          console.log('[App] Backend ready');
          break;
        case 'params':
          console.log('[App] Received params:', msg.params?.length);
          state.params = msg.params;
          loadParameters();
          break;
        case 'kernel-state':
          handleKernelState(msg);
          break;
        case 'shell-output':
          handleShellOutput(msg.text, 'stdout');
          break;
        case 'midi':
          handleMidiMessage(msg.data);
          break;
        case 'error':
          console.error('[App] Backend error:', msg.message);
          setStatus('error', 'Error');
          break;
      }
    };

    backend.onError = (err) => {
      console.error('[App] Backend error:', err);
      setStatus('error', 'Error');
    };

    // Start audio
    await backend.start();

    state.isPlaying = true;
    els.startBtn.hidden = true;
    els.stopBtn.hidden = false;
    els.audioIndicator.classList.add('active');
    els.shellInput.disabled = false;

    // Update sample rate display
    const audioContext = backend.getAudioContext?.();
    if (audioContext) {
      els.sampleRate.textContent = `${audioContext.sampleRate} Hz`;
    }

    setStatus('playing', 'Playing');

    // Load parameters (may be updated later via onMessage)
    loadParameters();

    // Start update loop
    startUpdateLoop();
  } catch (err) {
    console.error('[App] Start failed:', err);
    setStatus('error', 'Start Failed');
  }
}

async function stopAudio() {
  if (state.backend) {
    state.backend.stop();
    state.backend = null;
  }

  state.isPlaying = false;
  els.startBtn.hidden = false;
  els.stopBtn.hidden = true;
  els.audioIndicator.classList.remove('active');
  els.shellInput.disabled = true;

  setStatus('ready', 'Ready');
  stopUpdateLoop();
}

// ========== Components ==========

function initKeyboard() {
  if (!els.keyboard) return;

  const keyboard = new Keyboard(els.keyboard, {
    keyMap: DEFAULT_KEY_MAP,
  });

  keyboard.onNoteOn = (note, velocity) => {
    if (state.backend) {
      state.backend.noteOn(note, velocity);
    }
  };

  keyboard.onNoteOff = (note) => {
    if (state.backend) {
      state.backend.noteOff(note);
    }
  };

  // Enable keyboard input
  keyboard.enable();

  // Store reference
  state.keyboard = keyboard;
}

function loadParameters() {
  if (!els.paramGrid) return;

  // Use params from state (received via onMessage) or backend
  const params = state.params || state.backend?.params || [];

  if (params.length === 0) {
    els.paramGrid.innerHTML = '<p style="color: var(--color-text-secondary);">No parameters available</p>';
    return;
  }

  els.paramGrid.innerHTML = '';

  for (const param of params) {
    const div = document.createElement('div');
    div.className = 'param';
    const defaultValue = param.default !== undefined ? param.default : 0.5;
    div.innerHTML = `
      <div class="param-header">
        <span class="param-name">${param.name}</span>
        <span class="param-value">${formatParamValue({ ...param, value: defaultValue })}</span>
      </div>
      <input type="range" min="0" max="1" step="0.001" value="${defaultValue}">
    `;

    const slider = div.querySelector('input');
    const valueEl = div.querySelector('.param-value');

    slider.addEventListener('input', () => {
      const value = parseFloat(slider.value);
      state.backend?.setParam?.(param.id, value);
      valueEl.textContent = formatParamValue({ ...param, value });
    });

    els.paramGrid.appendChild(div);
  }
}

function formatParamValue(param) {
  if (param.unit === '%') {
    return `${Math.round(param.value * 100)}%`;
  }
  return param.value.toFixed(2);
}

// ========== Callbacks ==========

function handleMidiMessage(data) {
  state.midiRxCount++;
  els.midiRxCount.textContent = state.midiRxCount;
  els.midiIndicator.classList.add('active');
  setTimeout(() => els.midiIndicator.classList.remove('active'), 100);

  // Add to monitor
  addMidiToMonitor(data);
}

function addMidiToMonitor(data) {
  if (!els.midiMonitor) return;

  const time = new Date().toLocaleTimeString('en-US', { hour12: false }).slice(3, 8);
  const status = data[0] & 0xf0;
  const channel = (data[0] & 0x0f) + 1;

  let msgClass = 'other';
  let text = '';

  switch (status) {
    case 0x90:
      msgClass = data[2] > 0 ? 'note-on' : 'note-off';
      text = `Note ${data[2] > 0 ? 'On' : 'Off'} ch${channel} n${data[1]} v${data[2]}`;
      break;
    case 0x80:
      msgClass = 'note-off';
      text = `Note Off ch${channel} n${data[1]}`;
      break;
    case 0xb0:
      msgClass = 'cc';
      text = `CC ch${channel} cc${data[1]}=${data[2]}`;
      break;
    default:
      text = `[${Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ')}]`;
  }

  const div = document.createElement('div');
  div.className = `midi-msg ${msgClass}`;
  div.innerHTML = `<span class="time">${time}</span> ${text}`;

  els.midiMonitor.appendChild(div);
  els.midiMonitor.scrollTop = els.midiMonitor.scrollHeight;

  // Limit entries
  while (els.midiMonitor.children.length > 100) {
    els.midiMonitor.removeChild(els.midiMonitor.firstChild);
  }
}

function handleShellOutput(text, type) {
  if (!els.shellOutput) return;

  const span = document.createElement('span');
  span.className = type === 'stderr' ? 'error' : 'response';
  span.textContent = text + '\n';
  els.shellOutput.appendChild(span);
  els.shellOutput.scrollTop = els.shellOutput.scrollHeight;
}

function handleKernelState(msg) {
  // Update UI based on kernel state
  if (msg.dspLoad !== undefined) {
    const load = Math.round(msg.dspLoad * 100);
    els.dspLoad.textContent = `${load}%`;
    els.dspLoadBar.style.width = `${load}%`;
    els.dspLoadBar.className = 'load-bar-fill' +
      (load > 80 ? ' critical' : load > 50 ? ' warning' : '');
    els.footerDsp.textContent = `${load}%`;
  }

  if (msg.sampleRate) {
    els.sampleRate.textContent = `${msg.sampleRate} Hz`;
  }

  if (msg.bufferSize) {
    els.bufferSize.textContent = msg.bufferSize;
    els.footerBuf.textContent = msg.bufferSize;
  }

  if (msg.uptime !== undefined) {
    els.uptime.textContent = formatUptime(msg.uptime);
  }

  if (msg.state) {
    els.kernelState.textContent = msg.state;
  }
}

// ========== Update Loop ==========

let updateInterval = null;

function startUpdateLoop() {
  if (updateInterval) return;

  // Request kernel state periodically (UMI-OS backend sends via onMessage)
  updateInterval = setInterval(() => {
    if (!state.backend) return;

    // Request kernel state (will be delivered via onMessage -> handleKernelState)
    state.backend.requestKernelState?.();

    // Update waveform from analyzer node
    const analyzer = state.backend.getAnalyzer?.();
    if (analyzer && els.waveformCanvas) {
      const data = new Float32Array(analyzer.fftSize);
      analyzer.getFloatTimeDomainData(data);
      drawWaveform(data);
    }
  }, 50);
}

function stopUpdateLoop() {
  if (updateInterval) {
    clearInterval(updateInterval);
    updateInterval = null;
  }
}

function formatUptime(ms) {
  const sec = Math.floor(ms / 1000);
  const min = Math.floor(sec / 60);
  const hr = Math.floor(min / 60);
  return `${hr}:${String(min % 60).padStart(2, '0')}:${String(sec % 60).padStart(2, '0')}`;
}

// ========== Waveform ==========

function drawWaveform(data) {
  if (!data || !els.waveformCanvas) return;

  const canvas = els.waveformCanvas;
  const ctx = canvas.getContext('2d');
  const { width, height } = canvas.getBoundingClientRect();

  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }

  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--color-bg').trim() || '#0a0a15';
  ctx.fillRect(0, 0, width, height);

  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--color-accent').trim() || '#4ecca3';
  ctx.lineWidth = 1.5;
  ctx.beginPath();

  const step = data.length / width;
  const mid = height / 2;

  for (let i = 0; i < width; i++) {
    const idx = Math.floor(i * step);
    const y = mid - data[idx] * mid * 0.9;
    if (i === 0) ctx.moveTo(i, y);
    else ctx.lineTo(i, y);
  }

  ctx.stroke();
}

// ========== Audio Devices ==========

async function initAudioDevices() {
  if (!navigator.mediaDevices) {
    console.warn('[App] MediaDevices API not available');
    return;
  }

  try {
    // Request permission to enumerate devices
    await navigator.mediaDevices.getUserMedia({ audio: true })
      .then(stream => stream.getTracks().forEach(t => t.stop()))
      .catch(() => {});

    await updateAudioDevices();

    // Listen for device changes
    navigator.mediaDevices.addEventListener('devicechange', updateAudioDevices);
  } catch (err) {
    console.warn('[App] Audio device enumeration failed:', err);
  }
}

async function updateAudioDevices() {
  const devices = await navigator.mediaDevices.enumerateDevices();
  const outputs = devices.filter(d => d.kind === 'audiooutput');
  const inputs = devices.filter(d => d.kind === 'audioinput');

  // Update output select
  if (els.audioOutputSelect) {
    const currentOutputId = els.audioOutputSelect.value;
    els.audioOutputSelect.innerHTML = '<option value="">Default</option>';

    for (const device of outputs) {
      const opt = document.createElement('option');
      opt.value = device.deviceId;
      opt.textContent = device.label || `Speaker ${device.deviceId.slice(0, 8)}`;
      if (device.deviceId === currentOutputId) opt.selected = true;
      els.audioOutputSelect.appendChild(opt);
    }

    const savedOutput = localStorage.getItem('umi-audio-output');
    if (savedOutput && !currentOutputId) {
      els.audioOutputSelect.value = savedOutput;
    }
  }

  // Update input select
  if (els.audioInputSelect) {
    const currentInputId = els.audioInputSelect.value;
    els.audioInputSelect.innerHTML = '<option value="">None</option>';

    for (const device of inputs) {
      const opt = document.createElement('option');
      opt.value = device.deviceId;
      opt.textContent = device.label || `Mic ${device.deviceId.slice(0, 8)}`;
      if (device.deviceId === currentInputId) opt.selected = true;
      els.audioInputSelect.appendChild(opt);
    }

    const savedInput = localStorage.getItem('umi-audio-input');
    if (savedInput && !currentInputId) {
      els.audioInputSelect.value = savedInput;
    }
  }
}

async function applyAudioOutput(deviceId) {
  localStorage.setItem('umi-audio-output', deviceId || '');

  const audioContext = state.backend?.getAudioContext?.();
  if (audioContext?.setSinkId) {
    try {
      await audioContext.setSinkId(deviceId || '');
      console.log('[App] Audio output changed');
    } catch (err) {
      console.warn('[App] Failed to change audio output:', err);
    }
  }
}

// ========== MIDI Devices ==========

async function initWebMIDI() {
  if (!navigator.requestMIDIAccess) {
    console.warn('[App] WebMIDI not supported');
    return;
  }

  try {
    state.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
    console.log('[App] WebMIDI initialized');

    updateMIDIDevices();

    state.midiAccess.onstatechange = () => updateMIDIDevices();
  } catch (err) {
    console.warn('[App] WebMIDI access denied:', err);
  }
}

function getSavedMidiInputs() {
  try {
    const saved = localStorage.getItem('umi-midi-inputs');
    return saved ? JSON.parse(saved) : [];
  } catch { return []; }
}

function saveMidiInputs(inputIds) {
  try {
    localStorage.setItem('umi-midi-inputs', JSON.stringify(inputIds));
  } catch {}
}

function updateMIDIDevices() {
  if (!state.midiAccess) return;

  // Update MIDI inputs
  if (els.midiInputList) {
    const savedInputs = getSavedMidiInputs();
    els.midiInputList.innerHTML = '';
    let hasInputs = false;

    for (const input of state.midiAccess.inputs.values()) {
      hasInputs = true;
      const item = document.createElement('div');
      item.className = 'midi-device-item';
      item.style.cssText = 'display: flex; align-items: center; gap: 0.5rem; padding: 0.25rem 0;';

      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.id = `midi-in-${input.id}`;
      cb.value = input.id;
      cb.checked = savedInputs.includes(input.id) || savedInputs.length === 0;

      const label = document.createElement('label');
      label.htmlFor = cb.id;
      label.textContent = input.name || input.id;
      label.style.cursor = 'pointer';

      cb.addEventListener('change', () => {
        if (cb.checked) {
          connectMIDIInput(input.id);
        } else {
          disconnectMIDIInput(input.id);
        }
        const selected = Array.from(els.midiInputList.querySelectorAll('input:checked'))
          .map(el => el.value);
        saveMidiInputs(selected);
      });

      item.appendChild(cb);
      item.appendChild(label);
      els.midiInputList.appendChild(item);

      if (cb.checked) {
        connectMIDIInput(input.id);
      }
    }

    if (!hasInputs) {
      els.midiInputList.textContent = 'No MIDI devices';
    }
  }

  // Update MIDI outputs
  if (els.midiOutputList) {
    const savedOutputs = getSavedMidiOutputs();
    els.midiOutputList.innerHTML = '';
    let hasOutputs = false;

    for (const output of state.midiAccess.outputs.values()) {
      hasOutputs = true;
      const item = document.createElement('div');
      item.className = 'midi-device-item';
      item.style.cssText = 'display: flex; align-items: center; gap: 0.5rem; padding: 0.25rem 0;';

      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.id = `midi-out-${output.id}`;
      cb.value = output.id;
      cb.checked = savedOutputs.includes(output.id) || savedOutputs.length === 0;

      const label = document.createElement('label');
      label.htmlFor = cb.id;
      label.textContent = output.name || output.id;
      label.style.cursor = 'pointer';

      cb.addEventListener('change', () => {
        if (cb.checked) {
          connectMIDIOutput(output.id);
        } else {
          disconnectMIDIOutput(output.id);
        }
        const selected = Array.from(els.midiOutputList.querySelectorAll('input:checked'))
          .map(el => el.value);
        saveMidiOutputs(selected);
      });

      item.appendChild(cb);
      item.appendChild(label);
      els.midiOutputList.appendChild(item);

      if (cb.checked) {
        connectMIDIOutput(output.id);
      }
    }

    if (!hasOutputs) {
      els.midiOutputList.textContent = 'No MIDI devices';
    }
  }
}

function connectMIDIInput(inputId) {
  if (!inputId || !state.midiAccess || state.midiInputs.has(inputId)) return;

  const input = state.midiAccess.inputs.get(inputId);
  if (input) {
    input.onmidimessage = (e) => {
      // Forward to backend
      if (state.backend) {
        state.backend.sendMidi?.(Array.from(e.data));
      }
      // Update UI
      handleMidiMessage(Array.from(e.data));
    };
    state.midiInputs.set(inputId, input);
    console.log('[App] Connected MIDI input:', input.name);
  }
}

function disconnectMIDIInput(inputId) {
  const input = state.midiInputs.get(inputId);
  if (input) {
    input.onmidimessage = null;
    state.midiInputs.delete(inputId);
    console.log('[App] Disconnected MIDI input:', input.name);
  }
}

function getSavedMidiOutputs() {
  try {
    const saved = localStorage.getItem('umi-midi-outputs');
    return saved ? JSON.parse(saved) : [];
  } catch { return []; }
}

function saveMidiOutputs(outputIds) {
  try {
    localStorage.setItem('umi-midi-outputs', JSON.stringify(outputIds));
  } catch {}
}

function connectMIDIOutput(outputId) {
  if (!outputId || !state.midiAccess || state.midiOutputs.has(outputId)) return;

  const output = state.midiAccess.outputs.get(outputId);
  if (output) {
    state.midiOutputs.set(outputId, output);
    console.log('[App] Connected MIDI output:', output.name);
  }
}

function disconnectMIDIOutput(outputId) {
  const output = state.midiOutputs.get(outputId);
  if (output) {
    state.midiOutputs.delete(outputId);
    console.log('[App] Disconnected MIDI output:', output.name);
  }
}

// Send MIDI to all connected outputs
function sendMidiToOutputs(data) {
  for (const output of state.midiOutputs.values()) {
    try {
      output.send(data);
    } catch (err) {
      console.warn('[App] MIDI send failed:', err);
    }
  }
}

// ========== Theme ==========

function loadTheme() {
  const saved = localStorage.getItem('umi-theme') || 'dark';
  setTheme(saved);
}

function setTheme(theme) {
  state.theme = theme;
  document.documentElement.setAttribute('data-theme', theme);
  applyTheme(theme === 'dark' ? darkTheme : lightTheme);
  els.themeIcon.textContent = theme === 'dark' ? '🌙' : '☀️';
  localStorage.setItem('umi-theme', theme);
}

function toggleTheme() {
  setTheme(state.theme === 'dark' ? 'light' : 'dark');
}

// ========== Status ==========

function setStatus(type, text) {
  els.statusBadge.className = `badge ${type}`;
  els.statusBadge.textContent = text;
}

// ========== Event Listeners ==========

function setupEventListeners() {
  // Note: targetSelect events handled by TargetSelector component
  els.startBtn.addEventListener('click', startAudio);
  els.stopBtn.addEventListener('click', stopAudio);
  els.themeToggle.addEventListener('click', toggleTheme);

  // Audio output change
  els.audioOutputSelect?.addEventListener('change', (e) => {
    applyAudioOutput(e.target.value);
  });

  // Audio input change
  els.audioInputSelect?.addEventListener('change', (e) => {
    localStorage.setItem('umi-audio-input', e.target.value || '');
    // Audio input handling would be implemented here if needed
  });

  // Shell input
  els.shellInput?.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && state.backend) {
      const cmd = els.shellInput.value.trim();
      if (cmd) {
        els.shellOutput.innerHTML += `<span class="prompt">umi$ </span>${cmd}\n`;
        state.backend.sendShellCommand?.(cmd);
        els.shellInput.value = '';
      }
    }
  });

  // Keyboard shortcuts
  document.addEventListener('keydown', (e) => {
    if (e.key === ' ' && e.target === document.body) {
      e.preventDefault();
      if (state.isPlaying) stopAudio();
      else startAudio();
    }
  });
}

// ========== Start ==========
init().catch(console.error);
