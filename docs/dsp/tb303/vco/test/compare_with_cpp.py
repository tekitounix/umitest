#!/usr/bin/env python3
"""
C++ WaveShaper実装とPythonリファレンス実装の比較

C++モジュール(tb303_waveshaper)をインポートし、
Pythonリファレンス実装と比較してC++実装の精度を検証する。

使用法:
    # モジュールをビルド
    xmake waveshaper-py

    # テストを実行
    cd docs/dsp/tb303/vco/test
    python3 compare_with_cpp.py
"""

import numpy as np
import sys
import os

# パス設定（同ディレクトリのモジュールを優先）
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SAMPLE_RATE = 48000.0
DT = 1.0 / SAMPLE_RATE


# =============================================================================
# Pythonリファレンス実装
# =============================================================================
class WaveShaperReference:
    """
    Pythonリファレンス実装（100回反復Newton法）
    C++モジュールがない環境でも比較可能
    """
    # 回路定数
    V_CC = 12.0
    V_COLL = 5.33
    R2, R3, R4, R5 = 100e3, 10e3, 22e3, 10e3
    C1, C2 = 10e-9, 1e-6
    V_T = 0.025865
    I_S = 5e-14  # 2SA733P パラメータ (SPICEモデル中央値)
    BETA_F = 300.0  # Pランク中央値
    ALPHA_F = BETA_F / (BETA_F + 1.0)
    BETA_R = 0.1
    ALPHA_R = BETA_R / (BETA_R + 1.0)

    def __init__(self):
        self.reset()

    def reset(self):
        self.v_c1 = 0.0
        self.v_c2 = 8.0
        self.v_b = 8.0
        self.v_e = 8.0
        self.v_c = self.V_COLL

    def _diode_iv(self, v):
        """ダイオードI-V特性"""
        v_crit = self.V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / self.V_T)
            g = self.I_S / self.V_T * exp_crit
            i = self.I_S * (exp_crit - 1) + g * (v - v_crit)
        elif v < -10 * self.V_T:
            i = -self.I_S
            g = 1e-12
        else:
            exp_v = np.exp(v / self.V_T)
            i = self.I_S * (exp_v - 1)
            g = self.I_S / self.V_T * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        """1サンプル処理（100回反復）"""
        g2, g3, g4, g5 = 1/self.R2, 1/self.R3, 1/self.R4, 1/self.R5
        g_c1, g_c2 = self.C1/DT, self.C2/DT
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(100):
            i_ef, g_ef = self._diode_iv(v_e - v_b)
            i_cr, g_cr = self._diode_iv(v_c - v_b)

            i_e = i_ef - self.ALPHA_R * i_cr
            i_c = self.ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1*(v_in - v_cap - v_c1_prev) - g3*(v_cap - v_b)
            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(self.V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(self.V_COLL - v_c) + i_c

            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-12:
                break

            J = np.array([
                [-g_c1-g3, g3, 0, 0],
                [g3, -g2-g3-(1-self.ALPHA_F)*g_ef-(1-self.ALPHA_R)*g_cr,
                 (1-self.ALPHA_F)*g_ef, (1-self.ALPHA_R)*g_cr],
                [0, g_ef-self.ALPHA_R*g_cr, -g4-g_ef-g_c2, self.ALPHA_R*g_cr],
                [0, -self.ALPHA_F*g_ef+g_cr, self.ALPHA_F*g_ef, -g5-g_cr]
            ])
            b = np.array([-f1, -f2, -f3, -f4])

            try:
                dv = np.linalg.solve(J, b)
            except:
                break

            max_dv = np.max(np.abs(dv))
            damp = min(1.0, 0.5/max_dv) if max_dv > 0.5 else 1.0
            v_cap += damp*dv[0]
            v_b += damp*dv[1]
            v_e = np.clip(v_e + damp*dv[2], 0, self.V_CC+0.5)
            v_c = np.clip(v_c + damp*dv[3], 0, self.V_CC+0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b = v_b
        self.v_e = v_e
        self.v_c = v_c
        return v_c


def generate_sawtooth(freq, duration):
    """ノコギリ波生成（TB-303入力信号）"""
    n = int(SAMPLE_RATE * duration)
    t = np.arange(n) / SAMPLE_RATE
    phase = (freq * t) % 1.0
    return 12.0 - phase * 6.5  # 12V → 5.5V


def compare_implementations():
    """C++とPythonの実装を比較"""
    print("=" * 60)
    print("TB-303 WaveShaper: C++ vs Python Comparison")
    print("=" * 60)

    # C++モジュールをインポート
    try:
        import tb303_waveshaper as ws
        print(f"\nC++ module loaded: tb303_waveshaper")
        print(f"  V_T = {ws.V_T}")
        print(f"  I_S = {ws.I_S}")
        print(f"  BETA_F = {ws.BETA_F}")
        has_cpp = True
    except ImportError as e:
        print(f"\nWARNING: C++ module not found: {e}")
        print("Build with: xmake waveshaper-py")
        has_cpp = False

    # テスト信号生成
    freq = 440.0
    duration = 0.01  # 10ms
    input_signal = generate_sawtooth(freq, duration)
    n_samples = len(input_signal)

    print(f"\nTest signal: {freq}Hz sawtooth, {n_samples} samples")

    # Pythonリファレンス
    print("\n--- Python Reference (100 iterations) ---")
    ref = WaveShaperReference()
    ref.reset()
    output_ref = np.array([ref.process(x) for x in input_signal])

    results = []

    if has_cpp:
        # C++ WaveShaperSchur<2> (推奨)
        print("\n--- C++ WaveShaperSchur2 ---")
        shaper = ws.WaveShaperSchur2()
        shaper.set_sample_rate(SAMPLE_RATE)
        shaper.reset()
        output_cpp = shaper.process_array(input_signal.astype(np.float32))

        err = np.abs(output_cpp - output_ref)
        max_err = np.max(err)
        mean_err = np.mean(err)
        rms_err = np.sqrt(np.mean(err**2))

        print(f"  Max error:  {max_err:.6f} V ({max_err*1000:.3f} mV)")
        print(f"  Mean error: {mean_err:.6f} V ({mean_err*1000:.3f} mV)")
        print(f"  RMS error:  {rms_err:.6f} V ({rms_err*1000:.3f} mV)")
        results.append(("WaveShaperSchur2", max_err, mean_err, rms_err))

        # C++ WaveShaperSchur<1> (最速)
        print("\n--- C++ WaveShaperSchur1 ---")
        shaper = ws.WaveShaperSchur1()
        shaper.set_sample_rate(SAMPLE_RATE)
        shaper.reset()
        output_cpp = shaper.process_array(input_signal.astype(np.float32))

        err = np.abs(output_cpp - output_ref)
        max_err = np.max(err)
        mean_err = np.mean(err)
        rms_err = np.sqrt(np.mean(err**2))

        print(f"  Max error:  {max_err:.6f} V ({max_err*1000:.3f} mV)")
        print(f"  Mean error: {mean_err:.6f} V ({mean_err*1000:.3f} mV)")
        print(f"  RMS error:  {rms_err:.6f} V ({rms_err*1000:.3f} mV)")
        results.append(("WaveShaperSchur1", max_err, mean_err, rms_err))

        # C++ WaveShaperNewton<3>
        print("\n--- C++ WaveShaperNewton3 ---")
        shaper = ws.WaveShaperNewton3()
        shaper.set_sample_rate(SAMPLE_RATE)
        shaper.reset()
        output_cpp = shaper.process_array(input_signal.astype(np.float32))

        err = np.abs(output_cpp - output_ref)
        max_err = np.max(err)
        mean_err = np.mean(err)
        rms_err = np.sqrt(np.mean(err**2))

        print(f"  Max error:  {max_err:.6f} V ({max_err*1000:.3f} mV)")
        print(f"  Mean error: {mean_err:.6f} V ({mean_err*1000:.3f} mV)")
        print(f"  RMS error:  {rms_err:.6f} V ({rms_err*1000:.3f} mV)")
        results.append(("WaveShaperNewton3", max_err, mean_err, rms_err))

        # C++ WaveShaperLUT
        print("\n--- C++ WaveShaperLUT ---")
        shaper = ws.WaveShaperLUT()
        shaper.set_sample_rate(SAMPLE_RATE)
        shaper.reset()
        output_cpp = shaper.process_array(input_signal.astype(np.float32))

        err = np.abs(output_cpp - output_ref)
        max_err = np.max(err)
        mean_err = np.mean(err)
        rms_err = np.sqrt(np.mean(err**2))

        print(f"  Max error:  {max_err:.6f} V ({max_err*1000:.3f} mV)")
        print(f"  Mean error: {mean_err:.6f} V ({mean_err*1000:.3f} mV)")
        print(f"  RMS error:  {rms_err:.6f} V ({rms_err*1000:.3f} mV)")
        results.append(("WaveShaperLUT", max_err, mean_err, rms_err))

    # 結果サマリー
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    if results:
        print(f"{'Implementation':<22} {'Max(mV)':<10} {'Mean(mV)':<10} {'RMS(mV)':<10}")
        print("-" * 52)
        for name, max_e, mean_e, rms_e in results:
            print(f"{name:<22} {max_e*1000:<10.3f} {mean_e*1000:<10.3f} {rms_e*1000:<10.3f}")
    else:
        print("No C++ implementations tested.")
        print("Build the module with: xmake waveshaper-py")


def benchmark_cpp():
    """C++実装のベンチマーク"""
    try:
        import tb303_waveshaper as ws
    except ImportError:
        print("C++ module not available. Build with: xmake waveshaper-py")
        return

    import time

    print("=" * 60)
    print("TB-303 WaveShaper: C++ Performance Benchmark")
    print("=" * 60)

    # 1秒分の信号
    duration = 1.0
    input_signal = generate_sawtooth(440.0, duration).astype(np.float32)
    n_samples = len(input_signal)

    implementations = [
        ("WaveShaperSchur1", ws.WaveShaperSchur1),
        ("WaveShaperSchur2", ws.WaveShaperSchur2),
        ("WaveShaperNewton1", ws.WaveShaperNewton1),
        ("WaveShaperNewton2", ws.WaveShaperNewton2),
        ("WaveShaperNewton3", ws.WaveShaperNewton3),
        ("WaveShaperLUT", ws.WaveShaperLUT),
        ("WaveShaperPade", ws.WaveShaperPade),
    ]

    print(f"\nProcessing {n_samples} samples ({duration}s at {SAMPLE_RATE}Hz)")
    print(f"{'Implementation':<22} {'Time(ms)':<12} {'Samples/sec':<15} {'Realtime ratio':<15}")
    print("-" * 64)

    for name, cls in implementations:
        shaper = cls()
        shaper.set_sample_rate(SAMPLE_RATE)
        shaper.reset()

        # Warmup
        _ = shaper.process_array(input_signal[:1000])
        shaper.reset()

        # Benchmark
        start = time.perf_counter()
        _ = shaper.process_array(input_signal)
        elapsed = time.perf_counter() - start

        samples_per_sec = n_samples / elapsed
        realtime_ratio = samples_per_sec / SAMPLE_RATE

        print(f"{name:<22} {elapsed*1000:<12.2f} {samples_per_sec:<15.0f} {realtime_ratio:<15.1f}x")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="TB-303 WaveShaper comparison tool")
    parser.add_argument("--benchmark", "-b", action="store_true",
                        help="Run performance benchmark")
    args = parser.parse_args()

    if args.benchmark:
        benchmark_cpp()
    else:
        compare_implementations()
