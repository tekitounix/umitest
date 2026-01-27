#!/usr/bin/env python3
"""
TB-303 WaveShaper: 全モデル比較テスト

C++実装の精度・パフォーマンスを評価し、グラフを生成

使用法:
    # C++モジュールをビルド
    cd docs/dsp/tb303/vco/code
    clang++ -std=c++17 -O3 -shared -fPIC -undefined dynamic_lookup \
        $(python3 -m pybind11 --includes) waveshaper_pybind.cpp \
        -o tb303_waveshaper$(python3-config --extension-suffix)

    # テスト実行
    cd docs/dsp/tb303/vco/test
    python3 compare_all_models.py              # 全比較 + グラフ出力
    python3 compare_all_models.py --no-plot    # グラフ表示なし（保存のみ）
"""

import numpy as np
import matplotlib.pyplot as plt
import time
import sys
import os
import argparse

# パス設定（codeディレクトリのモジュールを使用）
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'code'))

SAMPLE_RATE = 48000.0


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


def run_comparison(ws_module, output_dir):
    """精度比較 + ベンチマーク + 統合グラフ出力"""
    print("=" * 70)
    print("TB-303 WaveShaper: Model Comparison")
    print("=" * 70)

    if not ws_module:
        print("ERROR: C++ module not found. Build first.")
        return

    # パラメータ表示
    print(f"\nCircuit Parameters:")
    print(f"  V_T     = {ws_module.V_T}")
    print(f"  I_S     = {ws_module.I_S}")
    print(f"  BETA_F  = {ws_module.BETA_F}")
    print(f"  ALPHA_F = {ws_module.ALPHA_F:.6f}")

    # ========================================================================
    # 精度比較
    # ========================================================================
    test_freqs = [40.0, 110.0, 220.0, 440.0, 880.0]
    duration = 0.02  # 20ms per frequency

    # 入力信号生成
    input_segments = []
    for freq in test_freqs:
        input_segments.append(generate_sawtooth(freq, duration))
    input_signal = np.concatenate(input_segments)
    n_samples = len(input_signal)
    t_ms = np.arange(n_samples) / SAMPLE_RATE * 1000

    print(f"\nTest frequencies: {test_freqs} Hz")
    print(f"Total: {n_samples} samples ({len(test_freqs)*duration*1000:.0f}ms)")

    # リファレンス（C++ WaveShaperReference: 100回反復, std::exp）
    print("\nProcessing Reference (100 iterations)...")
    ref = ws_module.WaveShaperReference()
    ref.set_sample_rate(SAMPLE_RATE)
    ref.reset()
    output_ref = ref.process_array(input_signal.astype(np.float32))

    # テスト対象
    test_models = [
        ("Newton2", ws_module.WaveShaperNewton2),
        ("Newton3", ws_module.WaveShaperNewton3),
        ("Schur2", ws_module.WaveShaperSchur2),
        ("SchurUltra", ws_module.WaveShaperSchurUltra),
        ("LUT", ws_module.WaveShaperLUT),
        ("Pade22", ws_module.WaveShaperPade),
        ("Pade33", ws_module.WaveShaperPade33),
    ]

    results = {}
    print("\nProcessing test models...")
    for name, cls in test_models:
        model = cls()
        model.set_sample_rate(SAMPLE_RATE)
        model.reset()
        output = model.process_array(input_signal.astype(np.float32))

        # 誤差計算
        err = output - output_ref
        rms = np.sqrt(np.mean(err**2)) * 1000  # mV
        max_e = np.max(np.abs(err)) * 1000  # mV

        results[name] = {
            "output": output,
            "rms": rms,
            "max": max_e,
            "cls": cls,
        }

    # 誤差サマリー表示
    print("\n" + "=" * 70)
    print("Accuracy Summary (vs Reference)")
    print("=" * 70)
    print(f"{'Model':<15} {'RMS [mV]':<12} {'Max [mV]':<12}")
    print("-" * 39)
    for name, data in results.items():
        print(f"{name:<15} {data['rms']:<12.2f} {data['max']:<12.2f}")

    # ========================================================================
    # ベンチマーク
    # ========================================================================
    print("\n" + "=" * 70)
    print("Performance Benchmark (1 second of audio)")
    print("=" * 70)

    bench_signal = generate_sawtooth(440.0, 1.0).astype(np.float32)
    n_bench = len(bench_signal)

    print(f"\n{'Model':<15} {'Time [ms]':<12} {'Samples/s':<15} {'Realtime':<10}")
    print("-" * 52)

    bench_results = {}
    for name, data in results.items():
        model = data["cls"]()
        model.set_sample_rate(SAMPLE_RATE)
        model.reset()

        # Warmup
        _ = model.process_array(bench_signal[:1000])
        model.reset()

        # Benchmark (3回平均)
        times = []
        for _ in range(3):
            model.reset()
            start = time.perf_counter()
            _ = model.process_array(bench_signal)
            elapsed = time.perf_counter() - start
            times.append(elapsed)

        avg_time = np.mean(times)
        samples_per_sec = n_bench / avg_time
        realtime_ratio = samples_per_sec / SAMPLE_RATE

        bench_results[name] = {
            "time_ms": avg_time * 1000,
            "samples_per_sec": samples_per_sec,
            "realtime": realtime_ratio,
        }
        print(f"{name:<15} {avg_time*1000:<12.2f} {samples_per_sec:<15.0f} {realtime_ratio:<10.1f}x")

    # ========================================================================
    # 統合グラフ生成
    # ========================================================================
    print("\nGenerating plots...")

    fig = plt.figure(figsize=(18, 12))
    fig.suptitle('TB-303 WaveShaper: Model Comparison', fontsize=14, fontweight='bold')

    # 色の統一定義
    model_colors = {
        'Newton2': 'C0', 'Newton3': 'C1', 'Schur2': 'C2', 'SchurUltra': 'C3',
        'LUT': 'C4', 'Pade22': 'C5', 'Pade33': 'C6'
    }

    # 1. 波形比較（上段全体）- 全モデル表示
    ax1 = fig.add_subplot(3, 2, (1, 2))
    ax1.plot(t_ms, input_signal, 'k--', alpha=0.3, label='Input', linewidth=1)
    ax1.plot(t_ms, output_ref, 'b-', label='Reference (100 iter)', linewidth=2)

    for name, data in results.items():
        ax1.plot(t_ms, data["output"], color=model_colors[name],
                 label=f'{name} ({data["rms"]:.1f}mV)', linewidth=1, alpha=0.7)

    ax1.set_xlabel('Time [ms]')
    ax1.set_ylabel('Voltage [V]')
    ax1.set_title('Waveform Comparison (5 frequencies: 40, 110, 220, 440, 880 Hz)')
    ax1.legend(loc='upper right', fontsize=8, ncol=2)
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim([4, 10])

    # 2. 誤差時系列（左中）- 全モデル表示
    ax2 = fig.add_subplot(3, 2, 3)
    for name, data in results.items():
        err_mv = (data["output"] - output_ref) * 1000
        ax2.plot(t_ms, err_mv, label=name, linewidth=1, alpha=0.7, color=model_colors[name])

    ax2.set_xlabel('Time [ms]')
    ax2.set_ylabel('Error [mV]')
    ax2.set_title('Error vs Reference')
    ax2.legend(loc='upper right', fontsize=9)
    ax2.grid(True, alpha=0.3)

    # 3. 誤差バー + ベンチマーク（右中）
    ax3 = fig.add_subplot(3, 2, 4)

    names = list(results.keys())
    rms_vals = [results[n]["rms"] for n in names]
    realtime_vals = [bench_results[n]["realtime"] for n in names]

    x = np.arange(len(names))
    width = 0.35

    ax3_twin = ax3.twinx()
    bars1 = ax3.bar(x - width/2, rms_vals, width, label='RMS Error [mV]', color='steelblue')
    bars2 = ax3_twin.bar(x + width/2, realtime_vals, width, label='Realtime Ratio', color='coral')

    ax3.set_xticks(x)
    ax3.set_xticklabels(names, rotation=30, ha='right')
    ax3.set_ylabel('RMS Error [mV]', color='steelblue')
    ax3_twin.set_ylabel('Realtime Ratio [x]', color='coral')
    ax3.set_title('Error & Performance')
    ax3.tick_params(axis='y', labelcolor='steelblue')
    ax3_twin.tick_params(axis='y', labelcolor='coral')

    # 凡例を統合
    lines1, labels1 = ax3.get_legend_handles_labels()
    lines2, labels2 = ax3_twin.get_legend_handles_labels()
    ax3.legend(lines1 + lines2, labels1 + labels2, loc='upper right')
    ax3.grid(True, alpha=0.3, axis='y')

    # 4. 高周波ズーム（左下）- 440Hz - 全モデル表示
    ax4 = fig.add_subplot(3, 2, 5)
    seg_samples = int(duration * SAMPLE_RATE)
    start = 3 * seg_samples
    end = start + int(0.005 * SAMPLE_RATE)

    ax4.plot(t_ms[start:end], output_ref[start:end], 'b-', label='Reference', linewidth=2)
    for name, data in results.items():
        ax4.plot(t_ms[start:end], data["output"][start:end],
                 color=model_colors[name], label=name, linewidth=1, alpha=0.7)

    ax4.set_xlabel('Time [ms]')
    ax4.set_ylabel('Voltage [V]')
    ax4.set_title('Zoomed: 440Hz (High Frequency)')
    ax4.legend(loc='upper right', fontsize=8)
    ax4.grid(True, alpha=0.3)

    # 5. 低周波ズーム（右下）- 40Hz - 全モデル表示
    ax5 = fig.add_subplot(3, 2, 6)
    start = int(0.005 * SAMPLE_RATE)
    end = int(0.015 * SAMPLE_RATE)

    ax5.plot(t_ms[start:end], output_ref[start:end], 'b-', label='Reference', linewidth=2)
    for name, data in results.items():
        ax5.plot(t_ms[start:end], data["output"][start:end],
                 color=model_colors[name], label=name, linewidth=1, alpha=0.7)

    ax5.set_xlabel('Time [ms]')
    ax5.set_ylabel('Voltage [V]')
    ax5.set_title('Zoomed: 40Hz (Soft Knee Region)')
    ax5.legend(loc='upper right', fontsize=8)
    ax5.grid(True, alpha=0.3)

    plt.tight_layout()

    # 保存
    out_path = os.path.join(output_dir, 'waveshaper_comparison.png')
    plt.savefig(out_path, dpi=150)
    print(f"Saved: {out_path}")

    return results, bench_results


def main():
    parser = argparse.ArgumentParser(description="TB-303 WaveShaper comparison tool")
    parser.add_argument("--no-plot", action="store_true",
                        help="Skip plot display (save only)")
    parser.add_argument("--output", "-o", default=None,
                        help="Output directory for plots")
    args = parser.parse_args()

    output_dir = args.output or os.path.dirname(os.path.abspath(__file__))

    ws = try_import_cpp()
    if ws:
        print(f"C++ module loaded: tb303_waveshaper")
    else:
        print("ERROR: C++ module not found")
        print("\nBuild with:")
        print("  cd docs/dsp/tb303/vco/code")
        print("  clang++ -std=c++17 -O3 -shared -fPIC -undefined dynamic_lookup \\")
        print("      $(python3 -m pybind11 --includes) waveshaper_pybind.cpp \\")
        print("      -o tb303_waveshaper$(python3-config --extension-suffix)")
        sys.exit(1)

    run_comparison(ws, output_dir)

    if not args.no_plot:
        plt.show()


if __name__ == '__main__':
    main()
