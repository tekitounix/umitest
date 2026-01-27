/**
 * Knob logic - Framework-independent interaction handling
 * Handles pointer, wheel, and keyboard events for knob controls
 */

/**
 * @typedef {Object} KnobLogic
 * @property {(e: PointerEvent) => void} onPointerDown
 * @property {(e: PointerEvent) => boolean} onPointerMove
 * @property {() => void} onPointerUp
 * @property {() => void} onPointerCancel
 * @property {(e: WheelEvent) => void} onWheel
 * @property {(e: KeyboardEvent) => void} onKeyDown
 * @property {() => boolean} isDragging
 */

/**
 * Create knob interaction logic
 * @param {Object} options
 * @param {import('../domain/param.js').Param} options.param - Parameter to control
 * @param {number} [options.fineScale=0.25] - Scale factor for fine adjustment (shift key)
 * @param {number} [options.normalScale=1.0] - Normal scale factor
 * @returns {KnobLogic}
 */
export function createKnobLogic({ param, fineScale = 0.25, normalScale = 1.0 }) {
  let dragging = false;
  let startY = 0;
  let startV = 0;
  let lastPointerUpTime = 0;
  let hasMoved = false;

  const DRAG_SENSITIVITY = 0.006;
  const WHEEL_STEP = 0.03;
  const FINE_WHEEL_STEP = 0.01;
  const KEY_STEP = 0.05;
  const FINE_KEY_STEP = 0.01;
  const DOUBLE_CLICK_THRESHOLD = 300;

  return {
    // Pointer handlers
    onPointerDown: (e) => {
      dragging = true;
      hasMoved = false;
      startY = e.clientY;
      startV = param.get01();
    },
    onPointerMove: (e) => {
      if (!dragging) return false;
      const scale = e.shiftKey ? fineScale : normalScale;
      const dy = startY - e.clientY;
      // Only consider it a move if we've moved more than a few pixels
      if (Math.abs(dy) > 2) {
        hasMoved = true;
      }
      param.set01(startV + dy * DRAG_SENSITIVITY * scale);
      return true;
    },
    onPointerUp: () => {
      const now = Date.now();
      const wasDragging = dragging;
      dragging = false;

      // Double-click detection: only reset if we didn't drag
      if (!hasMoved && wasDragging) {
        if (now - lastPointerUpTime < DOUBLE_CLICK_THRESHOLD) {
          param.reset();
          lastPointerUpTime = 0; // Prevent triple-click
          return;
        }
      }
      lastPointerUpTime = now;
    },
    onPointerCancel: () => {
      dragging = false;
      hasMoved = false;
    },

    // Wheel handler
    onWheel: (e) => {
      const step = e.shiftKey ? FINE_WHEEL_STEP : WHEEL_STEP;
      param.set01(param.get01() + (e.deltaY < 0 ? step : -step));
    },

    // Keyboard handler
    onKeyDown: (e) => {
      const step = e.shiftKey ? FINE_KEY_STEP : KEY_STEP;
      switch (e.key) {
        case 'ArrowUp':
        case 'ArrowRight':
          e.preventDefault();
          param.set01(param.get01() + step);
          break;
        case 'ArrowDown':
        case 'ArrowLeft':
          e.preventDefault();
          param.set01(param.get01() - step);
          break;
        case 'Home':
          e.preventDefault();
          param.set01(0);
          break;
        case 'End':
          e.preventDefault();
          param.set01(1);
          break;
      }
    },

    isDragging: () => dragging,
  };
}
