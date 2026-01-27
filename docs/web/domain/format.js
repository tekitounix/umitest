/**
 * Format utilities for displaying parameter values
 */

/**
 * Format utility object containing various formatters
 */
export const Format = {
  /**
   * Format frequency value
   * @param {number} x - Frequency in Hz
   * @returns {string} Formatted string (e.g., "440" or "1.20 k")
   */
  hz: (x) => (x >= 1000 ? (x / 1000).toFixed(2) + ' k' : x.toFixed(0)),

  /**
   * Format gain as decibels
   * @param {number} g - Linear gain value
   * @returns {string} Formatted dB string
   */
  db: (g) => (20 * Math.log10(Math.max(1e-6, g))).toFixed(1),

  /**
   * Format Q/resonance value
   * @param {number} x - Q value
   * @returns {string} Formatted string with 2 decimal places
   */
  q: (x) => x.toFixed(2),

  /**
   * Format percentage
   * @param {number} x - Value (0-1)
   * @returns {string} Formatted percentage string
   */
  percent: (x) => (x * 100).toFixed(0) + '%',

  /**
   * Format time in milliseconds
   * @param {number} x - Time in ms
   * @returns {string} Formatted time string
   */
  ms: (x) => x.toFixed(1) + ' ms',

  /**
   * Format time in seconds
   * @param {number} x - Time in seconds
   * @returns {string} Formatted time string
   */
  sec: (x) => x.toFixed(2) + ' s',
};
