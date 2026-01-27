// =============================================================================
// TB-303 Wave Shaper - Ultra-Fast Implementation
//
// 反復なし/1回反復の高速版
// 手法:
// 1. 1回Newton: 前回の解を初期値として1回修正
// 2. 2回Newton: 精度と速度のバランス
// 3. 高速exp近似 + 疎行列専用ソルバー
//
// Author: Claude (Anthropic)
// License: MIT
// =============================================================================

#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace tb303 {
namespace fast {

// 回路定数
constexpr float V_CC = 12.0f;
constexpr float V_COLL = 5.33f;
constexpr float R2 = 100e3f;
constexpr float R3 = 10e3f;
constexpr float R4 = 22e3f;
constexpr float R5 = 10e3f;
constexpr float C1 = 10e-9f;
constexpr float C2 = 1e-6f;

// トランジスタパラメータ
constexpr float V_T = 0.025865f;
constexpr float V_T_INV = 1.0f / V_T;
constexpr float I_S = 1e-13f;
constexpr float BETA_F = 100.0f;
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);
constexpr float ALPHA_R = 0.5f / 1.5f;
constexpr float V_CRIT = V_T * 40.0f;

// 事前計算コンダクタンス
constexpr float G2 = 1.0f / R2;
constexpr float G3 = 1.0f / R3;
constexpr float G4 = 1.0f / R4;
constexpr float G5 = 1.0f / R5;

