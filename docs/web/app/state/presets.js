/**
 * Preset Storage
 * Handles persistence of synth presets to localStorage
 */

const PRESET_KEY = 'demo_presets_v2';

/**
 * Load presets from localStorage
 * @returns {Array} Array of preset objects
 */
export function loadPresets() {
  try {
    return JSON.parse(localStorage.getItem(PRESET_KEY) || '[]');
  } catch {
    return [];
  }
}

/**
 * Save presets to localStorage
 * @param {Array} list - Array of preset objects
 */
export function savePresets(list) {
  localStorage.setItem(PRESET_KEY, JSON.stringify(list));
}
