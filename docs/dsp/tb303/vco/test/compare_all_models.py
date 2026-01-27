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


def generate_sawtooth(freq, duration, ws_module=None):
    """TB-303風ノコギリ波生成 (12V→5.5V)

    ws_module が指定されていれば C++ PolyBLEP オシレータを使用
    """
    n = int(SAMPLE_RATE * duration)

    if ws_module is not None and hasattr(ws_module, 'SawOscillator'):
        osc = ws_module.SawOscillator()
        osc.set_sample_rate(SAMPLE_RATE)
        osc.set_frequency(freq)
        osc.reset()
        return osc.process_array(n)
    else:
        # フォールバック: Python実装（Naive）
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


def run_oversampling_test(ws_module, output_dir):
    """オーバーサンプリングの必要性を検証

    WaveShaperは非線形処理なので、新たな高調波が生成される。
    オーバーサンプリングなしで折り返しエイリアシングが発生するか確認。
    """
    from scipy import signal

    print("\n" + "=" * 70)
    print("Oversampling Test: 1x vs 2x vs 4x")
    print("=" * 70)

    test_freq = 880.0  # 高周波でエイリアシングが顕著
    duration = 0.1
    base_sr = SAMPLE_RATE

    # リファレンス: 8xオーバーサンプリング
    ref_os = 8
    ref_sr = base_sr * ref_os
    n_ref = int(ref_sr * duration)

    osc = ws_module.SawOscillator()
    osc.set_sample_rate(ref_sr)
    osc.set_frequency(test_freq)
    osc.reset()
    input_ref = osc.process_array(n_ref)

    ws_ref = ws_module.WaveShaperTurbo()
    ws_ref.set_sample_rate(ref_sr)
    ws_ref.reset()
    output_ref_os = ws_ref.process_array(input_ref.astype(np.float32))

    # ダウンサンプル（LPF + デシメーション）
    output_ref = signal.decimate(output_ref_os, ref_os, ftype='fir')

    results = {}
    for os_factor in [1, 2, 4]:
        sr = base_sr * os_factor
        n = int(sr * duration)

        osc = ws_module.SawOscillator()
        osc.set_sample_rate(sr)
        osc.set_frequency(test_freq)
        osc.reset()
        input_os = osc.process_array(n)

        ws = ws_module.WaveShaperTurbo()
        ws.set_sample_rate(sr)
        ws.reset()
        output_os = ws.process_array(input_os.astype(np.float32))

        if os_factor > 1:
            output = signal.decimate(output_os, os_factor, ftype='fir')
        else:
            output = output_os

        # リファレンスとの長さを揃える
        min_len = min(len(output), len(output_ref))
        output = output[:min_len]
        ref = output_ref[:min_len]

        err = output - ref
        rms = np.sqrt(np.mean(err**2)) * 1000
        ref_power = np.sum(ref**2)
        err_power = np.sum(err**2)
        sinad = 10 * np.log10(ref_power / (err_power + 1e-20))

        results[os_factor] = {
            "output": output,
            "rms": rms,
            "sinad": sinad,
        }

    print(f"\n{'OS Factor':<12} {'RMS [mV]':<12} {'SINAD [dB]':<12}")
    print("-" * 36)
    for os_factor, data in results.items():
        print(f"{os_factor}x{'':<10} {data['rms']:<12.2f} {data['sinad']:<12.2f}")

    # SINADの改善度
    sinad_1x = results[1]["sinad"]
    sinad_2x = results[2]["sinad"]
    sinad_4x = results[4]["sinad"]
    print(f"\n2x vs 1x improvement: {sinad_2x - sinad_1x:.2f} dB")
    print(f"4x vs 1x improvement: {sinad_4x - sinad_1x:.2f} dB")

    return results


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
    test_freqs = [22.5, 55.0, 110.0, 220.0, 440.0, 880.0]
    min_cycles = 1  # 各周波数で最低1周期

    # 入力信号生成（C++ PolyBLEPオシレータ使用）
    # 単一オシレータで周波数を切り替えながら連続生成（セグメント境界でのジャンプを防ぐ）
    osc = ws_module.SawOscillator()
    osc.set_sample_rate(SAMPLE_RATE)
    osc.reset()

    input_segments = []
    segment_lengths = []  # 各セグメントのサンプル数を記録
    segment_cycles = []   # 各セグメントの周期数を記録
    for freq in test_freqs:
        osc.set_frequency(freq)
        period_samples = SAMPLE_RATE / freq  # 1周期のサンプル数
        n_cycles = min_cycles
        n_samples_seg = int(round(period_samples * n_cycles))  # ちょうど整数周期
        seg = osc.process_array(n_samples_seg)
        input_segments.append(seg)
        segment_lengths.append(len(seg))
        segment_cycles.append(n_cycles)
    input_signal = np.concatenate(input_segments)
    n_samples = len(input_signal)
    t_ms = np.arange(n_samples) / SAMPLE_RATE * 1000

    # 各セグメントの開始インデックスを計算
    segment_starts = [0]
    for length in segment_lengths[:-1]:
        segment_starts.append(segment_starts[-1] + length)

    total_duration_ms = n_samples / SAMPLE_RATE * 1000
    print(f"\nTest frequencies: {test_freqs} Hz")
    print(f"Cycles per freq: {segment_cycles}")
    print(f"Total: {n_samples} samples ({total_duration_ms:.0f}ms)")

    # リファレンス（C++ WaveShaperReference: 100回反復, std::exp）
    print("\nProcessing Reference (100 iterations)...")
    ref = ws_module.WaveShaperReference()
    ref.set_sample_rate(SAMPLE_RATE)
    ref.reset()
    output_ref = ref.process_array(input_signal.astype(np.float32))

    # テスト対象（全実装バリエーション）
    test_models = [
        # 基本実装
        ("Newton2", ws_module.WaveShaperNewton2),
        # Fast版（緩和ダンピング）
        ("Fast1", ws_module.WaveShaperFast1),
        ("Fast2", ws_module.WaveShaperFast2),
        # Hybrid版（1回目Fast + 2回目通常）
        ("Hybrid", ws_module.WaveShaperHybrid),
        # Turbo版（2反復、2回目E-B再利用）
        ("Turbo", ws_module.WaveShaperTurbo),
        # TurboLite版（ヤコビアン再利用）
        ("TurboLite", ws_module.WaveShaperTurboLite),
    ]

    # 440Hz以上の開始インデックス（22.5, 55, 110, 220の4セグメント後）
    high_freq_start = segment_starts[4]  # 440Hz, 880Hzの領域

    results = {}
    print("\nProcessing test models...")
    for name, cls in test_models:
        model = cls()
        model.set_sample_rate(SAMPLE_RATE)
        model.reset()
        output = model.process_array(input_signal.astype(np.float32))

        # 誤差計算（全体）
        err = output - output_ref
        rms = np.sqrt(np.mean(err**2)) * 1000  # mV
        max_e = np.max(np.abs(err)) * 1000  # mV

        # 誤差計算（440Hz以上のみ）
        err_hf = err[high_freq_start:]
        rms_hf = np.sqrt(np.mean(err_hf**2)) * 1000  # mV

        # SINAD (Signal-to-Noise-and-Distortion): リファレンスに対する品質指標
        # SINAD = リファレンスパワー / 誤差パワー (dB)
        # 高いほど良い
        ref_power = np.sum(output_ref**2)
        err_power = np.sum(err**2)
        sinad_db = 10 * np.log10(ref_power / (err_power + 1e-20))

        results[name] = {
            "output": output,
            "rms": rms,
            "rms_hf": rms_hf,
            "max": max_e,
            "sinad_db": sinad_db,
            "cls": cls,
        }

    # 誤差サマリー表示
    print("\n" + "=" * 70)
    print("Accuracy Summary (vs Reference)")
    print("=" * 70)
    print(f"{'Model':<15} {'RMS [mV]':<12} {'RMS≥440Hz':<12} {'Max [mV]':<12} {'SINAD [dB]':<12}")
    print("-" * 63)
    for name, data in results.items():
        print(f"{name:<15} {data['rms']:<12.2f} {data['rms_hf']:<12.2f} {data['max']:<12.2f} {data['sinad_db']:<12.2f}")

    # ========================================================================
    # ベンチマーク
    # ========================================================================
    print("\n" + "=" * 70)
    print("Performance Benchmark (1 second of audio)")
    print("=" * 70)

    bench_signal = generate_sawtooth(440.0, 1.0, ws_module).astype(np.float32)
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

    fig = plt.figure(figsize=(18, 14))
    fig.suptitle('TB-303 WaveShaper: Model Comparison', fontsize=14, fontweight='bold')

    # 色の統一定義（全モデル用）
    import matplotlib.cm as cm
    cmap = plt.colormaps['tab20']
    model_names = list(results.keys())
    model_colors = {name: cmap(i / len(model_names)) for i, name in enumerate(model_names)}

    # 1. 波形比較（上段全体）- 全モデル表示
    ax1 = fig.add_subplot(4, 2, (1, 2))
    ax1.plot(t_ms, input_signal, 'k--', alpha=0.3, label='Input', linewidth=1)
    ax1.plot(t_ms, output_ref, 'b-', label='Reference (100 iter)', linewidth=2)

    for name, data in results.items():
        ax1.plot(t_ms, data["output"], color=model_colors[name],
                 label=f'{name} ({data["rms"]:.1f}mV)', linewidth=1, alpha=0.7)

    ax1.set_xlabel('Time [ms]')
    ax1.set_ylabel('Voltage [V]')
    ax1.set_title('Waveform Comparison (6 frequencies: 22.5, 55, 110, 220, 440, 880 Hz)')
    ax1.legend(loc='upper right', fontsize=8, ncol=2)
    ax1.grid(True, alpha=0.3)

    # 2. 誤差時系列（左中上）- 全モデル表示
    ax2 = fig.add_subplot(4, 2, 3)
    for name, data in results.items():
        err_mv = (data["output"] - output_ref) * 1000
        ax2.plot(t_ms, err_mv, label=name, linewidth=1, alpha=0.7, color=model_colors[name])

    ax2.set_xlabel('Time [ms]')
    ax2.set_ylabel('Error [mV]')
    ax2.set_title('Error vs Reference')
    ax2.legend(loc='upper right', fontsize=9)
    ax2.grid(True, alpha=0.3)

    # 3. 誤差バー + ベンチマーク（右中上）
    ax3 = fig.add_subplot(4, 2, 4)

    names = list(results.keys())
    rms_vals = [results[n]["rms"] for n in names]
    rms_hf_vals = [results[n]["rms_hf"] for n in names]
    realtime_vals = [bench_results[n]["realtime"] for n in names]

    x = np.arange(len(names))
    width = 0.25

    ax3_twin = ax3.twinx()
    bars1 = ax3.bar(x - width, rms_vals, width, label='RMS All [mV]', color='steelblue')
    bars2 = ax3.bar(x, rms_hf_vals, width, label='RMS ≥440Hz [mV]', color='royalblue')
    bars3 = ax3_twin.bar(x + width, realtime_vals, width, label='Realtime Ratio', color='coral')

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

    # 4. 高周波ズーム（左中下）- 440Hz - 1周期表示
    ax4 = fig.add_subplot(4, 2, 5)
    freq_440 = test_freqs[4]  # 440Hz
    period_440_samples = int(round(SAMPLE_RATE / freq_440))
    start = segment_starts[4]
    end = start + period_440_samples  # 1周期分

    ax4.plot(t_ms[start:end], output_ref[start:end], 'b-', label='Reference', linewidth=2)
    for name, data in results.items():
        ax4.plot(t_ms[start:end], data["output"][start:end],
                 color=model_colors[name], label=name, linewidth=1, alpha=0.7)

    ax4.set_xlabel('Time [ms]')
    ax4.set_ylabel('Voltage [V]')
    ax4.set_title(f'Zoomed: 440Hz (1 cycle = {1000/freq_440:.2f}ms)')
    ax4.legend(loc='upper right', fontsize=8)
    ax4.grid(True, alpha=0.3)

    # 5. 低周波ズーム（右中下）- 22.5Hz - 1周期表示
    ax5 = fig.add_subplot(4, 2, 6)
    freq_22 = test_freqs[0]  # 22.5Hz
    period_22_samples = int(round(SAMPLE_RATE / freq_22))
    start = segment_starts[0]
    end = start + period_22_samples  # 1周期分

    ax5.plot(t_ms[start:end], output_ref[start:end], 'b-', label='Reference', linewidth=2)
    for name, data in results.items():
        ax5.plot(t_ms[start:end], data["output"][start:end],
                 color=model_colors[name], label=name, linewidth=1, alpha=0.7)

    ax5.set_xlabel('Time [ms]')
    ax5.set_ylabel('Voltage [V]')
    ax5.set_title(f'Zoomed: 22.5Hz (1 cycle = {1000/freq_22:.1f}ms)')
    ax5.legend(loc='upper right', fontsize=8)
    ax5.grid(True, alpha=0.3)

    # 6. SINAD バーグラフ（左下）
    ax6 = fig.add_subplot(4, 2, 7)
    sinad_vals = [results[n]["sinad_db"] for n in names]
    colors_bar = [model_colors[n] for n in names]
    bars = ax6.bar(x, sinad_vals, color=colors_bar)
    ax6.set_xticks(x)
    ax6.set_xticklabels(names, rotation=30, ha='right')
    ax6.set_ylabel('SINAD [dB]')
    ax6.set_title('SINAD: Signal / (Noise + Distortion) (higher = better)')
    ax6.grid(True, alpha=0.3, axis='y')

    # 7. スペクトル比較（右下）- 880Hz領域
    ax7 = fig.add_subplot(4, 2, 8)
    # 880Hz領域の出力をFFT
    freq_880 = test_freqs[5]
    start_880 = segment_starts[5]
    end_880 = start_880 + segment_lengths[5]

    ref_seg = output_ref[start_880:end_880]
    ref_fft = np.fft.rfft(ref_seg)
    freqs_fft = np.fft.rfftfreq(len(ref_seg), 1.0 / SAMPLE_RATE)

    ax7.plot(freqs_fft, 20 * np.log10(np.abs(ref_fft) + 1e-12), 'b-',
             label='Reference', linewidth=2, alpha=0.8)
    for name, data in results.items():
        seg = data["output"][start_880:end_880]
        seg_fft = np.fft.rfft(seg)
        ax7.plot(freqs_fft, 20 * np.log10(np.abs(seg_fft) + 1e-12),
                 color=model_colors[name], label=name, linewidth=1, alpha=0.6)

    ax7.set_xlabel('Frequency [Hz]')
    ax7.set_ylabel('Magnitude [dB]')
    ax7.set_title(f'Spectrum at {freq_880:.0f}Hz')
    ax7.legend(loc='upper right', fontsize=7)
    ax7.grid(True, alpha=0.3)
    ax7.set_xlim([0, SAMPLE_RATE / 2])

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
    run_oversampling_test(ws, output_dir)

    if not args.no_plot:
        plt.show()


if __name__ == '__main__':
    main()
