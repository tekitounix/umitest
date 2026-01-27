// =============================================================================
// TB-303 VCO - PolyBLEP Sawtooth Oscillator
//
// TB-303のVCOは下降ノコギリ波を出力する。
// PolyBLEP (Polynomial Bandlimited Step) でエイリアシングを低減。
//
// 出力範囲: 12V → 5.5V (下降ノコギリ波)
//
// Author: Claude (Anthropic)
// License: MIT
// =============================================================================

#pragma once

#include <cmath>

namespace tb303 {
namespace vco {

// =============================================================================
// PolyBLEP補正関数
//
// 参考: https://github.com/martinfinke/PolyBLEP
//       https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/
//
// PolyBLEP (Polynomial Bandlimited Step) でエイリアシングを低減。
// =============================================================================
namespace detail {

// PolyBLEP関数（標準的な実装）
// 参考: https://pbat.ch/sndkit/blep/
// t: 現在の位相 [0, 1)
// dt: 位相増分（1サンプルあたり）
// first: 初期状態フラグ（trueなら直後の補正をスキップ）
// 戻り値: 補正量
inline float blep(float t, float dt, bool first = false) {
    if (!first && t < dt) {
        // 不連続点の直後: t ∈ (0, dt)
        // first=trueの場合はスキップ（初期状態は位相wrap直後ではない）
        float x = t / dt;
        // 2t - t^2 - 1
        return (x + x) - (x * x) - 1.0f;
    }
    if (t > 1.0f - dt) {
        // 不連続点の直前: t ∈ (1-dt, 1)
        float x = (t - 1.0f) / dt;
        // t^2 + 2t + 1
        return (x * x) + (x + x) + 1.0f;
    }
    return 0.0f;
}

// 4次PolyBLEP（より高品質）
inline float blep4(float t, float dt, bool first = false) {
    if (!first && t < dt) {
        float x = t / dt;
        float x2 = x * x;
        float x3 = x2 * x;
        float x4 = x2 * x2;
        // 4t - 6t^2 + 4t^3 - t^4 - 1
        return (4.0f * x) - (6.0f * x2) + (4.0f * x3) - x4 - 1.0f;
    }
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        float x2 = x * x;
        float x3 = x2 * x;
        float x4 = x2 * x2;
        // t^4 + 4t^3 + 6t^2 + 4t + 1
        return x4 + (4.0f * x3) + (6.0f * x2) + (4.0f * x) + 1.0f;
    }
    return 0.0f;
}

}  // namespace detail

// =============================================================================
// 係数構造体（サンプルレート依存）
// =============================================================================
struct OscillatorCoeffs {
    float sample_rate;
    float inv_sample_rate;  // 1 / sample_rate
};

inline OscillatorCoeffs make_oscillator_coeffs(float sample_rate) {
    return {sample_rate, 1.0f / sample_rate};
}

// =============================================================================
// 状態構造体
// =============================================================================
struct OscillatorState {
    float phase = 0.0f;       // [0, 1)
    float freq = 110.0f;      // Hz
    float phase_inc = 0.0f;   // 位相増分（サンプル毎）
    bool first_sample = true; // 初期状態フラグ

    void reset() {
        phase = 0.0f;
        first_sample = true;
    }

