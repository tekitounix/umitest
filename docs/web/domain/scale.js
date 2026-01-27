/**
 * Scale functions for parameter value conversion
 * Converts between normalized 0..1 values and actual parameter values
 */

import { clamp01 } from '../lib/utils.js';

/**
 * @typedef {Object} ScaleConverter
 * @property {(v01: number) => number} toValue - Convert 0..1 to actual value
 * @property {(val: number) => number} to01 - Convert actual value to 0..1
 */

/**
 * Scale utility object containing various scale functions
 */
export const Scale = {
  /**
   * Linear scale
   * @param {number} min - Minimum value
   * @param {number} max - Maximum value
   * @returns {ScaleConverter}
   */
  linear: (min, max) => ({
    toValue: (v01) => min + clamp01(v01) * (max - min),
    to01: (val) => clamp01((val - min) / (max - min)),
  }),

  /**
   * Logarithmic scale (for frequencies, etc.)
   * @param {number} min - Minimum value (must be > 0)
   * @param {number} max - Maximum value
   * @returns {ScaleConverter}
   */
  log: (min, max) => ({
    toValue: (v01) => min * Math.pow(max / min, clamp01(v01)),
    to01: (val) => clamp01(Math.log(val / min) / Math.log(max / min)),
  }),

  /**
   * Decibel scale (for gain controls)
   * @param {number} minDb - Minimum dB value
   * @param {number} maxDb - Maximum dB value
   * @returns {ScaleConverter}
   */
  db: (minDb, maxDb) => ({
    toValue: (v01) => Math.pow(10, (minDb + clamp01(v01) * (maxDb - minDb)) / 20),
    to01: (gain) => clamp01((20 * Math.log10(Math.max(1e-6, gain)) - minDb) / (maxDb - minDb)),
  }),
};
