/**
 * Audio Engine - AudioWorklet-based filter processing
 * Provides synth functionality with biquad lowpass filter
 */

import { $ } from '../lib/utils.js';

/**
 * @typedef {Object} Engine
 * @property {AudioContext} ctx - Audio context
 * @property {AnalyserNode} analyser - Analyser node for visualization
 * @property {() => Promise<void>} start - Start the engine
 * @property {() => void} stop - Stop the engine
 * @property {(freq: number) => void} noteOn - Play a note
 * @property {() => void} noteOff - Stop current note
 * @property {(note: number) => number} midiToFreq - Convert MIDI note to frequency
 * @property {(hz: number) => void} setCutoff - Set filter cutoff frequency
 * @property {(q: number) => void} setResonance - Set filter resonance
 * @property {(g: number) => void} setPreGain - Set pre-filter gain
 * @property {(g: number) => void} setOutGain - Set output gain
 * @property {(on: boolean) => void} setBypass - Enable/disable filter bypass
 */

/**
 * Create audio engine
 * @param {Object} [options]
 * @param {(text: string, ok?: boolean) => void} [options.onStatusChange] - Status change callback
 * @param {string} [options.workletPath] - Path to worklet processor file
 * @returns {Promise<Engine>}
 */
export async function createEngine({ onStatusChange, workletPath } = {}) {
  const setStatus = onStatusChange || (() => {});

  const Ctx = window.AudioContext || window.webkitAudioContext;
  const ctx = new Ctx({ latencyHint: 'interactive' });

  // GC防止: windowオブジェクトに参照を保持
  // ブラウザによっては参照がなくなるとAudioContextがGCされることがある
  if (!window.__audioContexts) window.__audioContexts = [];
  window.__audioContexts.push(ctx);

  // state変更の監視
  // wasRunning: start()で明示的に開始された場合のみtrue
  // wasSuspendedBySystem: システム（ブラウザ/OS）による中断かどうか
  let wasRunning = false;
  let wasSuspendedBySystem = false;

  ctx.onstatechange = () => {
    if (ctx.state === 'interrupted') {
      // iOS/Safariで電話着信時などに発生（システムによる中断）
      wasSuspendedBySystem = true;
    } else if (ctx.state === 'running' && wasSuspendedBySystem) {
      // システム中断からの復帰完了
      wasSuspendedBySystem = false;
    }
  };

  // バックグラウンドタブからの復帰時
  // 注意: 自動resumeはシステム中断からの復帰時のみ
  const handleVisibilityChange = () => {
    if (document.visibilityState === 'visible' &&
        wasRunning &&
        wasSuspendedBySystem &&
        ctx.state === 'suspended') {
      ctx.resume().catch(() => {});
    }
  };
  document.addEventListener('visibilitychange', handleVisibilityChange);

  const inputGain = ctx.createGain();
  inputGain.gain.value = 1.0;

  const outputGain = ctx.createGain();
  outputGain.gain.value = 1.0;

  const analyser = ctx.createAnalyser();
  analyser.fftSize = 2048;

  // AudioWorkletが使えるかチェック
  let useWorklet = false;
  let workletNode = null;
  let biquadFilter = null;

  try {
    if (ctx.audioWorklet) {
      // Try external file first, then inline script
      if (workletPath) {
        await ctx.audioWorklet.addModule(workletPath);
      } else {
        // Fallback to inline script if available
        const workletScript = $('#worklet-processor');
        if (workletScript) {
          const workletCode = workletScript.textContent;
          const blob = new Blob([workletCode], { type: 'application/javascript' });
          const url = URL.createObjectURL(blob);
          await ctx.audioWorklet.addModule(url);
          URL.revokeObjectURL(url);
        } else {
          throw new Error('No worklet source available');
        }
      }

      workletNode = new AudioWorkletNode(ctx, 'filter-processor', {
        numberOfInputs: 1,
        numberOfOutputs: 1,
        outputChannelCount: [2],
      });
      useWorklet = true;
    }
  } catch (e) {
    // AudioWorklet not available, will use fallback
    console.warn('AudioWorklet not available, using BiquadFilter fallback:', e);
  }

  // フォールバック: BiquadFilterNode
  if (!useWorklet) {
    biquadFilter = ctx.createBiquadFilter();
    biquadFilter.type = 'lowpass';
    biquadFilter.frequency.value = 1200;
    biquadFilter.Q.value = 0.7;
  }

  // 接続
  if (useWorklet) {
    inputGain.connect(workletNode);
    workletNode.connect(outputGain);
  } else {
    inputGain.connect(biquadFilter);
    biquadFilter.connect(outputGain);
  }
  outputGain.connect(analyser);
  analyser.connect(ctx.destination);

  // シンセ用オシレーター（noteOn/noteOffで制御）
  let osc = null;
  let oscGain = null;

  function noteOn(freq) {
    if (osc) noteOff();
    osc = ctx.createOscillator();
    oscGain = ctx.createGain();
    osc.type = 'sawtooth';
    osc.frequency.value = freq;
    oscGain.gain.value = 0;
    osc.connect(oscGain);
    oscGain.connect(inputGain);
    osc.start();
    // Attack
    oscGain.gain.setTargetAtTime(1.0, ctx.currentTime, 0.01);
  }

  function noteOff() {
    if (!osc) return;
    const g = oscGain;
    const o = osc;
    // Release
    g.gain.setTargetAtTime(0, ctx.currentTime, 0.05);
    setTimeout(() => {
      try { o.stop(); } catch {}
      try { o.disconnect(); } catch {}
      try { g.disconnect(); } catch {}
    }, 200);
    osc = null;
    oscGain = null;
  }

  // MIDIノート番号から周波数へ変換
  function midiToFreq(note) {
    return 440 * Math.pow(2, (note - 69) / 12);
  }

  async function start() {
    await ctx.resume();
    wasRunning = true;
    setStatus(`Synth (${useWorklet ? 'Worklet' : 'Biquad'})`, true);
  }

  function stop() {
    noteOff();
    wasRunning = false;
    ctx.suspend().catch(() => {});
    setStatus('Stopped', false);
  }

  function destroy() {
    noteOff();
    ctx.close().catch(() => {});
    // イベントリスナー解除
    document.removeEventListener('visibilitychange', handleVisibilityChange);
    // GC防止用配列から削除
    if (window.__audioContexts) {
      const idx = window.__audioContexts.indexOf(ctx);
      if (idx !== -1) window.__audioContexts.splice(idx, 1);
    }
    setStatus('Destroyed', false);
  }

  // Parameter setters (AudioWorklet or BiquadFilter)
  function setCutoff(hz) {
    if (useWorklet && workletNode) {
      workletNode.parameters.get('cutoff').setTargetAtTime(hz, ctx.currentTime, 0.015);
    } else if (biquadFilter) {
      biquadFilter.frequency.setTargetAtTime(hz, ctx.currentTime, 0.015);
    }
  }
  function setResonance(q) {
    if (useWorklet && workletNode) {
      workletNode.parameters.get('resonance').setTargetAtTime(q, ctx.currentTime, 0.015);
    } else if (biquadFilter) {
      biquadFilter.Q.setTargetAtTime(q, ctx.currentTime, 0.015);
    }
  }
  function setPreGain(g) {
    if (useWorklet && workletNode) {
      workletNode.parameters.get('preGain').setTargetAtTime(g, ctx.currentTime, 0.015);
    } else {
      inputGain.gain.setTargetAtTime(g, ctx.currentTime, 0.015);
    }
  }
  function setOutGain(g) {
    if (useWorklet && workletNode) {
      workletNode.parameters.get('outGain').setTargetAtTime(g, ctx.currentTime, 0.015);
    } else {
      outputGain.gain.setTargetAtTime(g, ctx.currentTime, 0.015);
    }
  }
  function setBypass(on) {
    if (useWorklet && workletNode) {
      workletNode.port.postMessage({ type: 'bypass', value: on });
    }
    // BiquadFilter fallbackではbypassは未対応
  }

  return {
    ctx,
    analyser,
    start,
    stop,
    destroy,
    noteOn,
    noteOff,
    midiToFreq,
    setCutoff,
    setResonance,
    setPreGain,
    setOutGain,
    setBypass,
  };
}