    void setFrequency(float f, const OscillatorCoeffs& c) {
        freq = f;
        phase_inc = f * c.inv_sample_rate;
        // 周波数変更時も first_sample をセット
        // （周波数変更直後は wrap 直後ではないため直後補正をスキップ）
        first_sample = true;
    }
};

// =============================================================================
// PolyBLEP下降ノコギリ波オシレータ
//
// 参考実装: https://github.com/martinfinke/PolyBLEP
//
// 出力: 12V → 5.5V（TB-303互換）
// 位相リセット時（phase wrap）にPolyBLEP補正を適用
// =============================================================================

// 標準的なPolyBLEP実装（2次）
// 下降ノコギリ波: phase=0 → 12V, phase=1 → 5.5V
// 不連続点: phase=1→0 で波形は 5.5V→12V（上昇エッジ）
// 上向きステップなので補正は加算
inline float process_saw_accurate(OscillatorState& s, const OscillatorCoeffs&) {
    constexpr float V_HIGH = 12.0f;
    constexpr float V_LOW  = 5.5f;
    constexpr float V_RANGE = V_HIGH - V_LOW;

    float phase = s.phase;

    // PolyBLEPの前提を満たす範囲にクランプ
    float dt = s.phase_inc;
    if (dt <= 0.0f) dt = 0.0f;
    if (dt >= 1.0f) dt = 0.999999f;

    // 基本波形: 下降ノコギリ波
    float v_out = V_HIGH - (phase * V_RANGE);

    // wrap(1->0) は上向きステップなので補正は加算
    // first_sample=trueの場合は直後補正をスキップ（初期状態はwrap直後ではない）
    v_out += V_RANGE * detail::blep(phase, dt, s.first_sample);
    s.first_sample = false;

    // 位相を進める
    phase += dt;
    if (phase >= 1.0f) phase -= 1.0f;
    s.phase = phase;

    return v_out;
}

// 4次PolyBLEP版（より高品質）
inline float process_saw_hq(OscillatorState& s, const OscillatorCoeffs&) {
    constexpr float V_HIGH = 12.0f;
    constexpr float V_LOW  = 5.5f;
    constexpr float V_RANGE = V_HIGH - V_LOW;

    float phase = s.phase;

    float dt = s.phase_inc;
    if (dt <= 0.0f) dt = 0.0f;
    if (dt >= 1.0f) dt = 0.999999f;

    float v_out = V_HIGH - (phase * V_RANGE);

    // 上向きステップなので加算
    // first_sample=trueの場合は直後補正をスキップ（初期状態はwrap直後ではない）
    v_out += V_RANGE * detail::blep4(phase, dt, s.first_sample);
    s.first_sample = false;

    phase += dt;
    if (phase >= 1.0f) phase -= 1.0f;
    s.phase = phase;

    return v_out;
}

// =============================================================================
// オシレータクラス（使いやすいインターフェース）
// =============================================================================
class SawOscillator {
public:
    void setSampleRate(float sr) {
        coeffs_ = make_oscillator_coeffs(sr);
        state_.setFrequency(state_.freq, coeffs_);
    }

    void setFrequency(float freq) {
        state_.setFrequency(freq, coeffs_);
    }

    float getFrequency() const { return state_.freq; }

    void reset() { state_.reset(); }

    // 標準品質（2次PolyBLEP）
    float process() {
        return process_saw_accurate(state_, coeffs_);
    }

    // 高品質（4次PolyBLEP）
    float processHQ() {
        return process_saw_hq(state_, coeffs_);
    }

    // Naive（PolyBLEPなし、比較用）
    float processNaive() {
        constexpr float V_HIGH = 12.0f;
        constexpr float V_RANGE = 6.5f;

        float v_out = V_HIGH - state_.phase * V_RANGE;

        state_.phase += state_.phase_inc;
        if (state_.phase >= 1.0f) {
            state_.phase -= 1.0f;
        }

        return v_out;
    }

private:
    OscillatorState state_{};
    OscillatorCoeffs coeffs_ = make_oscillator_coeffs(48000.0f);
};

// =============================================================================
// TB-303 VCO + WaveShaper 統合クラス
// =============================================================================
template <typename WaveShaper>
class TB303VCO {
public:
    void setSampleRate(float sr) {
        osc_.setSampleRate(sr);
        shaper_.setSampleRate(sr);
    }

    void setFrequency(float freq) {
        osc_.setFrequency(freq);
    }

    float getFrequency() const { return osc_.getFrequency(); }

    void reset() {
        osc_.reset();
        shaper_.reset();
    }

    // 波形整形なし（デバッグ用）
    float processRaw() {
        return osc_.process();
    }

    // 波形整形あり
    float process() {
        float v_saw = osc_.process();
        return shaper_.process(v_saw);
    }

    // 高品質版
    float processHQ() {
        float v_saw = osc_.processHQ();
        return shaper_.process(v_saw);
    }

    SawOscillator& oscillator() { return osc_; }
    WaveShaper& waveshaper() { return shaper_; }

private:
    SawOscillator osc_{};
    WaveShaper shaper_{};
};

}  // namespace vco
}  // namespace tb303
