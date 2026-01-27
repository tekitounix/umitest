/**
 * UI Components - All styled UI components
 * This file aggregates all components for easy importing.
 * Components can be split into individual files later for better maintainability.
 */

import { clamp01 } from '../lib/utils.js';
import { createKnobLogic } from '../ui_headless/knob_logic.js';

// ============================================
// Knob View (SVG rendering)
// ============================================
function createKnobView({ size = 64, strokeWidth = 6 }) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('width', String(size));
  svg.setAttribute('height', String(size));
  svg.setAttribute('viewBox', `0 0 ${size} ${size}`);
  svg.style.touchAction = 'none';
  svg.style.outline = 'none';
  svg.setAttribute('tabindex', '0');
  svg.setAttribute('role', 'slider');

  const cx = size / 2;
  const cy = size / 2;
  const r = (size - strokeWidth) / 2 - 4;

  const startAngle = 135;
  const arcSpan = 270;
  const fullCircumference = 2 * Math.PI * r;
  const arcLength = (arcSpan / 360) * fullCircumference;

  const face = document.createElementNS(svg.namespaceURI, 'circle');
  face.setAttribute('cx', String(cx));
  face.setAttribute('cy', String(cy));
  face.setAttribute('r', String(r + 1));
  face.classList.add('knob-face');

  const track = document.createElementNS(svg.namespaceURI, 'circle');
  track.setAttribute('cx', String(cx));
  track.setAttribute('cy', String(cy));
  track.setAttribute('r', String(r));
  track.setAttribute('fill', 'none');
  track.setAttribute('stroke-width', String(strokeWidth));
  track.setAttribute('stroke-linecap', 'round');
  track.classList.add('knob-track');
  track.setAttribute('transform', `rotate(${startAngle} ${cx} ${cy})`);
  track.setAttribute('stroke-dasharray', `${arcLength} ${fullCircumference}`);

  const fill = document.createElementNS(svg.namespaceURI, 'circle');
  fill.setAttribute('cx', String(cx));
  fill.setAttribute('cy', String(cy));
  fill.setAttribute('r', String(r));
  fill.setAttribute('fill', 'none');
  fill.setAttribute('stroke-width', String(strokeWidth));
  fill.setAttribute('stroke-linecap', 'round');
  fill.classList.add('knob-fill');
  fill.setAttribute('transform', `rotate(${startAngle} ${cx} ${cy})`);
  fill.setAttribute('stroke-dasharray', `${arcLength} ${fullCircumference}`);

  const indicator = document.createElementNS(svg.namespaceURI, 'circle');
  indicator.setAttribute('r', '3');
  indicator.classList.add('knob-indicator');

  svg.append(face, track, fill, indicator);

  function render(value01) {
    const v = clamp01(value01);
    const fillLength = arcLength * v;
    const offset = arcLength - fillLength;
    fill.setAttribute('stroke-dashoffset', String(offset));

    const angle = startAngle + arcSpan * v;
    const rad = (angle * Math.PI) / 180;
    const ix = cx + r * Math.cos(rad);
    const iy = cy + r * Math.sin(rad);
    indicator.setAttribute('cx', String(ix));
    indicator.setAttribute('cy', String(iy));

    svg.setAttribute('aria-valuenow', String(Math.round(v * 100)));
  }

  svg.setAttribute('aria-valuemin', '0');
  svg.setAttribute('aria-valuemax', '100');

  return { el: svg, render };
}

// ============================================
// Knob Component
// ============================================
export function Knob({ label, param, onGestureStart }) {
  const s = 60, w = 6;
  const el = document.createElement('div');
  el.className = 'ctrl knob';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;

  const view = createKnobView({ size: s, strokeWidth: w });
  const logic = createKnobLogic({ param });

  const readout = document.createElement('div');
  readout.className = 'readout';

  el.append(labelEl, view.el, readout);

  function render() {
    view.render(param.get01());
    readout.textContent = param.display();
  }

  el.addEventListener('pointerdown', () => {
    if (onGestureStart) onGestureStart(param);
  });

  view.el.addEventListener('pointerdown', (e) => {
    logic.onPointerDown(e);
    view.el.setPointerCapture(e.pointerId);
  });
  view.el.addEventListener('pointermove', (e) => logic.onPointerMove(e));
  view.el.addEventListener('pointerup', () => logic.onPointerUp());
  view.el.addEventListener('pointercancel', () => logic.onPointerCancel());
  view.el.addEventListener('wheel', (e) => { e.preventDefault(); logic.onWheel(e); }, { passive: false });
  view.el.addEventListener('keydown', (e) => logic.onKeyDown(e));

  param.on(() => render());
  render();

  return { el, render, setNormalized: (v) => param.set01(v) };
}

// ============================================
// RotarySelector Component
// ============================================
export function RotarySelector({ label, options, defaultIndex = 0, onChange }) {
  const s = 60, w = 6;
  const el = document.createElement('div');
  el.className = 'ctrl knob';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;

  let currentIndex = defaultIndex;
  const total = options.length;

  const param = {
    get01: () => currentIndex / Math.max(1, total - 1),
    set01: (v) => {
      const newIdx = Math.round(clamp01(v) * (total - 1));
      if (newIdx !== currentIndex) {
        currentIndex = newIdx;
        render();
        if (onChange) onChange(options[currentIndex], currentIndex);
      }
    },
    reset: () => { currentIndex = defaultIndex; render(); if (onChange) onChange(options[currentIndex], currentIndex); },
  };

  const view = createKnobView({ size: s, strokeWidth: w });
  const logic = createKnobLogic({ param });

  const readout = document.createElement('div');
  readout.className = 'readout';

  el.append(labelEl, view.el, readout);

  function render() {
    view.render(param.get01());
    readout.textContent = options[currentIndex] || '';
  }

  view.el.addEventListener('pointerdown', (e) => { logic.onPointerDown(e); view.el.setPointerCapture(e.pointerId); });
  view.el.addEventListener('pointermove', (e) => logic.onPointerMove(e));
  view.el.addEventListener('pointerup', () => logic.onPointerUp());
  view.el.addEventListener('pointercancel', () => logic.onPointerCancel());
  view.el.addEventListener('wheel', (e) => { e.preventDefault(); logic.onWheel(e); }, { passive: false });
  view.el.addEventListener('keydown', (e) => logic.onKeyDown(e));

  render();
  return { el, getValue: () => options[currentIndex], getIndex: () => currentIndex };
}

// ============================================
// Slider Component
// ============================================
export function Slider({ label, param, onGestureStart, orientation = 'h', size = '8x1' }) {
  const el = document.createElement('div');
  const prefix = orientation === 'h' ? 'hslider' : 'vslider';
  let sizeClass = size;
  if (orientation === 'h') {
    if (size === 'xs') sizeClass = '4x1';
    else if (size === 'sm') sizeClass = '8x1';
    else if (size === 'md') sizeClass = '12x1';
    else if (size === 'lg') sizeClass = '16x1';
  } else {
    if (size === 'sm' || size === 'thin-short' || size === 'wide-short') sizeClass = 'short';
    else if (size === 'md' || size === 'thin-tall' || size === 'wide-tall') sizeClass = 'tall';
  }
  el.className = `ctrl ${prefix}--${sizeClass}`;

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;

  const wrap = document.createElement('div');
  wrap.className = 'slider-wrap';

  const input = document.createElement('input');
  input.type = 'range';
  input.min = '0';
  input.max = '1';
  input.step = '0.001';
  input.value = String(param.get01());
  input.className = `slider-${orientation}`;

  wrap.append(input);

  const readout = document.createElement('div');
  readout.className = 'readout';

  el.append(labelEl, wrap, readout);

  function render() {
    input.value = String(param.get01());
    readout.textContent = param.display();
  }

  el.addEventListener('pointerdown', () => {
    if (onGestureStart) onGestureStart(param);
  });

  el.addEventListener('dblclick', () => param.reset());

  input.addEventListener('input', () => param.set01(Number(input.value)));
  param.on(() => render());
  render();

  return { el, render, setNormalized: (v) => param.set01(v) };
}

