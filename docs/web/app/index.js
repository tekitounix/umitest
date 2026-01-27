/**
 * Application Entry Point
 * Handles tab navigation, theme switching, and initialization
 */

import { $ } from '../lib/utils.js';

// Re-export state management
export * from './state/index.js';

const THEME_KEY = 'ui_theme';

/**
 * Apply theme to document body
 * @param {string} themeClass - Theme class name (e.g., 'theme-midnight')
 */
export function applyTheme(themeClass) {
  document.body.classList.remove(
    'theme-midnight', 'theme-charcoal', 'theme-forest', 'theme-slate',
    'theme-light', 'theme-sepia'
  );
  if (themeClass) {
    document.body.classList.add(themeClass);
  }
  localStorage.setItem(THEME_KEY, themeClass || '');
}

/**
 * Load saved theme from localStorage
 * @returns {string} Saved theme class name
 */
export function loadSavedTheme() {
  return localStorage.getItem(THEME_KEY) || '';
}

/**
 * Initialize theme selector
 * @param {HTMLSelectElement} themeSel - Theme selector element
 */
export function initThemeSelector(themeSel) {
  const savedTheme = loadSavedTheme();
  if (savedTheme) {
    themeSel.value = savedTheme;
    applyTheme(savedTheme);
  }
  themeSel.onchange = () => applyTheme(themeSel.value);
}

/**
 * Initialize tab navigation
 * @param {Object} options
 * @param {HTMLElement} options.tabPlugin - Plugin tab button
 * @param {HTMLElement} options.tabNodes - Nodes tab button
 * @param {HTMLElement} options.wrap - Content wrapper
 * @param {HTMLElement} options.pluginView - Plugin view element
 * @param {HTMLElement} options.nodeView - Node view element
 */
export function initTabNavigation({ tabPlugin, tabNodes, wrap, pluginView, nodeView }) {
  function setTab(which) {
    tabPlugin.setAttribute('aria-selected', String(which === 'plugin'));
    tabNodes.setAttribute('aria-selected', String(which === 'nodes'));
    wrap.replaceChildren(which === 'plugin' ? pluginView : nodeView);
  }

  tabPlugin.onclick = () => setTab('plugin');
  tabNodes.onclick = () => setTab('nodes');
  setTab('plugin');
}

/**
 * Set status display
 * @param {HTMLElement} statusEl - Status element
 * @param {string} text - Status text
 * @param {boolean} ok - Status state (true=ok, false=bad)
 */
export function setStatus(statusEl, text, ok) {
  statusEl.textContent = text;
  statusEl.classList.toggle('ok', ok === true);
  statusEl.classList.toggle('bad', ok === false);
}

/**
 * Create horizontal rule element
 * @returns {HTMLDivElement}
 */
export function hr() {
  const d = document.createElement('div');
  d.className = 'hr';
  return d;
}

// MIDI utilities
/**
 * Request MIDI access
 * @returns {Promise<MIDIAccess|null>}
 */
export async function requestMidiAccess() {
  if (!navigator.requestMIDIAccess) return null;
  try {
    return await navigator.requestMIDIAccess({ sysex: false });
  } catch {
    return null;
  }
}

/**
 * Parse MIDI CC message
 * @param {MIDIMessageEvent} ev
 * @returns {{ ch: number, cc: number, value: number }|null}
 */
export function parseMidiCC(ev) {
  const d = ev.data;
  if (!d || d.length < 3) return null;
  const st = d[0] & 0xf0;
  const ch = d[0] & 0x0f;
  if (st !== 0xb0) return null;
  return { ch, cc: d[1], value: d[2] };
}

/**
 * Create key string for CC mapping
 * @param {number} ch - Channel
 * @param {number} cc - CC number
 * @returns {string}
 */
export function keyForCC(ch, cc) {
  return `cc:${ch}:${cc}`;
}

/**
 * Convert 7-bit MIDI value to 0-1 range
 * @param {number} v - 7-bit value (0-127)
 * @returns {number}
 */
export function v01From7bit(v) {
  return Math.max(0, Math.min(1, Math.max(0, Math.min(127, v)) / 127));
}
