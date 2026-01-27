#!/usr/bin/env python3
"""
TB-303 WaveShaper: 全モデル比較 (pybind11 C++モジュール使用)

C++実装とPythonリファレンスを比較し、精度・パフォーマンスを評価

使用法:
    # C++モジュールをビルド
    xmake waveshaper-py

    # テスト実行
    cd docs/dsp/tb303/vco/test
    python3 compare_all_models.py           # 全比較 + グラフ出力
    python3 compare_all_models.py --bench   # ベンチマークのみ
"""

import numpy as np
import matplotlib.pyplot as plt
import time
import sys
import os
import argparse

# パス設定（同ディレクトリのモジュールを優先）
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SAMPLE_RATE = 48000.0
DT = 1.0 / SAMPLE_RATE

# =============================================================================
# Pythonリファレンス実装
# =============================================================================
class WaveShaperReference:
    """Pythonリファレンス（100回Newton反復）- 精度基準"""
    # 2SA733Pパラメータ (C++実装と統一)
    V_CC = 12.0
    V_COLL = 5.33
    R2, R3, R4, R5 = 100e3, 10e3, 22e3, 10e3
    C1, C2 = 10e-9, 1e-6
    V_T = 0.025865
    V_T_INV = 1.0 / V_T
    I_S = 5e-14  # SPICEモデル中央値
    BETA_F = 300.0  # Pランク中央値
    ALPHA_F = BETA_F / (BETA_F + 1.0)
    BETA_R = 0.1
    ALPHA_R = BETA_R / (BETA_R + 1.0)

    G2 = 1/R2
    G3 = 1/R3
    G4 = 1/R4
    G5 = 1/R5

    def __init__(self):
        self.g_c1 = self.C1 / DT
        self.g_c2 = self.C2 / DT
        self.reset()

    def reset(self):
        self.v_c1 = 0.0
        self.v_c2 = 8.0
        self.v_b = 8.0
        self.v_e = 8.0
        self.v_c = self.V_COLL

    def _diode_iv(self, v):
        v_crit = self.V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / self.V_T)
            g = self.I_S * self.V_T_INV * exp_crit
            i = self.I_S * (exp_crit - 1) + g * (v - v_crit)
        elif v < -10 * self.V_T:
            i, g = -self.I_S, 1e-12
        else:
            exp_v = np.exp(v / self.V_T)
            i = self.I_S * (exp_v - 1)
            g = self.I_S * self.V_T_INV * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(100):
            i_ef, g_ef = self._diode_iv(v_e - v_b)
            i_cr, g_cr = self._diode_iv(v_c - v_b)

            i_e = i_ef - self.ALPHA_R * i_cr
            i_c = self.ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = self.g_c1*(v_in - v_cap - v_c1_prev) - self.G3*(v_cap - v_b)
            f2 = self.G2*(v_in - v_b) + self.G3*(v_cap - v_b) + i_b
            f3 = self.G4*(self.V_CC - v_e) - i_e - self.g_c2*(v_e - v_c2_prev)
            f4 = self.G5*(self.V_COLL - v_c) + i_c

            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-12:
                break

            J = np.array([
                [-self.g_c1-self.G3, self.G3, 0, 0],
                [self.G3, -self.G2-self.G3-(1-self.ALPHA_F)*g_ef-(1-self.ALPHA_R)*g_cr,
                 (1-self.ALPHA_F)*g_ef, (1-self.ALPHA_R)*g_cr],
                [0, g_ef-self.ALPHA_R*g_cr, -self.G4-g_ef-self.g_c2, self.ALPHA_R*g_cr],
                [0, -self.ALPHA_F*g_ef+g_cr, self.ALPHA_F*g_ef, -self.G5-g_cr]
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
    """TB-303風ノコギリ波生成 (12V→5.5V)"""
    n = int(SAMPLE_RATE * duration)
    t = np.arange(n) / SAMPLE_RATE
    phase = (freq * t) % 1.0
    return 12.0 - phase * 6.5


def try_import_cpp():
    """C++モジュールをインポート試行"""
    try:
        import tb303_waveshaper as ws
        return ws
    except ImportError:
        return None


def compare_accuracy(ws_module, output_dir):
    """精度比較 + グラフ出力"""
    print("=" * 70)
    print("TB-303 WaveShaper: Accuracy Comparison")
    print("=" * 70)

    # テスト信号
    freq = 40.0
    duration = 0.05  # 50ms
    input_signal = generate_sawtooth(freq, duration)
    n_samples = len(input_signal)
    t_ms = np.arange(n_samples) / SAMPLE_RATE * 1000

    print(f"\nTest: {freq}Hz sawtooth, {n_samples} samples ({duration*1000:.0f}ms)")

    # Pythonリファレンス
    print("\nProcessing Python Reference (100 iter)...")
    ref = WaveShaperReference()
    ref.reset()
    output_ref = np.array([ref.process(x) for x in input_signal])

    # C++実装の結果を格納
    results = {}

    if ws_module:
        # エクスポートされたパラメータを表示
        print(f"\nC++ Module Parameters:")
        print(f"  V_T     = {ws_module.V_T}")
        print(f"  I_S     = {ws_module.I_S}")
        print(f"  BETA_F  = {ws_module.BETA_F}")
        print(f"  ALPHA_F = {ws_module.ALPHA_F:.6f}")

        # テスト対象のC++実装
        cpp_classes = [
            ("WaveShaperSchur1", ws_module.WaveShaperSchur1, "C++ Schur 1iter"),
            ("WaveShaperSchur2", ws_module.WaveShaperSchur2, "C++ Schur 2iter"),
            ("WaveShaperNewton1", ws_module.WaveShaperNewton1, "C++ Newton 1iter"),
            ("WaveShaperNewton2", ws_module.WaveShaperNewton2, "C++ Newton 2iter"),
            ("WaveShaperNewton3", ws_module.WaveShaperNewton3, "C++ Newton 3iter"),
            ("WaveShaper3Var1", ws_module.WaveShaper3Var1, "C++ 3Var 1iter"),
            ("WaveShaper3Var2", ws_module.WaveShaper3Var2, "C++ 3Var 2iter"),
            ("WaveShaperSchurUltra", ws_module.WaveShaperSchurUltra, "C++ SchurUltra"),
            ("WaveShaperLUT", ws_module.WaveShaperLUT, "C++ LUT"),
            ("WaveShaperPade", ws_module.WaveShaperPade, "C++ Pade"),
        ]

        print("\nProcessing C++ implementations...")
        for name, cls, label in cpp_classes:
            shaper = cls()
            shaper.set_sample_rate(SAMPLE_RATE)
            shaper.reset()
            output = shaper.process_array(input_signal.astype(np.float32))
            results[name] = {
                "output": output,
                "label": label,
            }

    # 誤差計算
    def calc_error(output, ref):
        err = output - ref
        rms = np.sqrt(np.mean(err**2)) * 1000  # mV
        max_e = np.max(np.abs(err)) * 1000  # mV
        mean_e = np.mean(np.abs(err)) * 1000  # mV
        return rms, max_e, mean_e

    # 結果表示
    print("\n" + "=" * 70)
    print("Error Summary (vs Python Reference)")
    print("=" * 70)
    print(f"{'Implementation':<24} {'RMS(mV)':<12} {'Max(mV)':<12} {'Mean(mV)':<12}")
    print("-" * 60)

    error_data = []
    for name, data in results.items():
        rms, max_e, mean_e = calc_error(data["output"], output_ref)
        data["rms"] = rms
        data["max"] = max_e
        data["mean"] = mean_e
        error_data.append((name, data["label"], rms, max_e, mean_e))
        print(f"{data['label']:<24} {rms:<12.4f} {max_e:<12.4f} {mean_e:<12.4f}")

    # グラフ生成
    print("\nGenerating plots...")

    fig = plt.figure(figsize=(16, 14))

    # 1. 波形比較 (上部)
    ax1 = fig.add_subplot(3, 2, (1, 2))
    ax1.plot(t_ms, input_signal, 'k--', alpha=0.3, label='Input', linewidth=1)
    ax1.plot(t_ms, output_ref, 'b-', label='Python Ref (100 iter)', linewidth=2)

    # 代表的なC++実装をプロット
    if "WaveShaperSchur2" in results:
        ax1.plot(t_ms, results["WaveShaperSchur2"]["output"], 'g--',
                 label=f'C++ Schur2 ({results["WaveShaperSchur2"]["rms"]:.2f}mV)', linewidth=1.5, alpha=0.8)
    if "WaveShaperNewton3" in results:
        ax1.plot(t_ms, results["WaveShaperNewton3"]["output"], 'r--',
                 label=f'C++ Newton3 ({results["WaveShaperNewton3"]["rms"]:.2f}mV)', linewidth=1.5, alpha=0.8)
    if "WaveShaper3Var2" in results:
        ax1.plot(t_ms, results["WaveShaper3Var2"]["output"], 'm--',
                 label=f'C++ 3Var2 ({results["WaveShaper3Var2"]["rms"]:.2f}mV)', linewidth=1.5, alpha=0.8)

    ax1.set_xlabel('Time [ms]')
    ax1.set_ylabel('Voltage [V]')
    ax1.set_title('TB-303 WaveShaper: Waveform Comparison')
    ax1.legend(loc='upper right')
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim([4, 10])

    # 2. 誤差時系列 (左中)
    ax2 = fig.add_subplot(3, 2, 3)
    colors = plt.cm.tab10(np.linspace(0, 1, len(results)))
    for (name, data), color in zip(results.items(), colors):
        err_mv = (data["output"] - output_ref) * 1000
        ax2.plot(t_ms, err_mv, label=data["label"], linewidth=1, alpha=0.7, color=color)

    ax2.set_xlabel('Time [ms]')
    ax2.set_ylabel('Error [mV]')
    ax2.set_title('Error vs Python Reference')
    ax2.legend(loc='upper right', fontsize=8, ncol=2)
    ax2.grid(True, alpha=0.3)

    # 3. 誤差バー (右中)
    ax3 = fig.add_subplot(3, 2, 4)
    names = [d[1] for d in error_data]
    rms_vals = [d[2] for d in error_data]
    max_vals = [d[3] for d in error_data]

    x = np.arange(len(names))
    width = 0.35
    ax3.bar(x - width/2, rms_vals, width, label='RMS', color='steelblue')
    ax3.bar(x + width/2, max_vals, width, label='Max', color='coral')
    ax3.set_xticks(x)
    ax3.set_xticklabels([n.replace("C++ ", "") for n in names], rotation=45, ha='right', fontsize=8)
    ax3.set_ylabel('Error [mV]')
    ax3.set_title('Error Comparison')
    ax3.legend()
    ax3.grid(True, alpha=0.3, axis='y')

    # 4. ズーム波形 (左下)
    ax4 = fig.add_subplot(3, 2, 5)
    start = int(0.01 * SAMPLE_RATE)
    end = int(0.035 * SAMPLE_RATE)
    ax4.plot(t_ms[start:end], output_ref[start:end], 'b-', label='Python Ref', linewidth=2)
    if "WaveShaperSchur2" in results:
        ax4.plot(t_ms[start:end], results["WaveShaperSchur2"]["output"][start:end],
                 'g--', label='C++ Schur2', linewidth=1.5)
    if "WaveShaper3Var2" in results:
        ax4.plot(t_ms[start:end], results["WaveShaper3Var2"]["output"][start:end],
                 'm--', label='C++ 3Var2', linewidth=1.5)
    ax4.set_xlabel('Time [ms]')
    ax4.set_ylabel('Voltage [V]')
    ax4.set_title('Zoomed: 1 Cycle')
    ax4.legend(loc='upper right')
    ax4.grid(True, alpha=0.3)

    # 5. 遷移部分のズーム (右下)
    ax5 = fig.add_subplot(3, 2, 6)
    # Soft knee部分を探す
    transition_start = int(0.018 * SAMPLE_RATE)
    transition_end = int(0.022 * SAMPLE_RATE)
    ax5.plot(t_ms[transition_start:transition_end], output_ref[transition_start:transition_end],
             'b-', label='Python Ref', linewidth=2)
    if "WaveShaperSchur2" in results:
        ax5.plot(t_ms[transition_start:transition_end],
                 results["WaveShaperSchur2"]["output"][transition_start:transition_end],
                 'g--', label='C++ Schur2', linewidth=1.5)
    if "WaveShaperLUT" in results:
        ax5.plot(t_ms[transition_start:transition_end],
                 results["WaveShaperLUT"]["output"][transition_start:transition_end],
                 'c--', label='C++ LUT', linewidth=1.5)
    ax5.set_xlabel('Time [ms]')
    ax5.set_ylabel('Voltage [V]')
    ax5.set_title('Zoomed: Soft Knee Transition')
    ax5.legend(loc='upper right')
    ax5.grid(True, alpha=0.3)

    plt.tight_layout()

    # 保存
    out_path = os.path.join(output_dir, 'waveshaper_comparison.png')
    plt.savefig(out_path, dpi=150)
    print(f"Saved: {out_path}")

    return results


def benchmark(ws_module, output_dir):
    """パフォーマンスベンチマーク + グラフ出力"""
    print("\n" + "=" * 70)
    print("TB-303 WaveShaper: Performance Benchmark")
    print("=" * 70)

    if not ws_module:
        print("C++ module not available. Build with: xmake waveshaper-py")
        return

    # 1秒分の信号
    duration = 1.0
    input_signal = generate_sawtooth(440.0, duration).astype(np.float32)
    n_samples = len(input_signal)

    implementations = [
        ("WaveShaperSchur1", ws_module.WaveShaperSchur1),
        ("WaveShaperSchur2", ws_module.WaveShaperSchur2),
        ("WaveShaperNewton1", ws_module.WaveShaperNewton1),
        ("WaveShaperNewton2", ws_module.WaveShaperNewton2),
        ("WaveShaperNewton3", ws_module.WaveShaperNewton3),
        ("WaveShaper3Var1", ws_module.WaveShaper3Var1),
        ("WaveShaper3Var2", ws_module.WaveShaper3Var2),
        ("WaveShaper3Var3", ws_module.WaveShaper3Var3),
        ("WaveShaperSchurUltra", ws_module.WaveShaperSchurUltra),
        ("WaveShaperLUT", ws_module.WaveShaperLUT),
        ("WaveShaperPade", ws_module.WaveShaperPade),
        ("WaveShaperPade33", ws_module.WaveShaperPade33),
    ]

    print(f"\nProcessing {n_samples} samples ({duration}s at {SAMPLE_RATE}Hz)")
    print(f"{'Implementation':<24} {'Time(ms)':<12} {'Samples/sec':<15} {'Realtime':<12}")
    print("-" * 63)

    bench_results = []
    for name, cls in implementations:
        shaper = cls()
        shaper.set_sample_rate(SAMPLE_RATE)
        shaper.reset()

        # Warmup
        _ = shaper.process_array(input_signal[:1000])
        shaper.reset()

        # Benchmark (3回平均)
        times = []
        for _ in range(3):
            shaper.reset()
            start = time.perf_counter()
            _ = shaper.process_array(input_signal)
            elapsed = time.perf_counter() - start
            times.append(elapsed)

        avg_time = np.mean(times)
        samples_per_sec = n_samples / avg_time
        realtime_ratio = samples_per_sec / SAMPLE_RATE

        print(f"{name:<24} {avg_time*1000:<12.2f} {samples_per_sec:<15.0f} {realtime_ratio:<12.1f}x")
        bench_results.append((name, avg_time * 1000, samples_per_sec, realtime_ratio))

    # ベンチマーク棒グラフ
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    names = [r[0] for r in bench_results]
    times_ms = [r[1] for r in bench_results]
    realtime_ratios = [r[3] for r in bench_results]

    # 処理時間
    colors = plt.cm.viridis(np.linspace(0.2, 0.8, len(names)))
    bars1 = ax1.barh(names, times_ms, color=colors)
    ax1.set_xlabel('Processing Time [ms]')
    ax1.set_title('Processing Time (1 second of audio)')
    ax1.invert_yaxis()
    ax1.grid(True, alpha=0.3, axis='x')

    # リアルタイム比
    bars2 = ax2.barh(names, realtime_ratios, color=colors)
    ax2.axvline(x=1.0, color='r', linestyle='--', label='Realtime threshold')
    ax2.set_xlabel('Realtime Ratio (x faster than realtime)')
    ax2.set_title('Realtime Performance Ratio')
    ax2.invert_yaxis()
    ax2.grid(True, alpha=0.3, axis='x')
    ax2.legend()

    plt.tight_layout()

    out_path = os.path.join(output_dir, 'waveshaper_benchmark.png')
    plt.savefig(out_path, dpi=150)
    print(f"\nSaved: {out_path}")

    return bench_results


def main():
    parser = argparse.ArgumentParser(description="TB-303 WaveShaper comparison tool")
    parser.add_argument("--bench", "-b", action="store_true",
                        help="Run benchmark only")
    parser.add_argument("--no-plot", action="store_true",
                        help="Skip plot display (save only)")
    parser.add_argument("--output", "-o", default=None,
                        help="Output directory for plots")
    args = parser.parse_args()

    # 出力ディレクトリ
    output_dir = args.output or os.path.dirname(os.path.abspath(__file__))

    # C++モジュールをインポート
    ws = try_import_cpp()
    if ws:
        print(f"C++ module loaded: tb303_waveshaper")
    else:
        print("WARNING: C++ module not found")
        print("Build with: xmake waveshaper-py")
        print("Continuing with Python-only comparison...\n")

    if args.bench:
        benchmark(ws, output_dir)
    else:
        compare_accuracy(ws, output_dir)
        benchmark(ws, output_dir)

    if not args.no_plot:
        plt.show()


if __name__ == '__main__':
    main()