// ============================================
// TextInput Component
// ============================================
export function TextInput({ label, placeholder = '', value = '', onChange, multiline = false, gridSize }) {
  const defaultSize = multiline ? '8x4' : '8x2';
  const size = gridSize || defaultSize;

  const el = document.createElement('div');
  el.className = 'ctrl text-input-ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;

  const input = multiline ? document.createElement('textarea') : document.createElement('input');
  input.className = 'text-input';
  input.placeholder = placeholder;
  input.value = value;
  if (!multiline) input.type = 'text';

  el.append(labelEl, input);

  input.addEventListener('input', () => { if (onChange) onChange(input.value); });
  input.addEventListener('change', () => { if (onChange) onChange(input.value); });

  return { el, getValue: () => input.value, setValue: (v) => { input.value = v; }, focus: () => input.focus() };
}

// ============================================
// Button Component
// ============================================
export function Button({ label, onClick, variant = 'default', size = '4x2' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.alignItems = 'center';
  el.style.justifyContent = 'center';

  const btn = document.createElement('button');
  btn.className = `btn ${variant === 'primary' ? 'primary' : ''} ${variant === 'warn' ? 'warn' : ''}`;
  btn.textContent = label;
  btn.style.width = '100%';
  btn.style.height = '100%';
  btn.onclick = onClick;

  el.append(btn);

  return {
    el,
    setLabel: (text) => { btn.textContent = text; },
    setEnabled: (enabled) => { btn.disabled = !enabled; },
    setActive: (active) => { btn.classList.toggle('active', active); },
  };
}

// ============================================
// Label Component
// ============================================
export function Label({ text, size = '8x1', align = 'left' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.alignItems = 'center';
  el.style.justifyContent = align === 'center' ? 'center' : align === 'right' ? 'flex-end' : 'flex-start';
  el.style.padding = '0 4px';
  el.style.overflow = 'visible';

  const span = document.createElement('span');
  span.style.fontSize = 'var(--font-size-sm)';
  span.style.color = 'var(--color-fg)';
  span.style.whiteSpace = 'nowrap';
  span.textContent = text;

  el.append(span);

  return { el, setText: (t) => { span.textContent = t; } };
}

// ============================================
// Value Component
// ============================================
export function Value({ label, value = '', unit = '', size = '4x2' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.alignItems = 'center';
  el.style.justifyContent = 'center';
  el.style.overflow = 'visible';
  el.style.padding = '2px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.whiteSpace = 'nowrap';

  const valueEl = document.createElement('div');
  valueEl.style.fontSize = 'var(--font-size-md)';
  valueEl.style.fontWeight = 'bold';
  valueEl.style.color = 'var(--color-fg)';
  valueEl.style.whiteSpace = 'nowrap';
  valueEl.textContent = value + (unit ? ` ${unit}` : '');

  el.append(labelEl, valueEl);

  return { el, setValue: (v, u) => { valueEl.textContent = v + (u || unit ? ` ${u || unit}` : ''); } };
}

// ============================================
// Toggle Component
// ============================================
export function Toggle({ label = '', size = '2x2', defaultOn = false, onChange }) {
  const el = document.createElement('div');
  el.className = `toggle toggle--${size}${defaultOn ? ' on' : ''}`;
  el.tabIndex = 0;

  const sw = document.createElement('div');
  sw.className = 'switch';
  el.append(sw);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  let on = defaultOn;

  function toggle() {
    on = !on;
    el.classList.toggle('on', on);
    if (onChange) onChange(on);
  }

  el.addEventListener('click', toggle);
  el.addEventListener('keydown', (e) => {
    if (e.key === ' ' || e.key === 'Enter') {
      e.preventDefault();
      toggle();
    }
  });

  return { el, getValue: () => on, setValue: (v) => { on = v; el.classList.toggle('on', on); } };
}

// ============================================
// Dropdown Component
// ============================================
export function Dropdown({ label = '', options, defaultIndex = 0, size = '4x2', onChange }) {
  const el = document.createElement('div');
  el.className = `dropdown dropdown--${size}`;
  el.style.position = 'relative';

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  const value = document.createElement('div');
  value.className = 'value';
  value.textContent = options[defaultIndex] || '';
  el.append(value);

  const arrow = document.createElement('div');
  arrow.className = 'arrow';
  el.append(arrow);

  let currentIndex = defaultIndex;
  let menu = null;

  function openMenu() {
    if (menu) return;
    menu = document.createElement('div');
    menu.className = 'dropdown-menu';
    const rect = el.getBoundingClientRect();
    menu.style.left = `${rect.left}px`;
    menu.style.top = `${rect.bottom + 2}px`;
    menu.style.minWidth = `${rect.width}px`;

    options.forEach((opt, i) => {
      const item = document.createElement('div');
      item.className = `option${i === currentIndex ? ' selected' : ''}`;
      item.textContent = opt;
      item.addEventListener('click', (e) => {
        e.stopPropagation();
        currentIndex = i;
        value.textContent = opt;
        closeMenu();
        if (onChange) onChange(opt, i);
      });
      menu.append(item);
    });

    document.body.append(menu);
    setTimeout(() => { document.addEventListener('click', closeMenu, { once: true }); }, 0);
  }

  function closeMenu() {
    if (menu) { menu.remove(); menu = null; }
  }

  el.addEventListener('click', () => { if (menu) closeMenu(); else openMenu(); });

  return { el, getValue: () => options[currentIndex], getIndex: () => currentIndex, setIndex: (i) => { currentIndex = i; value.textContent = options[i] || ''; } };
}

// ============================================
// NumberInput Component
// ============================================
export function NumberInput({ label, value = 0, min = 0, max = 999, step = 1, onChange, size = '4x2' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.alignItems = 'center';
  el.style.justifyContent = 'center';
  el.style.gap = '2px';
  el.style.overflow = 'visible';
  el.style.padding = '2px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.whiteSpace = 'nowrap';

  const inputWrap = document.createElement('div');
  inputWrap.style.display = 'flex';
  inputWrap.style.alignItems = 'center';
  inputWrap.style.gap = '2px';

  const decBtn = document.createElement('button');
  decBtn.className = 'btn';
  decBtn.textContent = '−';
  decBtn.style.padding = '2px 6px';
  decBtn.style.fontSize = 'var(--font-size-sm)';

  const input = document.createElement('input');
  input.type = 'number';
  input.min = min;
  input.max = max;
  input.step = step;
  input.value = value;
  input.style.width = '48px';
  input.style.textAlign = 'center';
  input.style.fontSize = 'var(--font-size-sm)';
  input.style.border = '1px solid var(--color-line)';
  input.style.borderRadius = 'var(--ui-radius)';
  input.style.background = 'var(--color-panel)';
  input.style.color = 'var(--color-fg)';
  input.style.padding = '2px';

  const incBtn = document.createElement('button');
  incBtn.className = 'btn';
  incBtn.textContent = '+';
  incBtn.style.padding = '2px 6px';
  incBtn.style.fontSize = 'var(--font-size-sm)';

  inputWrap.append(decBtn, input, incBtn);
  el.append(labelEl, inputWrap);

  function update(v) {
    const clamped = Math.max(min, Math.min(max, v));
    input.value = clamped;
    if (onChange) onChange(clamped);
  }

  decBtn.onclick = () => update(Number(input.value) - step);
  incBtn.onclick = () => update(Number(input.value) + step);
  input.onchange = () => update(Number(input.value));

  return { el, getValue: () => Number(input.value), setValue: (v) => { input.value = Math.max(min, Math.min(max, v)); } };
}

// ============================================
// ProgressBar Component
// ============================================
export function ProgressBar({ label, size = '8x1' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = rows > 1 ? 'column' : 'row';
  el.style.alignItems = 'center';
  el.style.gap = '4px';
  el.style.padding = '4px 8px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.flex = '0 0 auto';

  const track = document.createElement('div');
  track.style.flex = '1';
  track.style.height = '6px';
  track.style.background = 'var(--color-panel)';
  track.style.borderRadius = '3px';
  track.style.overflow = 'hidden';
  track.style.minWidth = '40px';

  const fill = document.createElement('div');
  fill.style.height = '100%';
  fill.style.width = '0%';
  fill.style.background = 'var(--color-accent)';
  fill.style.transition = 'width 0.1s ease';

  track.append(fill);
  el.append(labelEl, track);

  const setProgress = (p) => { fill.style.width = `${Math.max(0, Math.min(100, p * 100))}%`; };
  return { el, set: setProgress, setProgress, setColor: (color) => { fill.style.background = color; } };
}

// ============================================
// Keyboard Component
// ============================================
export function Keyboard({ size = '12x2', startNote = 60, onNoteOn, onNoteOff }) {
  const el = document.createElement('div');
  el.className = `keyboard keyboard--${size}`;

  const octaves = 1;
  const whitePattern = [0, 2, 4, 5, 7, 9, 11];
  const blackPattern = [1, 3, null, 6, 8, 10, null];

  const whiteKeys = document.createElement('div');
  whiteKeys.className = 'white-keys';

  const blackKeys = document.createElement('div');
  blackKeys.className = 'black-keys';

  const keyEls = new Map();

  for (let oct = 0; oct < octaves; oct++) {
    whitePattern.forEach((semitone, i) => {
      const note = startNote + oct * 12 + semitone;
      const key = document.createElement('div');
      key.className = 'white-key';
      key.dataset.note = note;
      keyEls.set(note, key);
      whiteKeys.append(key);

      const slot = document.createElement('div');
      slot.className = 'black-key-slot';
      const blackSemitone = blackPattern[i];
      if (blackSemitone !== null) {
        const blackNote = startNote + oct * 12 + blackSemitone;
        const blackKey = document.createElement('div');
        blackKey.className = 'black-key';
        blackKey.dataset.note = blackNote;
        keyEls.set(blackNote, blackKey);
        slot.append(blackKey);
      }
      blackKeys.append(slot);
    });
  }

  el.append(whiteKeys, blackKeys);

  let activeNote = null;

  function noteOn(note) {
    if (activeNote === note) return;
    if (activeNote !== null) noteOff(activeNote);
    activeNote = note;
    const keyEl = keyEls.get(note);
    if (keyEl) keyEl.classList.add('active');
    if (onNoteOn) onNoteOn(note);
  }

  function noteOff(note) {
    if (note === null) return;
    const keyEl = keyEls.get(note);
    if (keyEl) keyEl.classList.remove('active');
    if (onNoteOff) onNoteOff(note);
    if (activeNote === note) activeNote = null;
  }

  function handleStart(e) {
    e.preventDefault();
    const target = e.target;
    if (target.dataset.note) noteOn(parseInt(target.dataset.note));
  }

  function handleMove(e) {
    if (activeNote === null) return;
    const touch = e.touches ? e.touches[0] : e;
    const target = document.elementFromPoint(touch.clientX, touch.clientY);
    if (target && target.dataset.note) {
      const note = parseInt(target.dataset.note);
      if (note !== activeNote) noteOn(note);
    }
  }

  function handleEnd() {
    if (activeNote !== null) noteOff(activeNote);
  }

  el.addEventListener('mousedown', handleStart);
  el.addEventListener('mousemove', (e) => { if (e.buttons === 1) handleMove(e); });
  el.addEventListener('mouseup', handleEnd);
  el.addEventListener('mouseleave', handleEnd);
  el.addEventListener('touchstart', handleStart, { passive: false });
  el.addEventListener('touchmove', handleMove, { passive: false });
  el.addEventListener('touchend', handleEnd);

  return {
    el,
    noteOn: (note) => { const keyEl = keyEls.get(note); if (keyEl) keyEl.classList.add('active'); },
    noteOff: (note) => { const keyEl = keyEls.get(note); if (keyEl) keyEl.classList.remove('active'); },
    allNotesOff: () => { keyEls.forEach((keyEl) => keyEl.classList.remove('active')); activeNote = null; }
  };
}

// ============================================
// Waveform Component
// ============================================
export function Waveform({ label = '', size = '8x4' }) {
  const el = document.createElement('div');
  el.className = `waveform waveform--${size}`;

  const canvas = document.createElement('canvas');
  el.append(canvas);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  let ctx = null;
  const resizeObserver = new ResizeObserver(() => {
    canvas.width = canvas.clientWidth * (window.devicePixelRatio || 1);
    canvas.height = canvas.clientHeight * (window.devicePixelRatio || 1);
    ctx = canvas.getContext('2d');
  });
  resizeObserver.observe(canvas);

  return {
    el,
    draw: (data) => {
      if (!ctx) return;
      const w = canvas.width, h = canvas.height;
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--color-accent').trim() || '#00a3ff';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      const step = w / data.length;
      for (let i = 0; i < data.length; i++) {
        const y = (1 - (data[i] + 1) / 2) * h;
        if (i === 0) ctx.moveTo(i * step, y);
        else ctx.lineTo(i * step, y);
      }
      ctx.stroke();
    }
  };
}

// ============================================
// Spectrum Component
// ============================================
export function Spectrum({ label = '', size = '8x4' }) {
  const el = document.createElement('div');
  el.className = `spectrum spectrum--${size}`;

  const canvas = document.createElement('canvas');
  el.append(canvas);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  let ctx = null;
  const resizeObserver = new ResizeObserver(() => {
    canvas.width = canvas.clientWidth * (window.devicePixelRatio || 1);
    canvas.height = canvas.clientHeight * (window.devicePixelRatio || 1);
    ctx = canvas.getContext('2d');
  });
  resizeObserver.observe(canvas);

  return {
    el,
    draw: (data) => {
      if (!ctx) return;
      const w = canvas.width, h = canvas.height;
      ctx.clearRect(0, 0, w, h);
      const barWidth = w / data.length;
      const accent = getComputedStyle(document.documentElement).getPropertyValue('--color-accent').trim() || '#00a3ff';
      ctx.fillStyle = accent;
      for (let i = 0; i < data.length; i++) {
        const barHeight = (data[i] / 255) * h;
        ctx.fillRect(i * barWidth, h - barHeight, barWidth - 1, barHeight);
      }
    }
  };
}

// ============================================
// StereoMeter Component
// ============================================
export function StereoMeter({ label = '', size = '2x4' }) {
  const el = document.createElement('div');
  el.className = `stereo-meter stereo-meter--${size}`;

  const pair = document.createElement('div');
  pair.className = 'meter-pair';

  const leftCh = document.createElement('div');
  leftCh.className = 'meter-channel';
  const leftBar = document.createElement('div');
  leftBar.className = 'meter-bar';
  leftBar.style.height = '0%';
  leftCh.append(leftBar);

  const rightCh = document.createElement('div');
  rightCh.className = 'meter-channel';
  const rightBar = document.createElement('div');
  rightBar.className = 'meter-bar';
  rightBar.style.height = '0%';
  rightCh.append(rightBar);

  pair.append(leftCh, rightCh);
  el.append(pair);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  return {
    el,
    setLevel: (l, r = l) => {
      leftBar.style.height = `${clamp01(l) * 100}%`;
      rightBar.style.height = `${clamp01(r) * 100}%`;
    }
  };
}

// ============================================
// RadioGroup Component
// ============================================
export function RadioGroup({ options, defaultIndex = 0, size = '4x2', onChange }) {
  const el = document.createElement('div');
  el.className = `radio-group radio-group--${size}`;

  let currentIndex = defaultIndex;
  const btns = [];

  options.forEach((opt, i) => {
    const btn = document.createElement('div');
    btn.className = `radio-option${i === currentIndex ? ' selected' : ''}`;
    btn.textContent = opt;
    btn.addEventListener('click', () => {
      currentIndex = i;
      btns.forEach((b, j) => b.classList.toggle('selected', j === i));
      if (onChange) onChange(opt, i);
    });
    btns.push(btn);
    el.append(btn);
  });

  return { el, getValue: () => options[currentIndex], getIndex: () => currentIndex, setIndex: (i) => { currentIndex = i; btns.forEach((b, j) => b.classList.toggle('selected', j === i)); } };
}

// ============================================
// XYPad Component
// ============================================
export function XYPad({ label = '', size = '4x4', onChange }) {
  const el = document.createElement('div');
  el.className = `xy-pad xy-pad--${size}`;

  const pad = document.createElement('div');
  pad.className = 'pad-area';

  const cursor = document.createElement('div');
  cursor.className = 'pad-cursor';
  cursor.style.left = '50%';
  cursor.style.top = '50%';
  pad.append(cursor);

  el.append(pad);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  let x = 0.5, y = 0.5;
  let dragging = false;

  function update(e) {
    const rect = pad.getBoundingClientRect();
    x = clamp01((e.clientX - rect.left) / rect.width);
    y = clamp01((e.clientY - rect.top) / rect.height);
    cursor.style.left = `${x * 100}%`;
    cursor.style.top = `${y * 100}%`;
    if (onChange) onChange(x, 1 - y);
  }

  pad.addEventListener('pointerdown', (e) => { dragging = true; pad.setPointerCapture(e.pointerId); update(e); });
  pad.addEventListener('pointermove', (e) => { if (dragging) update(e); });
  pad.addEventListener('pointerup', () => dragging = false);
  pad.addEventListener('pointercancel', () => dragging = false);

  return { el, getValue: () => ({ x, y: 1 - y }), setValue: (nx, ny) => { x = clamp01(nx); y = clamp01(1 - ny); cursor.style.left = `${x * 100}%`; cursor.style.top = `${y * 100}%`; } };
}

// ============================================
// Goniometer Component
// ============================================
export function Goniometer({ label = '', size = '4x4' }) {
  const el = document.createElement('div');
  el.className = `goniometer goniometer--${size}`;

  const canvas = document.createElement('canvas');
  el.append(canvas);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  let ctx = null;
  const resizeObserver = new ResizeObserver(() => {
    const size = Math.min(canvas.clientWidth, canvas.clientHeight);
    canvas.width = size * (window.devicePixelRatio || 1);
    canvas.height = size * (window.devicePixelRatio || 1);
    ctx = canvas.getContext('2d');
  });
  resizeObserver.observe(canvas);

  return {
    el,
    draw: (leftData, rightData) => {
      if (!ctx) return;
      const w = canvas.width, h = canvas.height;
      ctx.fillStyle = 'rgba(0,0,0,0.1)';
      ctx.fillRect(0, 0, w, h);

      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--color-accent').trim() || '#00a3ff';
      ctx.lineWidth = 1;
      ctx.beginPath();

      const len = Math.min(leftData.length, rightData.length);
      for (let i = 0; i < len; i++) {
        const l = leftData[i], r = rightData[i];
        const x = ((r - l) / 2 + 0.5) * w;
        const y = (1 - (l + r) / 2 - 0.5) * h + h / 2;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
  };
}

// ============================================
// Panel Component
// ============================================
export function Panel({ title = '', actions = null, mode = 'fixed' }) {
  const el = document.createElement('div');
  el.className = 'panel' + (mode === 'expand' ? ' panel--expand' : '');

  const header = document.createElement('div');
  header.className = 'panel-header';

  if (title) {
    const titleEl = document.createElement('div');
    titleEl.className = 'panel-title';
    titleEl.textContent = title;
    header.append(titleEl);
  }

  if (actions) {
    const actionsWrap = document.createElement('div');
    actionsWrap.className = 'panel-actions';
    if (Array.isArray(actions)) {
      actions.forEach(a => actionsWrap.append(a.el || a));
    } else {
      actionsWrap.append(actions.el || actions);
    }
    header.append(actionsWrap);
  }
  el.append(header);

  const body = document.createElement('div');
  body.className = 'panel-body';

  let leftCol = null;
  let rightCol = null;
  if (mode === 'expand') {
    const cols = document.createElement('div');
    cols.className = 'panel-columns';
    leftCol = document.createElement('div');
    leftCol.className = 'panel-col';
    rightCol = document.createElement('div');
    rightCol.className = 'panel-col';
    cols.append(leftCol, rightCol);
    body.append(cols);
  }

  el.append(body);

  let currentCol = 'left';

  return {
    el,
    header,
    body,
    toRight: () => { currentCol = 'right'; },
    toLeft: () => { currentCol = 'left'; },
    append: (...children) => {
      const target = (mode === 'expand' && currentCol === 'right') ? rightCol : (leftCol || body);
      children.forEach(c => target.append(c.el || c));
    },
    appendGrid: (...children) => {
      const target = (mode === 'expand' && currentCol === 'right') ? rightCol : (leftCol || body);
      const grid = document.createElement('div');
      grid.className = 'ctrl-grid';
      children.forEach(c => grid.append(c.el || c));
      target.append(grid);
      return grid;
    },
    appendSection: (label, ...children) => {
      const target = (mode === 'expand' && currentCol === 'right') ? rightCol : (leftCol || body);
      const sec = document.createElement('div');
      sec.className = 'section';
      if (label) {
        const h = document.createElement('div');
        h.className = 'section-header';
        h.textContent = label;
        sec.append(h);
      }
      const grid = document.createElement('div');
      grid.className = 'ctrl-grid';
      children.forEach(c => grid.append(c.el || c));
      sec.append(grid);
      target.append(sec);
      return { section: sec, grid };
    }
  };
}

// ============================================
// Scope Component
// ============================================
export function Scope(label, fftSize, mode, size = '16x4') {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'block';
  el.style.position = 'relative';
  el.style.overflow = 'hidden';
  el.style.background = 'var(--color-canvas)';
  el.style.padding = '0';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.position = 'absolute';
  labelEl.style.top = '4px';
  labelEl.style.left = '6px';
  labelEl.style.zIndex = '1';
  labelEl.style.pointerEvents = 'none';

  const canvas = document.createElement('canvas');
  canvas.width = 640;
  canvas.height = 160;
  canvas.style.display = 'block';
  canvas.style.width = '100%';
  canvas.style.height = '100%';
  const g = canvas.getContext('2d');
  const data = new Uint8Array(fftSize);
  el.append(canvas, labelEl);

  function draw(getData) {
    getData(data);
    const w = canvas.width;
    const h = canvas.height;
    g.clearRect(0, 0, w, h);
    g.fillStyle = 'rgba(255,255,255,0.06)';
    g.fillRect(0, 0, w, h);

    g.strokeStyle = 'rgba(255,255,255,0.15)';
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(0, h * 0.5);
    g.lineTo(w, h * 0.5);
    g.stroke();

    g.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--color-accent');
    g.lineWidth = 2;
    g.beginPath();

    if (mode === 'time') {
      for (let i = 0; i < data.length; i++) {
        const x = (i / (data.length - 1)) * w;
        const v = (data[i] - 128) / 128;
        const y = h * 0.5 - v * (h * 0.42);
        if (i === 0) g.moveTo(x, y);
        else g.lineTo(x, y);
      }
    } else {
      for (let i = 0; i < data.length; i++) {
        const x = (i / (data.length - 1)) * w;
        const v = data[i] / 255;
        const y = h - v * (h * 0.9) - h * 0.05;
        if (i === 0) g.moveTo(x, y);
        else g.lineTo(x, y);
      }
    }
    g.stroke();
  }

  return { el, draw };
}

// ============================================
// Meter Component
// ============================================
export function Meter(label = '', size = '16x2') {
  const el = document.createElement('div');
  const [cols, rows] = size.split('x').map(Number);
  el.style.cssText = `
    grid-column: span ${cols};
    grid-row: span ${rows};
    display: flex;
    flex-direction: row;
    align-items: center;
    gap: var(--space-sm);
    padding: var(--space-sm);
    background: var(--color-panel2);
    border: var(--border-width) solid var(--color-line);
    border-radius: var(--ui-radius);
    box-sizing: border-box;
    margin: 1px;
  `;

  if (label) {
    const labelEl = document.createElement('div');
    labelEl.className = 'label';
    labelEl.textContent = label;
    labelEl.style.flexShrink = '0';
    el.append(labelEl);
  }

  const box = document.createElement('div');
  box.style.flex = '1';
  box.style.minWidth = '0';

  const base = document.createElement('div');
  base.style.height = '14px';
  base.style.border = 'var(--border-width) solid var(--color-line)';
  base.style.borderRadius = 'var(--radius-full)';
  base.style.position = 'relative';
  base.style.overflow = 'hidden';
  base.style.background = 'var(--color-canvas)';

  const bar = document.createElement('div');
  bar.style.position = 'absolute';
  bar.style.left = '0';
  bar.style.top = '0';
  bar.style.bottom = '0';
  bar.style.width = '0%';
  bar.style.background = 'var(--color-accent)';
  bar.style.opacity = '0.85';

  const peak = document.createElement('div');
  peak.style.position = 'absolute';
  peak.style.top = '0';
  peak.style.bottom = '0';
  peak.style.width = '2px';
  peak.style.left = '0%';
  peak.style.background = 'var(--color-warn)';
  peak.style.opacity = '0.95';

  base.append(bar, peak);
  box.append(base);
  el.append(box);

  function set(level01, peak01) {
    const v = clamp01(level01);
    const p = clamp01(peak01);
    bar.style.width = `${(v * 100).toFixed(1)}%`;
    peak.style.left = `${(p * 100).toFixed(1)}%`;
  }

  return { el, set };
}

// ============================================
// Shell Component
// ============================================
export function Shell({ maxLines = 100, gridSize = '16x6' }) {
  const el = document.createElement('div');
  el.className = 'shell';
  const [cols, rows] = gridSize.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;

  const lines = [];

  function addLine(text, type = '') {
    const p = document.createElement('p');
    p.className = 'shell-line' + (type ? ` shell-line--${type}` : '');
    p.textContent = text;
    lines.push(p);
    if (lines.length > maxLines) {
      const old = lines.shift();
      old.remove();
    }
    el.append(p);
    el.scrollTop = el.scrollHeight;
  }

  return {
    el,
    log: (text) => addLine(text),
    info: (text) => addLine(text, 'info'),
    warn: (text) => addLine(text, 'warn'),
    error: (text) => addLine(text, 'error'),
    prompt: (text) => addLine('> ' + text, 'prompt'),
    clear: () => { lines.length = 0; el.replaceChildren(); },
  };
}

// ============================================
// PeakMeter Component
// ============================================
export function PeakMeter({ label = '', size = 'thin-short' }) {
  const el = document.createElement('div');
  el.className = `peak-meter peak-meter--${size}`;

  const clip = document.createElement('div');
  clip.className = 'clip-indicator';

  const wrap = document.createElement('div');
  wrap.className = 'meter-bar-wrap';

  const bar = document.createElement('div');
  bar.className = 'meter-bar';
  bar.style.height = '0%';
  wrap.append(bar);

  el.append(clip, wrap);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  let clipped = false;
  let clipTimeout = null;

  return {
    el,
    setLevel: (v) => {
      bar.style.height = `${clamp01(v) * 100}%`;
      if (v >= 0.99 && !clipped) {
        clipped = true;
        clip.classList.add('clipped');
        if (clipTimeout) clearTimeout(clipTimeout);
        clipTimeout = setTimeout(() => {
          clipped = false;
          clip.classList.remove('clipped');
        }, 2000);
      }
    },
    resetClip: () => {
      clipped = false;
      clip.classList.remove('clipped');
    }
  };
}

// ============================================
// VUMeter Component
// ============================================
export function VUMeter({ orientation = 'h', stereo = false, label = '', gridSize = '4x2' }) {
  const el = document.createElement('div');
  el.className = stereo ? `vu-stereo vu-stereo--${orientation}` : '';
  const [cols, rows] = gridSize.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;

  function createBar() {
    const bar = document.createElement('div');
    bar.className = `vu-meter vu-meter--${orientation}`;
    const base = document.createElement('div');
    base.className = 'vu-bar';
    const fill = document.createElement('div');
    fill.className = 'vu-fill';
    const peak = document.createElement('div');
    peak.className = 'vu-peak';
    base.append(fill, peak);
    bar.append(base);
    return { bar, fill, peak };
  }

  const left = createBar();
  const right = stereo ? createBar() : null;

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'vu-label';
    lbl.textContent = label;
    el.append(lbl);
  }

  el.append(left.bar);
  if (right) el.append(right.bar);

  function setLevel(l, r = l) {
    const prop = orientation === 'h' ? 'width' : 'height';
    left.fill.style[prop] = `${clamp01(l) * 100}%`;
    if (right) right.fill.style[prop] = `${clamp01(r) * 100}%`;
  }

  function setPeak(l, r = l) {
    const prop = orientation === 'h' ? 'left' : 'bottom';
    left.peak.style[prop] = `${clamp01(l) * 100}%`;
    if (right) right.peak.style[prop] = `${clamp01(r) * 100}%`;
  }

  return { el, setLevel, setPeak };
}

// ============================================
// LEDBar Component
// ============================================
export function LEDBar({ size = '1x4', ledCount = 8 }) {
  const el = document.createElement('div');
  el.className = `led-bar led-bar--${size}`;

  const leds = document.createElement('div');
  leds.className = 'leds';

  const ledEls = [];
  for (let i = 0; i < ledCount; i++) {
    const led = document.createElement('div');
    led.className = 'led';
    ledEls.push(led);
    leds.append(led);
  }
  el.append(leds);

  return {
    el,
    setLevel: (v) => {
      const activeCount = Math.round(clamp01(v) * ledCount);
      ledEls.forEach((led, i) => {
        led.className = 'led';
        if (i < activeCount) {
          const ratio = i / ledCount;
          if (ratio >= 0.9) led.classList.add('on-red');
          else if (ratio >= 0.7) led.classList.add('on-yellow');
          else led.classList.add('on-green');
        }
      });
    }
  };
}

// ============================================
// PhaseMeter Component
// ============================================
export function PhaseMeter({ label = 'Phase', size = '4x4' }) {
  const el = document.createElement('div');
  el.className = `phase-meter phase-meter--${size}`;

  const canvas = document.createElement('canvas');
  el.append(canvas);

  const readout = document.createElement('div');
  readout.className = 'readout';
  readout.textContent = '0.00';

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }
  el.append(readout);

  let ctx = null;
  const resizeObserver = new ResizeObserver(() => {
    canvas.width = canvas.clientWidth * (window.devicePixelRatio || 1);
    canvas.height = canvas.clientHeight * (window.devicePixelRatio || 1);
    ctx = canvas.getContext('2d');
  });
  resizeObserver.observe(canvas);

  return {
    el,
    setCorrelation: (v) => {
      readout.textContent = v.toFixed(2);
      if (!ctx) return;
      const w = canvas.width, h = canvas.height;
      ctx.clearRect(0, 0, w, h);

      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--color-line').trim() || '#333';
      ctx.beginPath();
      ctx.moveTo(0, h / 2);
      ctx.lineTo(w, h / 2);
      ctx.stroke();

      const x = ((v + 1) / 2) * w;
      ctx.fillStyle = v < 0 ? getComputedStyle(document.documentElement).getPropertyValue('--color-warn').trim() :
                      getComputedStyle(document.documentElement).getPropertyValue('--color-good').trim();
      ctx.beginPath();
      ctx.arc(x, h / 2, 6, 0, Math.PI * 2);
      ctx.fill();
    }
  };
}

// ============================================
// StepSequencer Component
// ============================================
export function StepSequencer({ steps = 8, size = '8x2', onChange }) {
  const el = document.createElement('div');
  el.className = `step-seq step-seq--${size}`;

  const state = new Array(steps).fill(false);
  const stepEls = [];
  let currentStep = -1;

  for (let i = 0; i < steps; i++) {
    const step = document.createElement('div');
    step.className = 'step';
    step.addEventListener('click', () => {
      state[i] = !state[i];
      step.classList.toggle('active', state[i]);
      if (onChange) onChange([...state], i);
    });
    stepEls.push(step);
    el.append(step);
  }

  return {
    el,
    getState: () => [...state],
    setState: (newState) => {
      newState.forEach((v, i) => {
        if (i < steps) {
          state[i] = v;
          stepEls[i].classList.toggle('active', v);
        }
      });
    },
    setCurrentStep: (i) => {
      if (currentStep >= 0 && currentStep < steps) {
        stepEls[currentStep].classList.remove('current');
      }
      currentStep = i;
      if (i >= 0 && i < steps) {
        stepEls[i].classList.add('current');
      }
    }
  };
}

// ============================================
// Lamp Component
// ============================================
export function Lamp({ label = '', size = 'md', state = 'off', gridSize = '2x2' }) {
  const el = document.createElement('div');
  el.className = 'lamp-row';
  const [cols, rows] = gridSize.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;

  const lamp = document.createElement('div');
  lamp.className = `lamp lamp--${size}`;

  const labelEl = document.createElement('span');
  labelEl.className = 'lamp-label';
  labelEl.textContent = label;

  if (label) el.append(lamp, labelEl);
  else el.append(lamp);

  function setState(s) {
    lamp.classList.remove('lamp--on', 'lamp--warn', 'lamp--bad');
    if (s === 'on') lamp.classList.add('lamp--on');
    else if (s === 'warn') lamp.classList.add('lamp--warn');
    else if (s === 'bad') lamp.classList.add('lamp--bad');
  }
  setState(state);

  return { el, setState, toggle: () => lamp.classList.toggle('lamp--on') };
}

// ============================================
// SegmentDisplay Component
// ============================================
export function SegmentDisplay({ label = '', digits = 4, value = 0 }) {
  const el = document.createElement('div');
  el.className = 'segment-display';

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  const disp = document.createElement('div');
  disp.className = 'digits';
  disp.textContent = String(value).padStart(digits, '0').slice(-digits);
  el.append(disp);

  return {
    el,
    setValue: (v) => {
      disp.textContent = String(Math.round(v)).padStart(digits, '0').slice(-digits);
    },
    setFloat: (v, decimals = 1) => {
      disp.textContent = v.toFixed(decimals);
    }
  };
}

// ============================================
// SegmentedControl Component
// ============================================
export function SegmentedControl({ options, defaultIndex = 0, onChange, gridSize = '8x2' }) {
  const el = document.createElement('div');
  el.className = 'seg-ctrl';
  const [cols, rows] = gridSize.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;

  let currentIndex = defaultIndex;
  const btns = [];

  for (let i = 0; i < options.length; i++) {
    const btn = document.createElement('button');
    btn.className = 'seg-btn';
    btn.textContent = options[i];
    btn.setAttribute('aria-selected', String(i === currentIndex));
    btn.addEventListener('click', () => {
      currentIndex = i;
      btns.forEach((b, j) => b.setAttribute('aria-selected', String(j === i)));
      if (onChange) onChange(options[i], i);
    });
    btns.push(btn);
    el.append(btn);
  }

  return {
    el,
    getValue: () => options[currentIndex],
    getIndex: () => currentIndex,
    setIndex: (i) => {
      currentIndex = i;
      btns.forEach((b, j) => b.setAttribute('aria-selected', String(j === i)));
    },
  };
}

// ============================================
// WaveSelector Component
// ============================================
export function WaveSelector({ label = 'Wave', defaultIndex = 0, onChange }) {
  const el = document.createElement('div');
  el.className = 'wave-selector';

  const icons = document.createElement('div');
  icons.className = 'icons';

  const waves = [
    { name: 'Sine', path: 'M2 8 Q6 2, 10 8 Q14 14, 18 8' },
    { name: 'Saw', path: 'M2 12 L10 4 L10 12 L18 4 L18 12' },
    { name: 'Square', path: 'M2 12 L2 4 L10 4 L10 12 L18 12 L18 4' },
    { name: 'Triangle', path: 'M2 12 L6 4 L14 12 L18 4' }
  ];

  let currentIndex = defaultIndex;
  const iconEls = [];

  waves.forEach((wave, i) => {
    const icon = document.createElement('div');
    icon.className = `wave-icon${i === currentIndex ? ' selected' : ''}`;
    icon.innerHTML = `<svg viewBox="0 0 20 16"><path d="${wave.path}"/></svg>`;
    icon.title = wave.name;
    icon.addEventListener('click', () => {
      currentIndex = i;
      iconEls.forEach((ic, j) => ic.classList.toggle('selected', j === i));
      if (onChange) onChange(wave.name, i);
    });
    iconEls.push(icon);
    icons.append(icon);
  });

  el.append(icons);

  if (label) {
    const lbl = document.createElement('div');
    lbl.className = 'label';
    lbl.textContent = label;
    el.append(lbl);
  }

  return {
    el,
    getValue: () => waves[currentIndex].name,
    getIndex: () => currentIndex,
    setIndex: (i) => {
      currentIndex = i;
      iconEls.forEach((ic, j) => ic.classList.toggle('selected', j === i));
    }
  };
}

// ============================================
// Encoder Component
// ============================================
export function Encoder({ label, onChange, sensitivity = 0.01 }) {
  const el = document.createElement('div');
  el.className = 'ctrl knob';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  const size = 60;
  svg.setAttribute('viewBox', `0 0 ${size} ${size}`);
  svg.setAttribute('width', size);
  svg.setAttribute('height', size);
  svg.setAttribute('tabindex', '0');
  svg.style.cursor = 'pointer';
  svg.style.outline = 'none';

  const bg = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  bg.setAttribute('cx', size/2);
  bg.setAttribute('cy', size/2);
  bg.setAttribute('r', size/2 - 4);
  bg.setAttribute('fill', 'var(--color-panel)');
  bg.setAttribute('stroke', 'var(--color-line)');
  bg.setAttribute('stroke-width', '2');

  const indicator = document.createElementNS('http://www.w3.org/2000/svg', 'line');
  indicator.setAttribute('x1', size/2);
  indicator.setAttribute('y1', 8);
  indicator.setAttribute('x2', size/2);
  indicator.setAttribute('y2', 16);
  indicator.setAttribute('stroke', 'var(--color-accent)');
  indicator.setAttribute('stroke-width', '3');
  indicator.setAttribute('stroke-linecap', 'round');

  svg.append(bg, indicator);

  const readout = document.createElement('div');
  readout.className = 'readout';
  readout.textContent = '0';

  el.append(labelEl, svg, readout);

  let angle = 0;
  let dragging = false;
  let lastY = 0;

  function render() {
    indicator.setAttribute('transform', `rotate(${angle}, ${size/2}, ${size/2})`);
  }

  svg.addEventListener('pointerdown', (e) => {
    dragging = true;
    lastY = e.clientY;
    svg.setPointerCapture(e.pointerId);
  });
  svg.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const delta = lastY - e.clientY;
    lastY = e.clientY;
    angle = (angle + delta * 3) % 360;
    render();
    if (onChange) onChange(delta * sensitivity);
  });
  svg.addEventListener('pointerup', () => { dragging = false; });
  svg.addEventListener('pointercancel', () => { dragging = false; });

  render();

  return {
    el,
    setAngle: (a) => { angle = a; render(); },
    setReadout: (text) => { readout.textContent = text; },
  };
}

// ============================================
// MultiSelect Component
// ============================================
export function MultiSelect({ label, items = [], onChange, gridSize = '8x4' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = gridSize.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.gap = 'var(--space-xs)';
  el.style.overflow = 'auto';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.flexShrink = '0';
  labelEl.style.textAlign = 'left';

  const list = document.createElement('div');
  list.className = 'multi-select';

  el.append(labelEl, list);

  const selected = new Set();

  function render() {
    list.replaceChildren();
    for (const item of items) {
      const row = document.createElement('label');
      row.className = 'multi-select-item';

      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.checked = selected.has(item.id);
      cb.addEventListener('change', () => {
        if (cb.checked) selected.add(item.id);
        else selected.delete(item.id);
        if (onChange) onChange([...selected]);
      });

      const span = document.createElement('span');
      span.textContent = item.label || item.id;

      row.append(cb, span);
      list.append(row);
    }
  }

  render();

  return {
    el,
    setItems: (newItems) => { items = newItems; render(); },
    getSelected: () => [...selected],
    setSelected: (ids) => { selected.clear(); ids.forEach(id => selected.add(id)); render(); },
  };
}

// ============================================
// Histogram Component
// ============================================
export function Histogram({ label, bars = 32, size = '8x4' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.padding = '4px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.marginBottom = '2px';

  const canvas = document.createElement('canvas');
  canvas.style.flex = '1';
  canvas.style.width = '100%';
  canvas.style.background = 'var(--color-canvas)';
  canvas.style.borderRadius = '2px';

  el.append(labelEl, canvas);

  const data = new Float32Array(bars);
  let ctx = null;

  function resize() {
    canvas.width = canvas.offsetWidth * devicePixelRatio;
    canvas.height = canvas.offsetHeight * devicePixelRatio;
    ctx = canvas.getContext('2d');
    ctx.scale(devicePixelRatio, devicePixelRatio);
    draw();
  }

  function draw() {
    if (!ctx) return;
    const w = canvas.offsetWidth;
    const h = canvas.offsetHeight;
    ctx.clearRect(0, 0, w, h);
    const barW = w / bars;
    ctx.fillStyle = getComputedStyle(el).getPropertyValue('--color-accent') || '#ff9f43';
    for (let i = 0; i < bars; i++) {
      const barH = data[i] * h;
      ctx.fillRect(i * barW + 1, h - barH, barW - 2, barH);
    }
  }

  requestAnimationFrame(resize);
  new ResizeObserver(resize).observe(canvas);

  return {
    el,
    push: (value) => {
      for (let i = 0; i < bars - 1; i++) data[i] = data[i + 1];
      data[bars - 1] = clamp01(value);
      draw();
    },
    setData: (arr) => {
      for (let i = 0; i < bars; i++) data[i] = arr[i] || 0;
      draw();
    },
  };
}

// ============================================
// FileDropZone Component
// ============================================
export function FileDropZone({ label = 'Drop file here', accept = '*', onFile, size = '8x4' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.alignItems = 'center';
  el.style.justifyContent = 'center';
  el.style.border = '2px dashed var(--color-line)';
  el.style.cursor = 'pointer';
  el.style.transition = 'border-color 0.2s, background 0.2s';

  const icon = document.createElement('div');
  icon.textContent = '📁';
  icon.style.fontSize = '24px';
  icon.style.marginBottom = '4px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = accept;
  fileInput.style.display = 'none';

  el.append(icon, labelEl, fileInput);

  el.onclick = () => fileInput.click();

  el.ondragover = (e) => {
    e.preventDefault();
    el.style.borderColor = 'var(--color-accent)';
    el.style.background = 'var(--color-panel)';
  };
  el.ondragleave = () => {
    el.style.borderColor = 'var(--color-line)';
    el.style.background = '';
  };
  el.ondrop = (e) => {
    e.preventDefault();
    el.style.borderColor = 'var(--color-line)';
    el.style.background = '';
    const file = e.dataTransfer.files[0];
    if (file && onFile) onFile(file);
  };
  fileInput.onchange = () => {
    const file = fileInput.files[0];
    if (file && onFile) onFile(file);
  };

  return {
    el,
    setLabel: (text) => { labelEl.textContent = text; },
  };
}

// ============================================
// EnvelopeEditor Component
// ============================================
export function EnvelopeEditor({ label = 'Envelope', onChange, size = '8x4' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.padding = '4px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.marginBottom = '2px';

  const canvas = document.createElement('canvas');
  canvas.style.flex = '1';
  canvas.style.width = '100%';
  canvas.style.background = 'var(--color-canvas)';
  canvas.style.borderRadius = '2px';
  canvas.style.cursor = 'crosshair';

  el.append(labelEl, canvas);

  const adsr = { attack: 0.1, decay: 0.2, sustain: 0.7, release: 0.3 };
  let ctx = null;
  let dragging = null;

  function resize() {
    canvas.width = canvas.offsetWidth * devicePixelRatio;
    canvas.height = canvas.offsetHeight * devicePixelRatio;
    ctx = canvas.getContext('2d');
    ctx.scale(devicePixelRatio, devicePixelRatio);
    draw();
  }

  function draw() {
    if (!ctx) return;
    const w = canvas.offsetWidth;
    const h = canvas.offsetHeight;
    const pad = 4;
    ctx.clearRect(0, 0, w, h);

    const totalTime = adsr.attack + adsr.decay + 0.3 + adsr.release;
    const scaleX = (w - pad * 2) / totalTime;
    const points = [
      { x: pad, y: h - pad },
      { x: pad + adsr.attack * scaleX, y: pad },
      { x: pad + (adsr.attack + adsr.decay) * scaleX, y: pad + (1 - adsr.sustain) * (h - pad * 2) },
      { x: pad + (adsr.attack + adsr.decay + 0.3) * scaleX, y: pad + (1 - adsr.sustain) * (h - pad * 2) },
      { x: w - pad, y: h - pad },
    ];

    ctx.strokeStyle = getComputedStyle(el).getPropertyValue('--color-accent') || '#ff9f43';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(points[0].x, points[0].y);
    for (let i = 1; i < points.length; i++) {
      ctx.lineTo(points[i].x, points[i].y);
    }
    ctx.stroke();

    ctx.fillStyle = ctx.strokeStyle;
    points.slice(1, 4).forEach((p) => {
      ctx.beginPath();
      ctx.arc(p.x, p.y, 5, 0, Math.PI * 2);
      ctx.fill();
    });
  }

  function getHandle(x, y) {
    const w = canvas.offsetWidth;
    const h = canvas.offsetHeight;
    const pad = 4;
    const totalTime = adsr.attack + adsr.decay + 0.3 + adsr.release;
    const scaleX = (w - pad * 2) / totalTime;
    const points = [
      { x: pad + adsr.attack * scaleX, y: pad, key: 'a' },
      { x: pad + (adsr.attack + adsr.decay) * scaleX, y: pad + (1 - adsr.sustain) * (h - pad * 2), key: 'd' },
      { x: pad + (adsr.attack + adsr.decay + 0.3) * scaleX, y: pad + (1 - adsr.sustain) * (h - pad * 2), key: 's' },
    ];
    for (const p of points) {
      if (Math.hypot(x - p.x, y - p.y) < 10) return p.key;
    }
    return null;
  }

  canvas.onpointerdown = (e) => {
    const rect = canvas.getBoundingClientRect();
    dragging = getHandle(e.clientX - rect.left, e.clientY - rect.top);
    if (dragging) canvas.setPointerCapture(e.pointerId);
  };
  canvas.onpointermove = (e) => {
    if (!dragging) return;
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const w = canvas.offsetWidth;
    const h = canvas.offsetHeight;

    if (dragging === 'a') {
      adsr.attack = clamp01(x / w) * 0.5;
    } else if (dragging === 'd') {
      adsr.decay = clamp01((x / w - adsr.attack) * 2);
      adsr.sustain = clamp01(1 - (y / h));
    } else if (dragging === 's') {
      adsr.sustain = clamp01(1 - (y / h));
    }
    draw();
    if (onChange) onChange({ ...adsr });
  };
  canvas.onpointerup = () => { dragging = null; };

  requestAnimationFrame(resize);
  new ResizeObserver(resize).observe(canvas);

  return {
    el,
    getADSR: () => ({ ...adsr }),
    setADSR: (a, d, s, r) => {
      adsr.attack = a; adsr.decay = d; adsr.sustain = s; adsr.release = r;
      draw();
    },
  };
}

// ============================================
// FilterGraph Component
// ============================================
export function FilterGraph({ label = 'Filter', size = '8x4' }) {
  const el = document.createElement('div');
  el.className = 'ctrl';
  const [cols, rows] = size.split('x').map(Number);
  el.style.gridColumn = `span ${cols}`;
  el.style.gridRow = `span ${rows}`;
  el.style.display = 'flex';
  el.style.flexDirection = 'column';
  el.style.padding = '4px';

  const labelEl = document.createElement('div');
  labelEl.className = 'label';
  labelEl.textContent = label;
  labelEl.style.marginBottom = '2px';

  const canvas = document.createElement('canvas');
  canvas.style.flex = '1';
  canvas.style.width = '100%';
  canvas.style.background = 'var(--color-canvas)';
  canvas.style.borderRadius = '2px';

  el.append(labelEl, canvas);

  let ctx = null;
  let magnitudes = null;

  function resize() {
    canvas.width = canvas.offsetWidth * devicePixelRatio;
    canvas.height = canvas.offsetHeight * devicePixelRatio;
    ctx = canvas.getContext('2d');
    ctx.scale(devicePixelRatio, devicePixelRatio);
    draw();
  }

  function draw() {
    if (!ctx) return;
    const w = canvas.offsetWidth;
    const h = canvas.offsetHeight;
    ctx.clearRect(0, 0, w, h);

    ctx.strokeStyle = 'var(--color-line)';
    ctx.lineWidth = 0.5;
    for (let db = -24; db <= 12; db += 12) {
      const y = h / 2 - (db / 36) * h;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }

    if (!magnitudes || magnitudes.length === 0) {
      ctx.strokeStyle = getComputedStyle(el).getPropertyValue('--color-accent') || '#ff9f43';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(0, h / 2);
      ctx.lineTo(w, h / 2);
      ctx.stroke();
      return;
    }

    ctx.strokeStyle = getComputedStyle(el).getPropertyValue('--color-accent') || '#ff9f43';
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < magnitudes.length; i++) {
      const x = (i / magnitudes.length) * w;
      const db = 20 * Math.log10(Math.max(0.001, magnitudes[i]));
      const y = h / 2 - (db / 36) * h;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  requestAnimationFrame(resize);
  new ResizeObserver(resize).observe(canvas);

  return {
    el,
    setResponse: (freqs, mags) => {
      magnitudes = mags;
      draw();
    },
    setFilter: (type, cutoff, q) => {
      const nPoints = 128;
      magnitudes = new Float32Array(nPoints);
      for (let i = 0; i < nPoints; i++) {
        const freq = 20 * Math.pow(10, i / nPoints * 3);
        const w = freq / cutoff;
        let mag;
        if (type === 'lowpass') {
          mag = 1 / Math.sqrt(1 + Math.pow(w, 4) / (q * q));
        } else if (type === 'highpass') {
          mag = (w * w) / Math.sqrt(1 + Math.pow(w, 4) / (q * q));
        } else if (type === 'bandpass') {
          mag = (w / q) / Math.sqrt(Math.pow(1 - w * w, 2) + Math.pow(w / q, 2));
        } else {
          mag = 1;
        }
        magnitudes[i] = Math.min(10, Math.max(0.001, mag));
      }
      draw();
    },
  };
}

// Re-export createKnobView for advanced usage
export { createKnobView };
