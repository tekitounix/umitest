/**
 * MIDI Bindings Storage
 * Handles persistence of MIDI CC mappings to localStorage
 */

const MIDI_BINDING_KEY = 'midi_bindings_v1';

/**
 * Load MIDI bindings from localStorage
 * @returns {{ synthMap: Array, controlsMap: Array, noteChannel: number|null }}
 */
export function loadMidiBindings() {
  try {
    const data = JSON.parse(localStorage.getItem(MIDI_BINDING_KEY) || '{}');
    return {
      synthMap: data.synthMap || [],      // [[key, paramId], ...]
      controlsMap: data.controlsMap || [],
      noteChannel: data.noteChannel ?? null
    };
  } catch {
    return { synthMap: [], controlsMap: [], noteChannel: null };
  }
}

/**
 * Save MIDI bindings to localStorage
 * @param {Map} synthMap - Synth panel CC mappings
 * @param {Map} controlsMap - Controls panel CC mappings
 * @param {number|null} noteChannel - MIDI note channel
 */
export function saveMidiBindings(synthMap, controlsMap, noteChannel) {
  const data = {
    synthMap: [...synthMap.entries()],
    controlsMap: [...controlsMap.entries()].map(([k, v]) => [k, v.paramId]),
    noteChannel
  };
  localStorage.setItem(MIDI_BINDING_KEY, JSON.stringify(data));
}