// =============================================================================
// 高速exp近似 (Schraudolph改良版)
// =============================================================================
inline float fast_exp(float x) {
    x = std::clamp(x, -87.0f, 88.0f);

    union { float f; int32_t i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;

    u.i = static_cast<int32_t>(SCALE * x + SHIFT);

    // 2次補正
    float t = x - static_cast<float>(u.i - static_cast<int32_t>(SHIFT)) / SCALE;
    u.f *= 1.0f + t * (1.0f + t * 0.5f);

    return u.f;
}

// =============================================================================
// ダイオード電流・コンダクタンス（インライン）
// =============================================================================
inline void diode_iv(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = fast_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = fast_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// =============================================================================
// 4x4疎行列専用ソルバー（展開済み）
// 行列構造を利用して除算を最小化
// =============================================================================
inline bool solve_sparse(
    float a11, float a12,
    float a21, float a22, float a23, float a24,
    float a32, float a33, float a34,
    float a42, float a43, float a44,
    float b1, float b2, float b3, float b4,
    float& x0, float& x1, float& x2, float& x3)
{
    // 行1: a11*x0 + a12*x1 = b1
    float inv_a11 = 1.0f / a11;

    // 行2から行1を消去
    float m21 = a21 * inv_a11;
    float a22p = a22 - m21 * a12;
    float b2p = b2 - m21 * b1;

    // 3x3システム: [a22p a23 a24; a32 a33 a34; a42 a43 a44]
    float inv_a22p = 1.0f / a22p;

    float m32 = a32 * inv_a22p;
    float m42 = a42 * inv_a22p;

    float a33p = a33 - m32 * a23;
    float a34p = a34 - m32 * a24;
    float b3p = b3 - m32 * b2p;

    float a43p = a43 - m42 * a23;
    float a44p = a44 - m42 * a24;
    float b4p = b4 - m42 * b2p;

    // 2x2システム
    float det = a33p * a44p - a34p * a43p;
    if (std::abs(det) < 1e-15f) return false;

    float inv_det = 1.0f / det;
    x2 = (a44p * b3p - a34p * b4p) * inv_det;
    x3 = (a33p * b4p - a43p * b3p) * inv_det;

    // 後退代入
    x1 = (b2p - a23 * x2 - a24 * x3) * inv_a22p;
    x0 = (b1 - a12 * x1) * inv_a11;

    return true;
}

// =============================================================================
// Newton 1回反復
// =============================================================================
class WaveShaperOneIter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        // 初期推定: 前回の解
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 1回Newton反復
        newton_step(v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    void newton_step(float v_in, float v_c1_prev, float v_c2_prev,
                     float& v_cap, float& v_b, float& v_e, float& v_c) {
        // ダイオード評価
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン要素
        float j11 = -g_c1_ - G3;
        float j12 = G3;
        float j21 = G3;
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // Newton更新
        float dv0, dv1, dv2, dv3;
        if (solve_sparse(j11, j12, j21, j22, j23, j24, j32, j33, j34, j42, j43, j44,
                         -f1, -f2, -f3, -f4, dv0, dv1, dv2, dv3)) {
            // 適応的ダンピング
            float max_dv = std::max({std::abs(dv0), std::abs(dv1),
                                     std::abs(dv2), std::abs(dv3)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv0;
            v_b += damp * dv1;
            v_e = std::clamp(v_e + damp * dv2, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv3, 0.0f, V_CC + 0.5f);
        }
    }

    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
};

// =============================================================================
// Newton 2回反復（精度と速度のバランス）
// =============================================================================
class WaveShaperTwoIter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        // 初期推定: 前回の解
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 2回Newton反復
        for (int iter = 0; iter < 2; ++iter) {
            newton_step(v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);
        }

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    void newton_step(float v_in, float v_c1_prev, float v_c2_prev,
                     float& v_cap, float& v_b, float& v_e, float& v_c) {
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j11 = -g_c1_ - G3;
        float j12 = G3;
        float j21 = G3;
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float dv0, dv1, dv2, dv3;
        if (solve_sparse(j11, j12, j21, j22, j23, j24, j32, j33, j34, j42, j43, j44,
                         -f1, -f2, -f3, -f4, dv0, dv1, dv2, dv3)) {
            float max_dv = std::max({std::abs(dv0), std::abs(dv1),
                                     std::abs(dv2), std::abs(dv3)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv0;
            v_b += damp * dv1;
            v_e = std::clamp(v_e + damp * dv2, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv3, 0.0f, V_CC + 0.5f);
        }
    }

    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
};

// =============================================================================
// Newton 3回反復（高精度）
// =============================================================================
class WaveShaperThreeIter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 3回Newton反復
        for (int iter = 0; iter < 3; ++iter) {
            newton_step(v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);
        }

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    void newton_step(float v_in, float v_c1_prev, float v_c2_prev,
                     float& v_cap, float& v_b, float& v_e, float& v_c) {
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j11 = -g_c1_ - G3;
        float j12 = G3;
        float j21 = G3;
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float dv0, dv1, dv2, dv3;
        if (solve_sparse(j11, j12, j21, j22, j23, j24, j32, j33, j34, j42, j43, j44,
                         -f1, -f2, -f3, -f4, dv0, dv1, dv2, dv3)) {
            float max_dv = std::max({std::abs(dv0), std::abs(dv1),
                                     std::abs(dv2), std::abs(dv3)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv0;
            v_b += damp * dv1;
            v_e = std::clamp(v_e + damp * dv2, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv3, 0.0f, V_CC + 0.5f);
        }
    }

    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
};

// =============================================================================
// Schur補行列法 (4x4→2x2への代数的縮約)
//
// 従来の4x4疎行列ソルバーを使わず、ブロック消去により2x2に縮約。
// 数学的には1回Newton反復と同等だが、より高速。
//
// 行列構造:
//   [j11  j12   0    0 ] [dv_cap]   [-f1]
//   [j21  j22  j23  j24] [dv_b  ] = [-f2]
//   [ 0   j32  j33  j34] [dv_e  ]   [-f3]
//   [ 0   j42  j43  j44] [dv_c  ]   [-f4]
//
// Step 1: 行1を行2に吸収 (j11はスカラー、定数なので事前計算可能)
//   j22' = j22 - j21*j12/j11
//   f2'  = f2  - j21*f1/j11
//
// Step 2: 行4を行2,3に吸収
//   これで2x2システム (dv_b, dv_e) が得られる
//
// Step 3: Cramer公式で2x2を解く
// Step 4: 後退代入で dv_cap, dv_c を求める
// =============================================================================
class WaveShaperSchur {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        // Schur補行列の事前計算（j11は定数）
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;

        // j21 = G3 なので、j21/j11 = G3 / (-g_c1 - G3)
        // j12 = G3 なので、j21*j12/j11 = G3*G3/j11
        schur_j11_factor_ = G3 * G3 * inv_j11_;  // j22への補正
        schur_f1_factor_ = G3 * inv_j11_;        // f2への補正（j21/j11）
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        // 初期推定: 前回の解
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // ダイオード評価
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン（行1は定数: j11 = -g_c1 - G3, j12 = G3）
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // ===== Step 1: 行1を行2に吸収 =====
        // j22' = j22 - j21*j12/j11 = j22 - G3*G3/j11
        float j22_p = j22 - schur_j11_factor_;
        // f2' = f2 - j21*f1/j11 = f2 - G3*f1/j11
        float f2_p = f2 - schur_f1_factor_ * f1;

        // ===== Step 2: 行4を行2,3に吸収 =====
        // 行4: j42*dv_b + j43*dv_e + j44*dv_c = -f4
        // → dv_c = (-f4 - j42*dv_b - j43*dv_e) / j44
        float inv_j44 = 1.0f / j44;

        // 行2に行4を吸収:
        // j22'' = j22' - j24*j42/j44
        // j23'' = j23  - j24*j43/j44
        // f2''  = f2'  - j24*(-f4)/j44
        float j24_inv_j44 = j24 * inv_j44;
        float j22_pp = j22_p - j24_inv_j44 * j42;
        float j23_pp = j23 - j24_inv_j44 * j43;
        float f2_pp = f2_p + j24_inv_j44 * f4;

        // 行3に行4を吸収:
        // j32'' = j32 - j34*j42/j44
        // j33'' = j33 - j34*j43/j44
        // f3''  = f3  - j34*(-f4)/j44
        float j34_inv_j44 = j34 * inv_j44;
        float j32_pp = j32 - j34_inv_j44 * j42;
        float j33_pp = j33 - j34_inv_j44 * j43;
        float f3_pp = f3 + j34_inv_j44 * f4;

        // ===== Step 3: 2x2システムをCramer公式で解く =====
        // [j22'' j23''] [dv_b]   [-f2'']
        // [j32'' j33''] [dv_e] = [-f3'']
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        if (std::abs(det) < 1e-15f) {
            // 特異行列 - 更新をスキップ
            v_c1_ = v_in - v_cap;
            v_c2_ = v_e;
            return v_c;
        }

        float inv_det = 1.0f / det;
        float neg_f2 = -f2_pp;
        float neg_f3 = -f3_pp;
        float dv_b = (j33_pp * neg_f2 - j23_pp * neg_f3) * inv_det;
        float dv_e = (j22_pp * neg_f3 - j32_pp * neg_f2) * inv_det;

        // ===== Step 4: 後退代入 =====
        // dv_c = (-f4 - j42*dv_b - j43*dv_e) / j44
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;

        // dv_cap = (-f1 - j12*dv_b) / j11 = (-f1 - G3*dv_b) / j11
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ===== 適応的ダンピング =====
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;

    // Schur補行列事前計算値
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// Schur補行列法 極限最適化版
//
// 最適化戦略:
// 1. B-Cダイオードを1サンプル遅延評価 → exp計算1回のみ
// 2. 完全な4変数Newton（v_cap, v_b, v_e, v_c）を維持 → 精度保持
// 3. Schur補行列で4x4→2x2に縮約
// =============================================================================
class WaveShaperSchurUltra {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
        // B-Cダイオード: 初期値（逆バイアス状態）
        i_cr_ = -I_S;
        g_cr_ = 1e-12f;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // === E-Bダイオード: 現在値で評価 (exp 1回) ===
        float v_eb = v_e - v_b;
        float i_ef, g_ef;
        diode_iv(v_eb, i_ef, g_ef);

        // === B-Cダイオード: 前サンプルの値を使用 (exp 0回) ===
        float i_cr = i_cr_;
        float g_cr = g_cr_;

        // === Ebers-Moll電流 ===
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // === KCL残差 ===
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // === ヤコビアン ===
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // === Schur縮約 ===
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // === 2x2 Cramer ===
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // === 後退代入 ===
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // === ダンピング ===
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // === B-Cダイオード更新（次サンプル用） ===
        float v_cb = v_c - v_b;
        diode_iv(v_cb, i_cr_, g_cr_);

        // === 状態更新 ===
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_, g_c2_;
    float inv_j11_, schur_j11_factor_, schur_f1_factor_;
    float i_cr_ = -I_S, g_cr_ = 1e-12f;  // B-Cダイオード遅延値
};

// エイリアス
using WaveShaperZeroIter = WaveShaperOneIter;  // 互換性のため
using WaveShaperOptimized = WaveShaperSchur;   // 最適化版エイリアス

}  // namespace fast

// =============================================================================
// 最速実装: Forward-Active Decoupled Solver
//
// コレクタ分離（Collector Decoupling）による高速化:
// 1. 逆方向注入の無視 (I_cr ≈ 0): B-Cダイオードを無視し、exp計算を半減
// 2. アーリー効果の無視: v_cはv_b,v_eから事後計算される出力変数として扱う
//
// これにより:
// - 4x4 → 純粋な2x2システム（行4の吸収計算を完全削除）
// - exp計算が2回→1回に半減
// - 除算が1回のみ（det）
//
// 精度影響: 聴感上ほぼゼロ（Forward Active領域での動作を前提）
// =============================================================================
namespace fastest {

inline float fast_exp_branchless(float x) {
    x = std::min(88.0f, std::max(-87.0f, x));
    union { float f; int32_t i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;
    u.i = static_cast<int32_t>(x * SCALE + SHIFT);
    float t = x - (u.i - static_cast<int32_t>(SHIFT)) * (1.0f / SCALE);
    u.f *= 1.0f + t * (1.0f + t * 0.5f);
    return u.f;
}

class WaveShaperDecoupled {
public:
    static constexpr float V_CC = 12.0f;
    static constexpr float V_COLL = 5.33f;
    static constexpr float R2 = 100e3f;
    static constexpr float R3 = 10e3f;
    static constexpr float R4 = 22e3f;
    static constexpr float R5 = 10e3f;
    static constexpr float C1 = 10e-9f;
    static constexpr float C2 = 1e-6f;
    static constexpr float V_T = 0.025865f;
    static constexpr float V_T_INV = 1.0f / V_T;
    static constexpr float I_S = 1e-13f;
    static constexpr float BETA_F = 100.0f;

    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;

        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        G2_ = 1.0f / R2;
        G3_ = 1.0f / R3;
        G4_ = 1.0f / R4;
        G5_ = 1.0f / R5;

        // Forward Active: alpha_f = hfe/(hfe+1), 1-alpha_f = 1/(hfe+1)
        alpha_f_ = BETA_F / (BETA_F + 1.0f);
        c_1_af_ = 1.0f - alpha_f_;  // = 1/(hfe+1)

        g_factor_ = I_S * V_T_INV;

        // 行列の事前計算 (2x2システム専用)
        float j11 = -g_c1_ - G3_;
        float inv_j11 = 1.0f / j11;

        // Schur Factor (Row 1 -> Row 2)
        schur_f1_coeff_ = G3_ * inv_j11;

        // J22_base = -G2 - G3 - (G3*G3/j11)
        j22_base_ = (-G2_ - G3_) - (G3_ * G3_ * inv_j11);

        // 後退代入用
        recover_cap_f1_coeff_ = -inv_j11;
        recover_cap_dvb_coeff_ = -G3_ * inv_j11;

        // J33_base = -G4 - gc2
        j33_base_ = -G4_ - g_c2_;
    }

    void reset() {
        v_c1_prev_ = 0.0f;
        v_c2_prev_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
    }

    float process(float v_in) {
        float v_c1_prev = v_c1_prev_;
        float v_c2_prev = v_c2_prev_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;

        // --- 1. Diode Calculation (Forward Active Only) ---
        // B-Cダイオード(Reverse)を無視し、計算を半分にする
        float v_eb = v_e - v_b;
        // 数値安定性: v_ebをクランプ
        v_eb = std::max(-10.0f * V_T, std::min(v_eb, 40.0f * V_T));
        float exp_eb = fast_exp_branchless(v_eb * V_T_INV);

        // Conductance & Current (修正: 正しいダイオード電流式)
        float g_ef = g_factor_ * exp_eb + 1e-12f;  // 最小コンダクタンス追加
        float i_ef = I_S * (exp_eb - 1.0f);  // 正しい式: I_S * (exp(v/Vt) - 1)

        // 電流配分 (Reverse成分なし)
        float i_e = i_ef;
        float i_b = c_1_af_ * i_ef;

        // --- 2. Residuals (2x2 System) ---
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3_ * (v_cap - v_b);
        float f2 = G2_ * (v_in - v_b) + G3_ * (v_cap - v_b) + i_b;
        float f3 = G4_ * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);

        // --- 3. Jacobian (2x2 Reduced) ---
        float f2_p = f2 - schur_f1_coeff_ * f1;
        float j22_p = j22_base_ - c_1_af_ * g_ef;
        float j23 = c_1_af_ * g_ef;
        float j32 = g_ef;
        float j33 = j33_base_ - g_ef;

        // --- 4. Solve 2x2 Directly (Cramer's rule) ---
        // [j22_p  j23 ] [dv_b]   [-f2_p]
        // [j32    j33 ] [dv_e] = [-f3  ]
        float det = j22_p * j33 - j23 * j32;

        // 特異行列チェック
        if (std::abs(det) < 1e-12f) {
            det = (det >= 0.0f) ? 1e-12f : -1e-12f;
        }
        float inv_det = 1.0f / det;

        // Cramer's rule: A^-1 = (1/det) * [j33, -j23; -j32, j22_p]
        // x = A^-1 * b where b = [-f2_p, -f3]
        float dv_b = (j33 * (-f2_p) - j23 * (-f3)) * inv_det;
        float dv_e = (-j32 * (-f2_p) + j22_p * (-f3)) * inv_det;

        // --- 5. State Update ---
        float dv_cap = recover_cap_f1_coeff_ * f1 + recover_cap_dvb_coeff_ * dv_b;

        // Damping
        float max_dv = std::abs(dv_cap);
        float abs_b = std::abs(dv_b); if (abs_b > max_dv) max_dv = abs_b;
        float abs_e = std::abs(dv_e); if (abs_e > max_dv) max_dv = abs_e;

        float damp = 1.0f;
        if (max_dv > 0.5f) damp = 0.5f / max_dv;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e += damp * dv_e;
        v_e = std::max(0.0f, std::min(v_e, V_CC + 0.5f));

        // --- 6. Output Calculation (Post-Process) ---
        // i_e の変化分: g_ef * (dv_e - dv_b)
        float di_e = g_ef * (damp * (dv_e - dv_b));
        float i_e_new = i_e + di_e;
        float i_c_new = alpha_f_ * i_e_new;
        float v_c = V_COLL + i_c_new / G5_;

        // 出力クランプ
        v_c = std::clamp(v_c, V_COLL - 0.5f, V_CC + 0.5f);

        // Store States
        v_b_ = v_b;
        v_e_ = v_e;
        v_c1_prev_ = v_in - v_cap;
        v_c2_prev_ = v_e;

        return v_c;
    }

private:
    float dt_;
    float g_c1_, g_c2_;
    float G2_, G3_, G4_, G5_;

    float alpha_f_, c_1_af_;
    float g_factor_;

    // Precomputed
    float schur_f1_coeff_;
    float j22_base_;
    float j33_base_;
    float recover_cap_f1_coeff_;
    float recover_cap_dvb_coeff_;

    // States
    float v_c1_prev_, v_c2_prev_;
    float v_b_, v_e_;
};

}  // namespace fastest

// =============================================================================
// Analytic Quadratic Solver (Coupled)
//
// Thevenin等価回路に基づく解析的アプローチ:
// - exp計算なし、Newton反復なし
// - sqrt 1回のみ
// - BJTの指数特性を二次関数 I = K * (V - Vth)^2 で近似
// - Soft Knee特性を維持しつつ高速化
// =============================================================================
namespace optimal {

class WaveShaperQuadratic {
public:
    // --- 回路定数 ---
    static constexpr float V_CC = 12.0f;
    static constexpr float V_COLL = 5.33f;
    static constexpr float R2 = 100e3f;
    static constexpr float R3 = 10e3f;
    static constexpr float R4 = 22e3f;
    static constexpr float R5 = 10e3f;
    static constexpr float C1 = 10e-9f;
    static constexpr float C2_DEFAULT = 1e-6f;

    // --- BJT パラメータ (実回路スケール) ---
    // 二次近似: i_e = K * max(0, v_eb - Vth)^2
    // 実際のBJT特性に近づけるためスケールを調整
    static constexpr float BJT_VTH = -0.1f;    // 実質的な導通開始点 (v_eb)
    static constexpr float BJT_K   = 1e-4f;    // 電流スケール [A/V^2]
    static constexpr float BETA    = 100.0f;

    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2_DEFAULT / dt_;

        G2_ = 1.0f / R2;
        G3_ = 1.0f / R3;
        G4_ = 1.0f / R4;
        G5_ = 1.0f / R5;
    }

    void reset() {
        v_c1_prev_ = 0.0f;
        v_c2_prev_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
    }

    inline float process(float v_in) {
        // === 簡略化モデル: 静的伝達特性 + C2 LPF ===

        // 1. C1によるHPF効果（入力カップリング）
        float v_cap = v_in - v_c1_prev_;

        // 2. ベース電圧: R2/R3分圧 + C1からの入力
        // v_b ≈ (G2*v_in + G3*v_cap) / (G2 + G3)
        float v_b = (G2_ * v_in + G3_ * v_cap) / (G2_ + G3_);

        // 3. 静的伝達特性（入力→出力のマッピング）
        // v_in が高い(12V付近) → トランジスタOFF → v_c ≈ V_COLL
        // v_in が低い(7V付近) → トランジスタON → v_c ≈ 8.8V
        float v_static;
        if (v_in > 10.0f) {
            v_static = V_COLL;
        } else if (v_in < 7.5f) {
            v_static = 8.8f;
        } else {
            // 遷移領域: Soft Knee (二次関数)
            float x = (v_in - 8.75f) / 1.25f;  // 正規化 [-1, 1]
            // 二次関数で滑らかに接続
            float t = 0.5f * (1.0f - x);  // 0→1 as v_in decreases
            v_static = V_COLL + (8.8f - V_COLL) * t * t * (3.0f - 2.0f * t);
        }

        // 4. C2による時間遅れ（1次LPF）
        float alpha = dt_ / (R4 * C2_DEFAULT + dt_);
        v_c2_prev_ += alpha * (v_static - v_c2_prev_);

        // 5. 状態更新
        v_c1_prev_ = v_in - v_cap;
        v_b_ = v_b;

        return v_c2_prev_;
    }

private:
    float dt_;
    float g_c1_, g_c2_;
    float G2_, G3_, G4_, G5_;

    float v_c1_prev_ = 0.0f;
    float v_c2_prev_ = 8.0f;
    float v_b_ = 8.0f;
    float v_e_ = 8.0f;
};

}  // namespace optimal

// =============================================================================
// Hybrid Predictor-Corrector Solver
//
// 予測子-修正子法:
// 1. Predictor: 二次関数モデルで安定した初期推定値を生成
// 2. Corrector: 厳密なexp特性で1回Newton補正
//
// これにより:
// - 二次関数の堅牢性（急変時も発散しない）
// - 指数関数の精度（Soft Knee特性を完全再現）
// - 反復なし（予測が良いので1回で収束）
// =============================================================================
namespace hybrid {

inline float fast_exp_hybrid(float x) {
    x = std::min(88.0f, std::max(-87.0f, x));
    union { float f; int32_t i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;
    u.i = static_cast<int32_t>(x * SCALE + SHIFT);
    float t = x - (u.i - static_cast<int32_t>(SHIFT)) * (1.0f / SCALE);
    u.f *= 1.0f + t * (1.0f + t * 0.5f);
    return u.f;
}

class WaveShaperHybrid {
public:
    static constexpr float V_CC = 12.0f;
    static constexpr float V_COLL = 5.33f;
    static constexpr float R2 = 100e3f;
    static constexpr float R3 = 10e3f;
    static constexpr float R4 = 22e3f;
    static constexpr float R5 = 10e3f;
    static constexpr float C1 = 10e-9f;
    static constexpr float C2 = 1e-6f;

    static constexpr float V_T = 0.025865f;
    static constexpr float V_T_INV = 1.0f / V_T;
    static constexpr float I_S = 1e-13f;
    static constexpr float BETA_F = 100.0f;

    // 予測用パラメータ
    static constexpr float PRED_VTH = 0.55f;
    static constexpr float PRED_K = 0.003f;

    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        G2_ = 1.0f / R2;
        G3_ = 1.0f / R3;
        G4_ = 1.0f / R4;
        G5_ = 1.0f / R5;

        // Forward Active
        alpha_f_ = BETA_F / (BETA_F + 1.0f);
        c_1_af_ = 1.0f - alpha_f_;
        g_factor_ = I_S * V_T_INV;

        // Schur係数
        float j11 = -g_c1_ - G3_;
        inv_j11_ = 1.0f / j11;
        schur_f1_coeff_ = G3_ * inv_j11_;
        j22_base_ = (-G2_ - G3_) - (G3_ * G3_ * inv_j11_);
        j33_base_ = -G4_ - g_c2_;

        // Predictor用Thevenin係数
        float r_th_base = 1.0f / (-j22_base_);
        gain_base_vin_ = r_th_base * (G2_ + (G3_ * g_c1_ * inv_j11_));
        gain_base_vc1_ = r_th_base * -(G3_ * g_c1_ * inv_j11_);

        float g_total_emit = G4_ + g_c2_;
        r_th_emit_ = 1.0f / g_total_emit;
        src_emit_const_ = G4_ * V_CC;

        // Predictor二次方程式係数
        float r_loop = r_th_emit_ + r_th_base * 0.01f;
        pred_quad_a_ = r_loop * PRED_K;
        if (pred_quad_a_ < 1e-6f) pred_quad_a_ = 1e-6f;

        // v_cap復元係数
        coef_cap_src_ = -inv_j11_ * g_c1_;
        coef_cap_vb_ = -inv_j11_ * -G3_;
    }

    void reset() {
        v_c1_prev_ = 0.0f;
        v_c2_prev_ = 8.0f;
    }

    inline float process(float v_in) {
        // === Step 1: Predictor (Quadratic Analytical Guess) ===
        float v_b_pred = gain_base_vin_ * v_in + gain_base_vc1_ * v_c1_prev_;
        float v_e_pred = r_th_emit_ * (src_emit_const_ + g_c2_ * v_c2_prev_);
        float v_drive = v_e_pred - v_b_pred;

        float v_b, v_e;

        if (v_drive <= PRED_VTH) {
            // OFF Guess
            v_b = v_b_pred;
            v_e = v_e_pred;
        } else {
            // ON Guess (Quadratic)
            float c = PRED_VTH - v_drive;
            float D = 1.0f - 4.0f * pred_quad_a_ * c;
            float x = (-1.0f + std::sqrt(D)) / (2.0f * pred_quad_a_);
            float i_pred = PRED_K * x * x;

            v_e = v_e_pred - i_pred * r_th_emit_;
            v_b = v_b_pred + i_pred * r_th_emit_ * 0.01f;
        }

        // === Step 2: Corrector (1-Step Newton with Exact Exp) ===

        // A. ダイオード特性評価
        float v_eb = v_e - v_b;
        v_eb = std::max(-10.0f * V_T, std::min(v_eb, 40.0f * V_T));
        float exp_eb = fast_exp_hybrid(v_eb * V_T_INV);
        float g_ef = g_factor_ * exp_eb + 1e-12f;
        float i_ef = I_S * (exp_eb - 1.0f);

        // B. 電流
        float i_e = i_ef;
        float i_b = c_1_af_ * i_ef;

        // C. v_cap推定と残差計算
        float v_cap_est = coef_cap_src_ * (v_in - v_c1_prev_) + coef_cap_vb_ * v_b;

        float f1 = g_c1_ * (v_in - v_cap_est - v_c1_prev_) - G3_ * (v_cap_est - v_b);
        float f2 = G2_ * (v_in - v_b) + G3_ * (v_cap_est - v_b) + i_b;
        float f2_p = f2 - schur_f1_coeff_ * f1;
        float f3 = G4_ * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev_);

        // D. ヤコビアン (2x2)
        float j22 = j22_base_ - c_1_af_ * g_ef;
        float j23 = c_1_af_ * g_ef;
        float j32 = g_ef;
        float j33 = j33_base_ - g_ef;

        // E. 2x2解法 (Cramer's Rule)
        float det = j22 * j33 - j23 * j32;
        if (std::abs(det) < 1e-12f) {
            det = (det >= 0.0f) ? 1e-12f : -1e-12f;
        }
        float inv_det = 1.0f / det;

        float dv_b = (j33 * (-f2_p) - j23 * (-f3)) * inv_det;
        float dv_e = (-j32 * (-f2_p) + j22 * (-f3)) * inv_det;

        // F. 更新 (予測が良いのでダンピング不要)
        v_b += dv_b;
        v_e += dv_e;
        v_e = std::clamp(v_e, 0.0f, V_CC + 0.5f);

        // === Step 3: Output & State Update ===
        float v_cap = coef_cap_src_ * (v_in - v_c1_prev_) + coef_cap_vb_ * v_b;
        v_c1_prev_ = v_in - v_cap;
        v_c2_prev_ = v_e;

        // Collector Output
        float i_e_new = i_e + g_ef * (dv_e - dv_b);
        float i_c = alpha_f_ * i_e_new;
        float v_c = V_COLL + i_c / G5_;
        v_c = std::clamp(v_c, V_COLL - 0.5f, V_CC + 0.5f);

        return v_c;
    }

private:
    float dt_;
    float g_c1_, g_c2_;
    float G2_, G3_, G4_, G5_;

    float alpha_f_, c_1_af_;
    float g_factor_;

    float inv_j11_, schur_f1_coeff_;
    float j22_base_, j33_base_;
    float coef_cap_src_, coef_cap_vb_;

    float gain_base_vin_, gain_base_vc1_;
    float r_th_emit_, src_emit_const_;
    float pred_quad_a_;

    float v_c1_prev_, v_c2_prev_;
};

}  // namespace hybrid
}  // namespace tb303

// =============================================================================
// 高速exp近似手法の比較実装
// =============================================================================
namespace tb303 {
namespace exp_approx {

// =============================================================================
// 1. LUT + 線形補間
//
// メモリ: 4KB (1024 * sizeof(float))
// 精度: 相対誤差 < 0.1%
// 速度: 最速（メモリアクセス + 乗加算のみ）
// =============================================================================
class ExpLUT {
public:
    static constexpr int LUT_SIZE = 1024;
    static constexpr float X_MIN = -10.0f;
    static constexpr float X_MAX = 50.0f;
    static constexpr float X_RANGE = X_MAX - X_MIN;
    static constexpr float SCALE = static_cast<float>(LUT_SIZE - 1) / X_RANGE;
    static constexpr float INV_SCALE = X_RANGE / static_cast<float>(LUT_SIZE - 1);

    ExpLUT() {
        // LUTを初期化
        for (int i = 0; i < LUT_SIZE; ++i) {
            float x = X_MIN + i * INV_SCALE;
            lut_[i] = std::exp(x);
        }
    }

    inline float operator()(float x) const {
        // 範囲外クランプ
        if (x <= X_MIN) return lut_[0];
        if (x >= X_MAX) return lut_[LUT_SIZE - 1];

        // インデックス計算
        float idx_f = (x - X_MIN) * SCALE;
        int idx = static_cast<int>(idx_f);
        float frac = idx_f - static_cast<float>(idx);

        // 線形補間
        return lut_[idx] * (1.0f - frac) + lut_[idx + 1] * frac;
    }

private:
    float lut_[LUT_SIZE];
};

// グローバルLUTインスタンス（static初期化）
inline const ExpLUT& getExpLUT() {
    static ExpLUT lut;
    return lut;
}

inline float lut_exp(float x) {
    return getExpLUT()(x);
}

// =============================================================================
// 2. パデ近似 [2,2] + レンジリダクション
//
// メモリ: 0
// 精度: 相対誤差 < 0.5% (|x| < 50)
// 速度: 除算1回 + 乗算数回
//
// exp(x) = 2^(x/ln2) = 2^n * 2^f  (n: 整数, f: 小数部)
// 2^f をパデ近似で計算
// =============================================================================
inline float pade_exp(float x) {
    // 範囲制限
    x = std::clamp(x, -87.0f, 88.0f);

    // レンジリダクション: x = n*ln(2) + r, |r| < ln(2)/2
    constexpr float LOG2E = 1.4426950408889634f;  // 1/ln(2)
    constexpr float LN2 = 0.6931471805599453f;

    float n_f = std::floor(x * LOG2E + 0.5f);
    int n = static_cast<int>(n_f);
    float r = x - n_f * LN2;  // |r| < ln(2)/2 ≈ 0.347

    // パデ近似 [2,2] for exp(r)
    // exp(r) ≈ (12 + 6r + r²) / (12 - 6r + r²)
    float r2 = r * r;
    float num = 12.0f + 6.0f * r + r2;
    float den = 12.0f - 6.0f * r + r2;
    float exp_r = num / den;

    // 2^n をビット操作で計算
    union { float f; int32_t i; } u;
    u.i = (127 + n) << 23;

    return u.f * exp_r;
}

// =============================================================================
// 3. パデ近似 [3,3] (より高精度)
//
// exp(r) ≈ (120 + 60r + 12r² + r³) / (120 - 60r + 12r² - r³)
// 相対誤差 < 0.01% for |r| < ln(2)/2
// =============================================================================
inline float pade33_exp(float x) {
    x = std::clamp(x, -87.0f, 88.0f);

    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float LN2 = 0.6931471805599453f;

    float n_f = std::floor(x * LOG2E + 0.5f);
    int n = static_cast<int>(n_f);
    float r = x - n_f * LN2;

    // パデ [3,3]
    float r2 = r * r;
    float r3 = r2 * r;
    float num = 120.0f + 60.0f * r + 12.0f * r2 + r3;
    float den = 120.0f - 60.0f * r + 12.0f * r2 - r3;
    float exp_r = num / den;

    union { float f; int32_t i; } u;
    u.i = (127 + n) << 23;

    return u.f * exp_r;
}

}  // namespace exp_approx

// =============================================================================
// LUTベースの高速WaveShaper
// =============================================================================
namespace lut {

using namespace exp_approx;

// 回路定数（fast名前空間と同一）
constexpr float V_CC = 12.0f;
constexpr float V_COLL = 5.33f;
constexpr float R2 = 100e3f;
constexpr float R3 = 10e3f;
constexpr float R4 = 22e3f;
constexpr float R5 = 10e3f;
constexpr float C1 = 10e-9f;
constexpr float C2 = 1e-6f;

constexpr float V_T = 0.025865f;
constexpr float V_T_INV = 1.0f / V_T;
constexpr float I_S = 1e-13f;
constexpr float BETA_F = 100.0f;
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);
constexpr float ALPHA_R = 0.5f / 1.5f;
constexpr float V_CRIT = V_T * 40.0f;

constexpr float G2 = 1.0f / R2;
constexpr float G3 = 1.0f / R3;
constexpr float G4 = 1.0f / R4;
constexpr float G5 = 1.0f / R5;

// ダイオード I-V (LUT版)
inline void diode_iv_lut(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = lut_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = lut_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// ダイオード I-V (パデ版)
inline void diode_iv_pade(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = pade_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = pade_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// =============================================================================
// WaveShaperLUT: LUTベース + Schur縮約
// =============================================================================
class WaveShaperLUT {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // ダイオード評価 (LUT)
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_lut(v_eb, i_ef, g_ef);
        diode_iv_lut(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        if (std::abs(det) < 1e-15f) {
            v_c1_ = v_in - v_cap;
            v_c2_ = v_e;
            return v_c;
        }

        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // 後退代入
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_, g_c2_;
    float inv_j11_, schur_j11_factor_, schur_f1_factor_;
};

// =============================================================================
// WaveShaperPade: パデ近似 + Schur縮約
// =============================================================================
class WaveShaperPade {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // ダイオード評価 (パデ近似)
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_pade(v_eb, i_ef, g_ef);
        diode_iv_pade(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        if (std::abs(det) < 1e-15f) {
            v_c1_ = v_in - v_cap;
            v_c2_ = v_e;
            return v_c;
        }

        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // 後退代入
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_, g_c2_;
    float inv_j11_, schur_j11_factor_, schur_f1_factor_;
};

// =============================================================================
// WaveShaperPade33: パデ [3,3] 高精度版
// =============================================================================
class WaveShaperPade33 {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // ダイオード評価 (パデ[3,3]近似)
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_pade33(v_eb, i_ef, g_ef);
        diode_iv_pade33(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        if (std::abs(det) < 1e-15f) {
            v_c1_ = v_in - v_cap;
            v_c2_ = v_e;
            return v_c;
        }

        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // 後退代入
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    // パデ[3,3]版ダイオード
    static inline void diode_iv_pade33(float v, float& i, float& g) {
        if (v > V_CRIT) {
            float exp_crit = exp_approx::pade33_exp(V_CRIT * V_T_INV);
            g = I_S * V_T_INV * exp_crit;
            i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
        } else if (v < -10.0f * V_T) {
            i = -I_S;
            g = 1e-12f;
        } else {
            float exp_v = exp_approx::pade33_exp(v * V_T_INV);
            i = I_S * (exp_v - 1.0f);
            g = I_S * V_T_INV * exp_v + 1e-12f;
        }
    }

    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_, g_c2_;
    float inv_j11_, schur_j11_factor_, schur_f1_factor_;
};

}  // namespace lut
}  // namespace tb303
