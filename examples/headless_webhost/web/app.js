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
  darkTheme,
  lightTheme,
  applyTheme,
} from './lib/umi_web/index.js';

// ========== State ==========
const state = {
  backend: null,
  backendManager: new BackendManager(),
  targets: [],
  currentTarget: null,
  isPlaying: false,
  theme: 'dark',
  midiRxCount: 0,
  audioContext: null,
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
  midiInputList: $('#midi-input-list'),
};

// ========== Initialization ==========

async function init() {
  console.log('[App] Initializing UMI Web App');

  // Load theme
  loadTheme();

  // Load targets (apps + hardware)
  await loadTargets();

  // Setup event listeners
  setupEventListeners();

  // Initialize components
  initKeyboard();

  // Update status
  setStatus('ready', 'Ready');

  console.log('[App] Initialization complete');
}

// ========== Target Management ==========

async function loadTargets() {
  try {
    // Load apps from manifest
    const apps = await state.backendManager.loadApplications();

    // Build target list
    state.targets = [];

    // Add simulator targets from apps
    for (const app of apps) {
      state.targets.push({
        id: app.id,
        name: app.name,
        icon: getTargetIcon(app.backend),
        type: 'simulator',
        backend: app.backend,
        app: app,
      });
    }

    // Add Renode if available
    const backends = await state.backendManager.getAvailableBackends();
    const renodeBackend = backends.find(b => b.type === BackendType.RENODE);
    if (renodeBackend?.available) {
      state.targets.push({
        id: 'renode',
        name: 'Renode Emulator',
        icon: '🔧',
        type: 'emulator',
        backend: 'renode',
      });
    }

    // Check for hardware devices (without triggering permission)
    const hwBackend = backends.find(b => b.type === 'hardware');
    if (hwBackend?.available) {
      state.targets.push({
        id: 'hardware-scan',
        name: 'Scan USB Devices...',
        icon: '🔌',
        type: 'hardware-scan',
        backend: 'hardware',
      });
    }

    // Populate select
    populateTargetSelect();

    // Auto-select default
    const defaultApp = apps.find(a => a.default) || apps[0];
    if (defaultApp) {
      els.targetSelect.value = defaultApp.id;
      state.currentTarget = state.targets.find(t => t.id === defaultApp.id);
    }
  } catch (err) {
    console.error('[App] Failed to load targets:', err);
    setStatus('error', 'Load Failed');
  }
}

function populateTargetSelect() {
  els.targetSelect.innerHTML = '';

  // Group targets by type
  const simulators = state.targets.filter(t => t.type === 'simulator');
  const emulators = state.targets.filter(t => t.type === 'emulator');
  const hardware = state.targets.filter(t => t.type === 'hardware' || t.type === 'hardware-scan');

  // Simulators
  if (simulators.length > 0) {
    const group = document.createElement('optgroup');
    group.label = 'Simulators';
    for (const t of simulators) {
      const opt = document.createElement('option');
      opt.value = t.id;
      opt.textContent = `${t.icon} ${t.name}`;
      group.appendChild(opt);
    }
    els.targetSelect.appendChild(group);
  }

  // Emulators
  if (emulators.length > 0) {
    const group = document.createElement('optgroup');
    group.label = 'Emulators';
    for (const t of emulators) {
      const opt = document.createElement('option');
      opt.value = t.id;
      opt.textContent = `${t.icon} ${t.name}`;
      group.appendChild(opt);
    }
    els.targetSelect.appendChild(group);
  }

  // Hardware
  if (hardware.length > 0) {
    const group = document.createElement('optgroup');
    group.label = 'Hardware';
    for (const t of hardware) {
      const opt = document.createElement('option');
      opt.value = t.id;
      opt.textContent = `${t.icon} ${t.name}`;
      group.appendChild(opt);
    }
    els.targetSelect.appendChild(group);
  }
}

function getTargetIcon(backend) {
  switch (backend) {
    case 'umim':
    case 'umios':
      return '🖥️';
    case 'renode':
      return '🔧';
    case 'hardware':
      return '🔌';
    default:
      return '📦';
  }
}

async function onTargetChange() {
  const targetId = els.targetSelect.value;
  const target = state.targets.find(t => t.id === targetId);

  if (!target) return;

  // Handle hardware scan
  if (target.type === 'hardware-scan') {
    await scanHardwareDevices();
    return;
  }

  state.currentTarget = target;
  console.log('[App] Selected target:', target.name);

  // If playing, switch backend
  if (state.isPlaying) {
    await stopAudio();
    await startAudio();
  }
}

