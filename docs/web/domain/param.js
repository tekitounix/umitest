/**
 * Parameter factory and management
 * Param internal value is always 0..1 (normalized)
 * Actual values are derived via Scale conversion
 */

import { clamp01 } from '../lib/utils.js';

/**
 * @typedef {Object} Param
 * @property {string} id - Unique identifier
 * @property {string} label - Display label
 * @property {string} [unit] - Unit string (e.g., "Hz", "dB")
 * @property {() => number} get01 - Get current normalized value
 * @property {(x01: number, opts?: {silent?: boolean}) => void} set01 - Set normalized value
 * @property {() => number} getValue - Get actual value via scale conversion
 * @property {() => void} reset - Reset to default value
 * @property {() => string} display - Get formatted display string
 * @property {(fn: (v01: number) => void) => () => void} on - Register change listener
 * @property {() => string} learnName - Get name for MIDI Learn display
 */

/**
 * Create a parameter instance
 * @param {Object} options
 * @param {string} options.id - Unique identifier
 * @param {string} options.label - Display label
 * @param {string} [options.unit] - Unit string
 * @param {number} options.def01 - Default value (0..1)
 * @param {import('./scale.js').ScaleConverter} options.scale - Scale converter
 * @param {(value: number) => string} options.format - Value formatter function
 * @returns {Param}
 */
export function createParam({ id, label, unit, def01, scale, format }) {
  const def = clamp01(def01);
  let v01 = def;
  const listeners = new Set();

  return {
    id,
    label,
    unit,
    get01: () => v01,
    set01: (x01, { silent = false } = {}) => {
      v01 = clamp01(x01);
      if (!silent) {
        for (const fn of listeners) fn(v01);
      }
    },
    getValue: () => scale.toValue(v01),
    reset: () => {
      v01 = def;
      for (const fn of listeners) fn(v01);
    },
    display: () => format(scale.toValue(v01)) + (unit ? ' ' + unit : ''),
    on: (fn) => {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
    learnName: () => label,
  };
}
