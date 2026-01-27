/**
 * Common utility functions
 */

/**
 * Clamp a value to the range [0, 1]
 * @param {number} x - Value to clamp
 * @returns {number} Clamped value
 */
export const clamp01 = (x) => Math.max(0, Math.min(1, x));

/**
 * Query selector shorthand
 * @param {string} q - Query selector string
 * @param {Document|Element} r - Root element (default: document)
 * @returns {Element|null}
 */
export const $ = (q, r = document) => r.querySelector(q);

/**
 * Query selector all shorthand
 * @param {string} q - Query selector string
 * @param {Document|Element} r - Root element (default: document)
 * @returns {NodeListOf<Element>}
 */
export const $$ = (q, r = document) => r.querySelectorAll(q);

/**
 * Escape HTML entities
 * @param {string} str - String to escape
 * @returns {string} Escaped string
 */
export const esc = (str) => {
  const el = document.createElement('span');
  el.textContent = str;
  return el.innerHTML;
};