async function scanHardwareDevices() {
  try {
    setStatus('loading', 'Scanning...');
    const devices = await state.backendManager.getHardwareDevices();

    // Remove old hardware entries (except scan option)
    state.targets = state.targets.filter(t => t.type !== 'hardware');

    // Add detected devices
    for (const device of devices) {
      state.targets.push({
        id: `hw-${device.name.replace(/\s+/g, '-').toLowerCase()}`,
        name: `USB: ${device.name}`,
        icon: '🔌',
        type: 'hardware',
        backend: 'hardware',
        deviceName: device.name,
      });
    }

    populateTargetSelect();
    setStatus('ready', 'Ready');

    if (devices.length > 0) {
      // Select first device
      const firstHw = state.targets.find(t => t.type === 'hardware');
      if (firstHw) {
        els.targetSelect.value = firstHw.id;
        state.currentTarget = firstHw;
      }
    }
  } catch (err) {
    console.error('[App] Hardware scan failed:', err);
    setStatus('error', 'Scan Failed');
  }
}

// ========== Audio Control ==========

async function startAudio() {
  if (!state.currentTarget) {
    console.warn('[App] No target selected');
    return;
  }

  try {
    setStatus('loading', 'Starting...');

    // Create backend based on target
    let backend;
    if (state.currentTarget.app) {
      backend = await state.backendManager.switchToApp(state.currentTarget.app.id);
    } else {
      backend = await state.backendManager.switchBackend(
        state.currentTarget.backend,
        state.currentTarget.deviceName ? { deviceName: state.currentTarget.deviceName } : {}
      );
    }

    state.backend = backend;

    // Setup callbacks
    backend.onMidiMessage = handleMidiMessage;
    backend.onShellOutput = handleShellOutput;
    backend.onStateUpdate = handleStateUpdate;

    // Start audio
    await backend.start();

    state.isPlaying = true;
    els.startBtn.hidden = true;
    els.stopBtn.hidden = false;
    els.audioIndicator.classList.add('active');
    els.shellInput.disabled = false;

    setStatus('playing', 'Playing');

    // Load parameters
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
    startOctave: 4,
    octaveRange: 2,
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
}

function loadParameters() {
  if (!state.backend || !els.paramGrid) return;

  const params = state.backend.getParameters?.() || [];

  if (params.length === 0) {
    els.paramGrid.innerHTML = '<p style="color: var(--color-text-secondary);">No parameters available</p>';
    return;
  }

  els.paramGrid.innerHTML = '';

  for (const param of params) {
    const div = document.createElement('div');
    div.className = 'param';
    div.innerHTML = `
      <div class="param-header">
        <span class="param-name">${param.name}</span>
        <span class="param-value">${formatParamValue(param)}</span>
      </div>
      <input type="range" min="0" max="1" step="0.001" value="${param.value}">
    `;

    const slider = div.querySelector('input');
    const valueEl = div.querySelector('.param-value');

    slider.addEventListener('input', () => {
      const value = parseFloat(slider.value);
      state.backend.setParameter?.(param.index, value);
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
  span.textContent = text;
  els.shellOutput.appendChild(span);
  els.shellOutput.scrollTop = els.shellOutput.scrollHeight;
}

function handleStateUpdate(state) {
  // Update UI based on backend state
}

// ========== Update Loop ==========

let updateInterval = null;

function startUpdateLoop() {
  if (updateInterval) return;

  updateInterval = setInterval(() => {
    if (!state.backend) return;

    const status = state.backend.getStatus?.() || {};

    // DSP Load
    if (status.dspLoad !== undefined) {
      const load = Math.round(status.dspLoad * 100);
      els.dspLoad.textContent = `${load}%`;
      els.dspLoadBar.style.width = `${load}%`;
      els.dspLoadBar.className = 'load-bar-fill' +
        (load > 80 ? ' critical' : load > 50 ? ' warning' : '');
      els.footerDsp.textContent = `${load}%`;
    }

    // Sample rate
    if (status.sampleRate) {
      els.sampleRate.textContent = `${status.sampleRate} Hz`;
    }

    // Buffer
    if (status.bufferSize) {
      els.bufferSize.textContent = status.bufferSize;
      els.footerBuf.textContent = status.bufferSize;
    }

    // Drops
    if (status.dropCount !== undefined) {
      els.dropCount.textContent = status.dropCount;
      els.dropCount.className = 'status-value' + (status.dropCount > 0 ? ' error' : '');
    }

    // Position
    if (status.samplePosition !== undefined) {
      els.footerPos.textContent = status.samplePosition.toLocaleString();
    }

    // Uptime
    if (status.uptime !== undefined) {
      els.uptime.textContent = formatUptime(status.uptime);
    }

    // Kernel state
    if (status.kernelState) {
      els.kernelState.textContent = status.kernelState;
    }

    // Waveform
    if (state.backend.getWaveformData && els.waveformCanvas) {
      drawWaveform(state.backend.getWaveformData());
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
  els.targetSelect.addEventListener('change', onTargetChange);
  els.startBtn.addEventListener('click', startAudio);
  els.stopBtn.addEventListener('click', stopAudio);
  els.themeToggle.addEventListener('click', toggleTheme);

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
