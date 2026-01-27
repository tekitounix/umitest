#!/usr/bin/env python3
"""
Newton反復回数 vs オーバーサンプリングの効果比較

疑問: Newton反復を減らした高速化でエイリアシングが増えているなら、
反復を増やせばオーバーサンプリングなしでも同等の品質が得られるか？

検証:
1. 1x OS + 各種Newton反復回数 (Reference, Newton2, Turbo)
2. 2x/4x OS + Turbo
3. コスト対効果の比較
"""

import sys
import os
import numpy as np
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'code'))

SAMPLE_RATE = 48000.0

def load_cpp_module():
    try:
        import tb303_waveshaper as ws
        return ws
    except ImportError:
        return None


def run_test():
    ws = load_cpp_module()
    if not ws:
        print("ERROR: C++ module not found")
        return

    from scipy import signal

    print("=" * 70)
    print("Newton Iterations vs Oversampling: Cost-Effectiveness Analysis")
    print("=" * 70)

    test_freq = 880.0  # 高周波でエイリアシング顕著
    duration = 0.1
    base_sr = SAMPLE_RATE

    # リファレンス: 8x OS + Reference (100 iterations)
    ref_os = 8
    ref_sr = base_sr * ref_os
    n_ref = int(ref_sr * duration)

    osc = ws.SawOscillator()
    osc.set_sample_rate(ref_sr)
    osc.set_frequency(test_freq)
    osc.reset()
    input_ref = osc.process_array(n_ref)

    ws_ref = ws.WaveShaperReference()
    ws_ref.set_sample_rate(ref_sr)
    ws_ref.reset()
    output_ref_os = ws_ref.process_array(input_ref.astype(np.float32))
    output_ref = signal.decimate(output_ref_os, ref_os, ftype='fir')

    results = {}

    # テストケース定義: (名前, モデルクラス, OS倍率)
    test_cases = [
        # 1x OS での各種Newton反復
        ("Ref(100) 1x", ws.WaveShaperReference, 1),
        ("Newton3 1x", ws.WaveShaperNewton3, 1),
        ("Newton2 1x", ws.WaveShaperNewton2, 1),
        ("Turbo 1x", ws.WaveShaperTurbo, 1),
        ("Fast1 1x", ws.WaveShaperFast1, 1),
        # オーバーサンプリング + Turbo
        ("Turbo 2x", ws.WaveShaperTurbo, 2),
        ("Turbo 4x", ws.WaveShaperTurbo, 4),
        # 比較用: Newton2 + OS
        ("Newton2 2x", ws.WaveShaperNewton2, 2),
    ]

    for name, model_cls, os_factor in test_cases:
        sr = base_sr * os_factor
        n = int(sr * duration)

        # 入力生成
        osc = ws.SawOscillator()
        osc.set_sample_rate(sr)
        osc.set_frequency(test_freq)
        osc.reset()
        input_os = osc.process_array(n)

        # 処理
        model = model_cls()
        model.set_sample_rate(sr)
        model.reset()

        # ベンチマーク
        n_bench = 10
        times = []
        for _ in range(n_bench):
            model.reset()
            t0 = time.perf_counter()
            output_os = model.process_array(input_os.astype(np.float32))
            t1 = time.perf_counter()
            times.append(t1 - t0)
        avg_time = np.mean(times)

        # デシメーション
        if os_factor > 1:
            output = signal.decimate(output_os, os_factor, ftype='fir')
        else:
            output = output_os

        # リファレンスとの比較
        min_len = min(len(output), len(output_ref))
        output = output[:min_len]
        ref = output_ref[:min_len]

        err = output - ref
        rms = np.sqrt(np.mean(err**2)) * 1000
        ref_power = np.sum(ref**2)
        err_power = np.sum(err**2)
        sinad = 10 * np.log10(ref_power / (err_power + 1e-20))

        # 相対コスト (処理サンプル数 × 時間)
        # OS倍率を考慮した実効コスト
        cost = avg_time * os_factor  # OSのためにos_factor倍の処理が必要

        results[name] = {
            "sinad": sinad,
            "rms": rms,
            "time_ms": avg_time * 1000,
            "cost": cost * 1000,
            "os_factor": os_factor,
        }

    # 結果表示
    print(f"\n{'Model':<15} {'SINAD[dB]':<12} {'RMS[mV]':<12} {'Time[ms]':<12} {'Cost':<12}")
    print("-" * 63)

    # Turbo 1xを基準としたコスト比
    turbo_1x_cost = results["Turbo 1x"]["cost"]

    for name, data in results.items():
        cost_ratio = data["cost"] / turbo_1x_cost
        print(f"{name:<15} {data['sinad']:<12.2f} {data['rms']:<12.2f} {data['time_ms']:<12.3f} {cost_ratio:<12.2f}x")

    # 分析
    print("\n" + "=" * 70)
    print("Analysis")
    print("=" * 70)

    turbo_1x = results["Turbo 1x"]
    turbo_2x = results["Turbo 2x"]
    turbo_4x = results["Turbo 4x"]
    ref_1x = results["Ref(100) 1x"]
    newton2_1x = results["Newton2 1x"]
    newton3_1x = results["Newton3 1x"]

    print(f"\n1. Newton反復による改善 (1x OS):")
    print(f"   Fast1 → Turbo: {results['Turbo 1x']['sinad'] - results['Fast1 1x']['sinad']:.2f} dB")
    print(f"   Turbo → Newton2: {newton2_1x['sinad'] - turbo_1x['sinad']:.2f} dB")
    print(f"   Newton2 → Newton3: {newton3_1x['sinad'] - newton2_1x['sinad']:.2f} dB")
    print(f"   Newton3 → Ref(100): {ref_1x['sinad'] - newton3_1x['sinad']:.2f} dB")

    print(f"\n2. オーバーサンプリングによる改善 (Turbo):")
    print(f"   1x → 2x: {turbo_2x['sinad'] - turbo_1x['sinad']:.2f} dB (コスト {turbo_2x['cost']/turbo_1x['cost']:.1f}x)")
    print(f"   1x → 4x: {turbo_4x['sinad'] - turbo_1x['sinad']:.2f} dB (コスト {turbo_4x['cost']/turbo_1x['cost']:.1f}x)")

    print(f"\n3. コスト対効果比較:")
    print(f"   Turbo 4x:    SINAD {turbo_4x['sinad']:.1f} dB, コスト {turbo_4x['cost']/turbo_1x_cost:.1f}x")
    print(f"   Ref(100) 1x: SINAD {ref_1x['sinad']:.1f} dB, コスト {ref_1x['cost']/turbo_1x_cost:.1f}x")
    print(f"   Newton3 1x:  SINAD {newton3_1x['sinad']:.1f} dB, コスト {newton3_1x['cost']/turbo_1x_cost:.1f}x")

    # Turbo 4xと同等のSINADを達成するのに必要な反復回数の推定
    print(f"\n4. 結論:")
    if turbo_4x['sinad'] > ref_1x['sinad']:
        print(f"   → 4x OSは100回反復よりも高いSINADを達成")
        print(f"   → エイリアシング低減にはOSが本質的に有効")
    else:
        print(f"   → 100回反復の方がSINADが高い")
        print(f"   → 反復回数増加でもある程度対応可能")

    sinad_per_cost_turbo4x = (turbo_4x['sinad'] - turbo_1x['sinad']) / (turbo_4x['cost']/turbo_1x_cost - 1)
    sinad_per_cost_newton3 = (newton3_1x['sinad'] - turbo_1x['sinad']) / (newton3_1x['cost']/turbo_1x_cost - 1) if newton3_1x['cost'] > turbo_1x_cost else 0

    print(f"\n   SINAD/コスト効率:")
    print(f"   - Turbo 1x→4x: {sinad_per_cost_turbo4x:.2f} dB per 1x cost")
    if sinad_per_cost_newton3 > 0:
        print(f"   - Turbo→Newton3: {sinad_per_cost_newton3:.2f} dB per 1x cost")


if __name__ == '__main__':
    run_test()
