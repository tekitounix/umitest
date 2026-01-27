#!/usr/bin/env python3
"""
Python実装とC++実装の出力比較

C++と同じアルゴリズム（ダンピング0.1）を使用して精度を検証
"""

import numpy as np
import os

SAMPLE_RATE = 48000.0
DT = 1.0 / SAMPLE_RATE

# 回路定数
V_CC = 12.0
V_COLL = 5.33
R1, R2, R3, R4, R5 = 10e3, 100e3, 10e3, 22e3, 10e3
C1, C2 = 10e-9, 1e-6
V_T = 0.025865
I_S = 1e-13
BETA_F = 100.0
ALPHA_F = BETA_F / (BETA_F + 1.0)
ALPHA_R = 0.5 / 1.5


class WaveShaperReference:
    """リファレンス（100反復）"""
    def __init__(self):
        self.reset()

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = 1/R2, 1/R3, 1/R4, 1/R5
        g_c1, g_c2 = C1/DT, C2/DT
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(100):
            v_eb, v_cb = v_e - v_b, v_c - v_b
            v_crit = V_T * 40.0

            if v_eb > v_crit:
                i_ef = I_S * (np.exp(v_crit/V_T) - 1) + I_S/V_T * np.exp(v_crit/V_T) * (v_eb - v_crit)
                g_ef = I_S / V_T * np.exp(v_crit / V_T)
            elif v_eb < -10 * V_T:
                i_ef, g_ef = -I_S, 1e-12
            else:
                i_ef = I_S * (np.exp(v_eb/V_T) - 1)
                g_ef = I_S / V_T * np.exp(v_eb/V_T) + 1e-12

            if v_cb > v_crit:
                i_cr = I_S * (np.exp(v_crit/V_T) - 1) + I_S/V_T * np.exp(v_crit/V_T) * (v_cb - v_crit)
                g_cr = I_S / V_T * np.exp(v_crit / V_T)
            elif v_cb < -10 * V_T:
                i_cr, g_cr = -I_S, 1e-12
            else:
                i_cr = I_S * (np.exp(v_cb/V_T) - 1)
                g_cr = I_S / V_T * np.exp(v_cb/V_T) + 1e-12

            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1*(v_in - v_cap - v_c1_prev) - g3*(v_cap - v_b)
            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(V_COLL - v_c) + i_c

            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-12:
                break

            J = np.array([
                [-g_c1-g3, g3, 0, 0],
                [g3, -g2-g3-(1-ALPHA_F)*g_ef-(1-ALPHA_R)*g_cr, (1-ALPHA_F)*g_ef, (1-ALPHA_R)*g_cr],
                [0, g_ef-ALPHA_R*g_cr, -g4-g_ef-g_c2, ALPHA_R*g_cr],
                [0, -ALPHA_F*g_ef+g_cr, ALPHA_F*g_ef, -g5-g_cr]
            ])
            b = np.array([-f1, -f2, -f3, -f4])
            try:
                dv = np.linalg.solve(J, b)
            except:
                break
            max_dv = np.max(np.abs(dv))
            damp = min(1.0, 0.1/max_dv) if max_dv > 0.1 else 1.0
            v_cap += damp*dv[0]
            v_b += damp*dv[1]
            v_e = np.clip(v_e + damp*dv[2], 0, V_CC+0.5)
            v_c = np.clip(v_c + damp*dv[3], 0, V_CC+0.5)

        self.v_c1, self.v_c2, self.v_b, self.v_e, self.v_c = v_in-v_cap, v_e, v_b, v_e, v_c
        return v_c


class WaveShaperCppEquivalent:
    """C++実装と同等のPython実装（ダンピング0.1）"""
    def __init__(self, max_iter=5):
        self.max_iter = max_iter
        self.reset()

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = 1/R2, 1/R3, 1/R4, 1/R5
        g_c1, g_c2 = C1/DT, C2/DT
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(self.max_iter):
            v_eb, v_cb = v_e - v_b, v_c - v_b
            v_crit = V_T * 40.0

            if v_eb > v_crit:
                i_ef = I_S * (np.exp(v_crit/V_T) - 1) + I_S/V_T * np.exp(v_crit/V_T) * (v_eb - v_crit)
                g_ef = I_S / V_T * np.exp(v_crit / V_T)
            elif v_eb < -10 * V_T:
                i_ef, g_ef = -I_S, 1e-12
            else:
                i_ef = I_S * (np.exp(v_eb/V_T) - 1)
                g_ef = I_S / V_T * np.exp(v_eb/V_T) + 1e-12

            if v_cb > v_crit:
                i_cr = I_S * (np.exp(v_crit/V_T) - 1) + I_S/V_T * np.exp(v_crit/V_T) * (v_cb - v_crit)
                g_cr = I_S / V_T * np.exp(v_crit / V_T)
            elif v_cb < -10 * V_T:
                i_cr, g_cr = -I_S, 1e-12
            else:
                i_cr = I_S * (np.exp(v_cb/V_T) - 1)
                g_cr = I_S / V_T * np.exp(v_cb/V_T) + 1e-12

            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1*(v_in - v_cap - v_c1_prev) - g3*(v_cap - v_b)
            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(V_COLL - v_c) + i_c

            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-9:
                break

            J = np.array([
                [-g_c1-g3, g3, 0, 0],
                [g3, -g2-g3-(1-ALPHA_F)*g_ef-(1-ALPHA_R)*g_cr, (1-ALPHA_F)*g_ef, (1-ALPHA_R)*g_cr],
                [0, g_ef-ALPHA_R*g_cr, -g4-g_ef-g_c2, ALPHA_R*g_cr],
                [0, -ALPHA_F*g_ef+g_cr, ALPHA_F*g_ef, -g5-g_cr]
            ])
            b = np.array([-f1, -f2, -f3, -f4])
            try:
                dv = np.linalg.solve(J, b)
            except:
                break
            # C++と同じ適応的ダンピング
            max_dv = np.max(np.abs(dv))
            if max_dv > 0.5:
                damp = 0.5 / max_dv
            elif max_dv > 0.1:
                damp = 0.1 / max_dv
            else:
                damp = 1.0
            v_cap += damp*dv[0]
            v_b += damp*dv[1]
            v_e = np.clip(v_e + damp*dv[2], 0, V_CC+0.5)
            v_c = np.clip(v_c + damp*dv[3], 0, V_CC+0.5)

        self.v_c1, self.v_c2, self.v_b, self.v_e, self.v_c = v_in-v_cap, v_e, v_b, v_e, v_c
        return v_c


def generate_sawtooth(freq, duration):
    n = int(SAMPLE_RATE * duration)
    t = np.arange(n) / SAMPLE_RATE
    phase = (freq * t) % 1.0
    return 12.0 - phase * 6.5


# =============================================================================
# SquareShaper (tb_square.hh equivalent - PNP transistor model)
# =============================================================================
class SquareShaper:
    """
    TB-303 Square Wave Shaper
    Models the 2SA733 PNP transistor-based square wave shaping circuit

    C++ benchmark uses input range 0-8V (descending sawtooth from 8 to 0).
    WaveShaper uses input range 5.5-12V (descending sawtooth from 12 to 5.5).
    For comparison, input is scaled and output is scaled back.
    """
    # 2SA733 PNP SPICE model parameters
    Re = 22e3       # External emitter resistance (Ω)
    Rc = 10e3       # External collector resistance (Ω)
    Vcc = 6.67      # Supply voltage (V)
    Is = 55.9e-15   # Reverse saturation current (A)
    NF = 1.01201    # Emission coefficient
    Vt = 0.02585 * NF  # Effective thermal voltage (V)
    beta = 205.0    # hFE
    Ib_max = 66.7e-6  # Maximum base current (A)
    Vce_sat = 0.1   # VCE saturation voltage (V)

    def __init__(self, shape=0.5):
        self.Ve = 0.0
        # shape parameter controls Ce from 0.1µF to 1.0µF
        Ce = 0.1e-6 + (shape * 0.9e-6)
        self.g = DT / Ce

    def reset(self):
        self.Ve = 0.0

    def fast_tanh(self, x):
        return x / (abs(x) + 0.1)

    def process(self, v_in):
        # Scale input from WaveShaper range (12->5.5V descending) to SquareShaper range (8->0V descending)
        # Both are descending sawtooth waves
        # WaveShaper: 12V (peak) -> 5.5V (trough), range 6.5V
        # SquareShaper: 8V (peak) -> 0V (trough), range 8V
        # Linear mapping preserving direction: v_in=12 -> 8, v_in=5.5 -> 0
        v_scaled = (v_in - 5.5) / 6.5 * 8.0

        Veb = self.Ve - v_scaled
        # Clamp exponent to avoid overflow
        exp_arg = np.clip(Veb / self.Vt, -40, 40)
        Ib = min(self.Is * (np.exp(exp_arg) - 1.0), self.Ib_max)
        Ic_ideal = self.beta * Ib
        Ic_lin = self.Ve / self.Rc
        Ic_sat = Ic_lin * self.fast_tanh(self.Ve / self.Vce_sat)
        # Soft saturation
        den_part = 0.1 * max(Ic_sat, 1e-6)
        Ic = (Ic_sat * Ic_ideal) / (abs(Ic_ideal) + den_part)
        Ie = Ib + Ic
        Icharge = (self.Vcc - self.Ve) / self.Re
        self.Ve += self.g * (Icharge - Ie)

        out_raw = self.Rc * Ic
        # Scale output from SquareShaper range (0-3V typical) to WaveShaper range (5.3-8V)
        # SquareShaper native output: ~0V (low) -> ~3V (peak)
        # WaveShaper output: ~5.3V (low) -> ~8V (peak)
        return 5.3 + out_raw * (2.7 / 3.0)


# =============================================================================
# WaveShaperHybridAdaptive: BJT状態適応型コンダクタンス切り替え
# =============================================================================
class WaveShaperHybrid:
    """
    C++ WaveShaperHybridAdaptive と同等の実装:
    - BJT OFF状態（v_eb < 閾値）: 高精度モード - 現在のコンダクタンスを使用
    - BJT ON状態（v_eb >= 閾値）: 高速モード - 前サンプルのコンダクタンスを使用

    OFF→ONの遷移直後は高精度モードを1サンプル維持
    """
    VEB_THRESHOLD = 0.3  # BJT ON/OFF判定閾値

    def __init__(self, veb_threshold=0.3):
        self.veb_threshold = veb_threshold
        self.reset()

        # Schur係数の事前計算
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1 = g_c1
        self.g_c2 = C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL
        self.g_ef_prev = 1e-12
        self.g_cr_prev = 1e-12
        self.bjt_on = False

    def diode_iv(self, v):
        """標準ダイオード I-V"""
        v_crit = V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / V_T)
            i = I_S * (exp_crit - 1) + I_S / V_T * exp_crit * (v - v_crit)
            g = I_S / V_T * exp_crit
        elif v < -10 * V_T:
            i, g = -I_S, 1e-12
        else:
            exp_v = np.exp(v / V_T)
            i = I_S * (exp_v - 1)
            g = I_S / V_T * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        """C++ WaveShaperHybridAdaptive同等のprocess"""
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2

        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b = self.v_b
        v_e = v_c2_prev
        v_c = self.v_c

        # BJT状態の判定
        v_eb = v_e - v_b
        bjt_on_now = (v_eb >= self.VEB_THRESHOLD)

        # モード切り替え: OFF→ONの遷移直後は高精度モードを維持
        use_fast_mode = bjt_on_now and self.bjt_on

        # 両接合を評価
        v_cb = v_c - v_b
        i_ef, g_ef = self.diode_iv(v_eb)
        i_cr, g_cr = self.diode_iv(v_cb)

        # ヤコビアンで使うコンダクタンスを選択
        if use_fast_mode:
            # 高速モード: 前サンプルのコンダクタンス
            g_ef_use = self.g_ef_prev
            g_cr_use = self.g_cr_prev
        else:
            # 高精度モード: 現在のコンダクタンス
            g_ef_use = g_ef
            g_cr_use = g_cr

        # Ebers-Moll電流（常に正確な電流）
        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        # KCL残差
        f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
        f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
        f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
        f4 = g5 * (V_COLL - v_c) + i_c

        # ヤコビアン
        j22 = -g2 - g3 - (1 - ALPHA_F) * g_ef_use - (1 - ALPHA_R) * g_cr_use
        j23 = (1 - ALPHA_F) * g_ef_use
        j24 = (1 - ALPHA_R) * g_cr_use
        j32 = g_ef_use - ALPHA_R * g_cr_use
        j33 = -g4 - g_ef_use - g_c2
        j34 = ALPHA_R * g_cr_use
        j42 = -ALPHA_F * g_ef_use + g_cr_use
        j43 = ALPHA_F * g_ef_use
        j44 = -g5 - g_cr_use

        # Schur縮約
        j22_p = j22 - self.schur_j11_factor
        f2_p = f2 - self.schur_f1_factor * f1
        inv_j44 = 1.0 / j44
        j24_inv = j24 * inv_j44
        j34_inv = j34 * inv_j44

        j22_pp = j22_p - j24_inv * j42
        j23_pp = j23 - j24_inv * j43
        f2_pp = f2_p + j24_inv * f4
        j32_pp = j32 - j34_inv * j42
        j33_pp = j33 - j34_inv * j43
        f3_pp = f3 + j34_inv * f4

        # 2x2 Cramer
        det = j22_pp * j33_pp - j23_pp * j32_pp
        inv_det = 1.0 / det
        dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
        dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det

        # 後退代入
        dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
        dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

        # ダンピング
        max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0

        v_cap += damp * dv_cap
        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        # 状態更新
        self.g_ef_prev = g_ef
        self.g_cr_prev = g_cr
        self.bjt_on = bjt_on_now

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b = v_b
        self.v_e = v_e
        self.v_c = v_c

        return v_c


# =============================================================================
# Schraudolph exp近似 (IEEE754ビット操作)
# =============================================================================
def schraudolph_exp(x):
    """Schraudolph (1999): IEEE754ビット操作による高速exp近似"""
    if np.isnan(x):
        return 1.0
    x = np.clip(x, -87.0, 88.0)
    # IEEE754 float32のビット操作をNumPyで近似
    # 12102203.0 = (1 << 23) / ln(2)
    # 1064866805 = 127 << 23 - 調整項
    i = int(12102203.0 * x + 1064866805.0)
    # int32をfloat32として解釈
    return np.frombuffer(np.array([i], dtype=np.int32).tobytes(), dtype=np.float32)[0]


def diode_iv_schraudolph(v):
    """Schraudolph exp を使ったダイオード I-V"""
    v_crit = V_T * 40.0
    if v > v_crit:
        exp_crit = schraudolph_exp(v_crit / V_T)
        g = I_S / V_T * exp_crit
        i = I_S * (exp_crit - 1) + g * (v - v_crit)
    elif v < -10 * V_T:
        i, g = -I_S, 1e-12
    else:
        exp_v = schraudolph_exp(v / V_T)
        i = I_S * (exp_v - 1)
        g = I_S / V_T * exp_v + 1e-12
    return i, g


class WaveShaperSchraudolph:
    """Schraudolph純粋exp版 - 最速のexp近似を使用"""
    def __init__(self, n_iter=5):
        self.n_iter = n_iter
        self.reset()
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1 = g_c1
        self.g_c2 = C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(self.n_iter):
            v_eb, v_cb = v_e - v_b, v_c - v_b
            i_ef, g_ef = diode_iv_schraudolph(v_eb)
            i_cr, g_cr = diode_iv_schraudolph(v_cb)
            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
            f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
            f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
            f4 = g5 * (V_COLL - v_c) + i_c

            j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
            j23 = (1-ALPHA_F)*g_ef
            j24 = (1-ALPHA_R)*g_cr
            j32 = g_ef - ALPHA_R*g_cr
            j33 = -g4 - g_ef - g_c2
            j34 = ALPHA_R*g_cr
            j42 = -ALPHA_F*g_ef + g_cr
            j43 = ALPHA_F*g_ef
            j44 = -g5 - g_cr

            j22_p = j22 - self.schur_j11_factor
            f2_p = f2 - self.schur_f1_factor * f1
            inv_j44 = 1.0 / j44
            j24_inv, j34_inv = j24 * inv_j44, j34 * inv_j44
            j22_pp = j22_p - j24_inv * j42
            j23_pp = j23 - j24_inv * j43
            f2_pp = f2_p + j24_inv * f4
            j32_pp = j32 - j34_inv * j42
            j33_pp = j33 - j34_inv * j43
            f3_pp = f3 + j34_inv * f4

            det = j22_pp * j33_pp - j23_pp * j32_pp
            if abs(det) < 1e-15:
                break
            inv_det = 1.0 / det
            dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
            dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det
            dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
            dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

            if any(np.isnan(x) or np.isinf(x) for x in [dv_cap, dv_b, dv_e, dv_c]):
                break

            max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
            damp = 0.5 / max_dv if max_dv > 0.5 else 1.0
            v_cap += damp * dv_cap
            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


# =============================================================================
# PWL (区分線形) ダイオード
# =============================================================================
def diode_iv_pwl(v):
    """3セグメントPWLダイオード (exp()を完全に排除)"""
    V_ON, V_KNEE = 0.5, 0.3
    G_ON, G_OFF = 0.1, 1e-9  # G_OFFを少し大きくして数値安定性向上
    if v < V_KNEE:
        i, g = G_OFF * v, G_OFF
    elif v < V_ON:
        t = (v - V_KNEE) / (V_ON - V_KNEE)
        g = G_OFF + t * t * (G_ON - G_OFF)
        i = G_OFF * V_KNEE + g * (v - V_KNEE)
    else:
        i = G_OFF * V_KNEE + G_ON * (v - V_KNEE)
        g = G_ON
    return i, max(g, 1e-12)


class WaveShaperPWL:
    """PWLダイオード + 1回Newton - exp()を完全に排除"""
    def __init__(self, n_iter=1):
        self.n_iter = n_iter
        self.reset()
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1, self.g_c2 = g_c1, C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(self.n_iter):
            v_eb, v_cb = v_e - v_b, v_c - v_b
            i_ef, g_ef = diode_iv_pwl(v_eb)
            i_cr, g_cr = diode_iv_pwl(v_cb)
            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
            f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
            f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
            f4 = g5 * (V_COLL - v_c) + i_c

            j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
            j23 = (1-ALPHA_F)*g_ef
            j24 = (1-ALPHA_R)*g_cr
            j32 = g_ef - ALPHA_R*g_cr
            j33 = -g4 - g_ef - g_c2
            j34 = ALPHA_R*g_cr
            j42 = -ALPHA_F*g_ef + g_cr
            j43 = ALPHA_F*g_ef
            j44 = -g5 - g_cr

            j22_p = j22 - self.schur_j11_factor
            f2_p = f2 - self.schur_f1_factor * f1
            inv_j44 = 1.0 / j44
            j24_inv, j34_inv = j24 * inv_j44, j34 * inv_j44
            j22_pp = j22_p - j24_inv * j42
            j23_pp = j23 - j24_inv * j43
            f2_pp = f2_p + j24_inv * f4
            j32_pp = j32 - j34_inv * j42
            j33_pp = j33 - j34_inv * j43
            f3_pp = f3 + j34_inv * f4

            det = j22_pp * j33_pp - j23_pp * j32_pp
            if abs(det) < 1e-15:
                break
            inv_det = 1.0 / det
            dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
            dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det
            dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
            dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

            if any(np.isnan(x) or np.isinf(x) for x in [dv_cap, dv_b, dv_e, dv_c]):
                break

            max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
            damp = 0.5 / max_dv if max_dv > 0.5 else 1.0
            v_cap += damp * dv_cap
            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


# =============================================================================
# VCCS (電圧制御電流源) 簡略モデル
# =============================================================================
class WaveShaperVCCS:
    """線形化VCCS BJTモデル - exp()を完全に排除、反復も不要"""
    VBE_ON = 0.6
    IC_Q = 0.5e-3
    GM = IC_Q / V_T  # ~19.3mS
    RO = 100e3
    GO = 1.0 / RO

    def __init__(self):
        self.reset()
        self.g_c1 = C1 / DT
        self.g_c2 = C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, 1/R3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0

    def process(self, v_in):
        v_cap = v_in - self.v_c1
        v_b = (v_cap * self.g3 + V_CC * self.g2) / (self.g2 + self.g3)
        v_e = self.v_c2
        v_be = v_b - v_e
        v_drive = v_be - self.VBE_ON
        i_c = np.clip(self.GM * v_drive, 0, 2e-3)
        i_b = i_c / BETA_F
        i_e = i_b + i_c

        i_r3 = self.g3 * (v_cap - v_b)
        self.v_c1 += DT / C1 * i_r3
        i_r4 = self.g4 * (V_CC - v_e)
        self.v_c2 += DT / C2 * (i_r4 - i_e)
        self.v_c2 = np.clip(self.v_c2, 0, V_CC)
        v_c = V_COLL - R5 * i_c
        return np.clip(v_c, 0, V_CC)


# =============================================================================
# OneIter: 1回Newton + 前サンプル勾配再利用
# =============================================================================
class WaveShaperOneIter:
    """1回Newton反復 + 前サンプルのコンダクタンス再利用"""
    def __init__(self):
        self.reset()
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1, self.g_c2 = g_c1, C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL
        self.g_ef_prev, self.g_cr_prev = 1e-12, 1e-12

    def diode_iv(self, v):
        v_crit = V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / V_T)
            i = I_S * (exp_crit - 1) + I_S / V_T * exp_crit * (v - v_crit)
            g = I_S / V_T * exp_crit
        elif v < -10 * V_T:
            i, g = -I_S, 1e-12
        else:
            exp_v = np.exp(v / V_T)
            i = I_S * (exp_v - 1)
            g = I_S / V_T * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        v_eb, v_cb = v_e - v_b, v_c - v_b
        i_ef, g_ef = self.diode_iv(v_eb)
        i_cr, g_cr = self.diode_iv(v_cb)

        # ヤコビアンには前サンプルのコンダクタンスを使用
        g_ef_use, g_cr_use = self.g_ef_prev, self.g_cr_prev

        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
        f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
        f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
        f4 = g5 * (V_COLL - v_c) + i_c

        j22 = -g2 - g3 - (1-ALPHA_F)*g_ef_use - (1-ALPHA_R)*g_cr_use
        j23 = (1-ALPHA_F)*g_ef_use
        j24 = (1-ALPHA_R)*g_cr_use
        j32 = g_ef_use - ALPHA_R*g_cr_use
        j33 = -g4 - g_ef_use - g_c2
        j34 = ALPHA_R*g_cr_use
        j42 = -ALPHA_F*g_ef_use + g_cr_use
        j43 = ALPHA_F*g_ef_use
        j44 = -g5 - g_cr_use

        j22_p = j22 - self.schur_j11_factor
        f2_p = f2 - self.schur_f1_factor * f1
        inv_j44 = 1.0 / j44
        j24_inv, j34_inv = j24 * inv_j44, j34 * inv_j44
        j22_pp = j22_p - j24_inv * j42
        j23_pp = j23 - j24_inv * j43
        f2_pp = f2_p + j24_inv * f4
        j32_pp = j32 - j34_inv * j42
        j33_pp = j33 - j34_inv * j43
        f3_pp = f3 + j34_inv * f4

        det = j22_pp * j33_pp - j23_pp * j32_pp
        inv_det = 1.0 / det
        dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
        dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det
        dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
        dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

        max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0
        v_cap += damp * dv_cap
        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        # コンダクタンスを保存
        self.g_ef_prev, self.g_cr_prev = g_ef, g_cr
        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


# =============================================================================
# TwoIter: 2回Newton反復
# =============================================================================
class WaveShaperTwoIter:
    """2回Newton反復 - 精度重視"""
    def __init__(self):
        self.reset()
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1, self.g_c2 = g_c1, C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def diode_iv(self, v):
        v_crit = V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / V_T)
            i = I_S * (exp_crit - 1) + I_S / V_T * exp_crit * (v - v_crit)
            g = I_S / V_T * exp_crit
        elif v < -10 * V_T:
            i, g = -I_S, 1e-12
        else:
            exp_v = np.exp(v / V_T)
            i = I_S * (exp_v - 1)
            g = I_S / V_T * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(2):  # 2回反復
            v_eb, v_cb = v_e - v_b, v_c - v_b
            i_ef, g_ef = self.diode_iv(v_eb)
            i_cr, g_cr = self.diode_iv(v_cb)
            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
            f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
            f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
            f4 = g5 * (V_COLL - v_c) + i_c

            j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
            j23 = (1-ALPHA_F)*g_ef
            j24 = (1-ALPHA_R)*g_cr
            j32 = g_ef - ALPHA_R*g_cr
            j33 = -g4 - g_ef - g_c2
            j34 = ALPHA_R*g_cr
            j42 = -ALPHA_F*g_ef + g_cr
            j43 = ALPHA_F*g_ef
            j44 = -g5 - g_cr

            j22_p = j22 - self.schur_j11_factor
            f2_p = f2 - self.schur_f1_factor * f1
            inv_j44 = 1.0 / j44
            j24_inv, j34_inv = j24 * inv_j44, j34 * inv_j44
            j22_pp = j22_p - j24_inv * j42
            j23_pp = j23 - j24_inv * j43
            f2_pp = f2_p + j24_inv * f4
            j32_pp = j32 - j34_inv * j42
            j33_pp = j33 - j34_inv * j43
            f3_pp = f3 + j34_inv * f4

            det = j22_pp * j33_pp - j23_pp * j32_pp
            inv_det = 1.0 / det
            dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
            dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det
            dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
            dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

            max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
            damp = 0.5 / max_dv if max_dv > 0.5 else 1.0
            v_cap += damp * dv_cap
            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


# =============================================================================
# Wright Omega (Fukushima-style)
# =============================================================================
def omega3(x):
    """Wright Omega polynomial approximation (DAFx2019 style)"""
    if x < -3.341459552768620:
        return np.exp(x)
    elif x < 8.0:
        y = x + 1.0
        return 0.6314 + y * (0.3632 + y * (0.04776 + y * (-0.001314)))
    else:
        return x - np.log(x)


def omega4(x):
    """Wright Omega with Newton-Raphson correction"""
    w = omega3(x)
    lnw = np.log(max(w, 1e-10))
    r = x - w - lnw
    return w * (1.0 + r / (1.0 + w))


def omega_fast2(x):
    """Wright Omega with 2-step Newton-Raphson correction (C++ equivalent)"""
    # Initial approximation (omega3)
    w = omega3(x)
    # Newton correction 1
    lnw = np.log(max(w, 1e-10))
    r = x - w - lnw
    w = w * (1.0 + r / (1.0 + w))
    # Newton correction 2
    lnw = np.log(max(w, 1e-10))
    r = x - w - lnw
    w = w * (1.0 + r / (1.0 + w))
    return w


def diode_iv_omega3(v):
    """
    ダイオード I-V using omega3 approximation (Newton補正なし)

    Wright Omega関数を使ってexp(V/V_T)を近似計算：
    omega(x) = W(e^x) where W is Lambert W function
    exp(x) = W * exp(W) when W = omega(x)

    I = I_S * (exp(V/V_T) - 1)
    """
    v_crit = V_T * 30.0

    if v > v_crit:
        # Linear extrapolation for large v
        i = I_S * (np.exp(v_crit/V_T) - 1) + I_S/V_T * np.exp(v_crit/V_T) * (v - v_crit)
        g = I_S / V_T * np.exp(v_crit / V_T)
        return i, g
    elif v < -10 * V_T:
        return -I_S, 1e-12

    # Use omega3 to compute exp
    x = v / V_T
    w = omega3(x)
    # exp(x) = w * exp(w)
    exp_v = w * np.exp(w)

    i = I_S * (exp_v - 1)
    g = I_S / V_T * exp_v + 1e-12
    return i, g


def diode_iv_lambertw(v):
    """
    ダイオード I-V using omega4 (Lambert W with Newton correction)

    Wright Omega関数を使ってexp(V/V_T)を近似計算：
    omega4(x) = W(e^x) with 4th-order accuracy

    exp(x) = W * exp(W) when W = omega4(x)

    I = I_S * (exp(V/V_T) - 1)
    """
    v_crit = V_T * 30.0

    if v > v_crit:
        # Linear extrapolation for large v
        i = I_S * (np.exp(v_crit/V_T) - 1) + I_S/V_T * np.exp(v_crit/V_T) * (v - v_crit)
        g = I_S / V_T * np.exp(v_crit / V_T)
        return i, g
    elif v < -10 * V_T:
        return -I_S, 1e-12

    # Use omega4 to compute exp
    x = v / V_T
    w = omega4(x)
    # exp(x) = w * exp(w)
    exp_v = w * np.exp(w)

    i = I_S * (exp_v - 1)
    g = I_S / V_T * exp_v + 1e-12
    return i, g


class WaveShaperSchurLambertW:
    """Schur complement + Lambert W (omega_fast2) - C++実装と同等"""
    def __init__(self, max_iter=2):
        self.max_iter = max_iter
        self.reset()
        # Schur係数の事前計算
        g_c1 = C1 / DT
        j11 = -g_c1 - 1/R3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = (1/R3)**2 * self.inv_j11
        self.schur_f1_factor = (1/R3) * self.inv_j11

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL
        self.prev_v_in = 8.0

    def process(self, v_in):
        g2, g3, g4, g5 = 1/R2, 1/R3, 1/R4, 1/R5
        g_c1, g_c2 = C1/DT, C2/DT
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        # 適応的反復：入力変化が大きい場合は反復を増やす
        dv_in = abs(v_in - self.prev_v_in)
        if dv_in > 0.5:
            actual_iter = max(self.max_iter, 5)  # 大きな遷移時は最低5反復
        else:
            actual_iter = self.max_iter
        self.prev_v_in = v_in

        for _ in range(actual_iter):
            # Lambert W によるダイオード評価
            v_eb, v_cb = v_e - v_b, v_c - v_b
            i_ef, g_ef = diode_iv_lambertw(v_eb)
            i_cr, g_cr = diode_iv_lambertw(v_cb)

            # Ebers-Moll電流
            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            # KCL残差
            f1 = g_c1*(v_in - v_cap - v_c1_prev) - g3*(v_cap - v_b)
            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(V_COLL - v_c) + i_c

            # 収束判定
            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-9:
                break

            # ヤコビアン
            j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
            j23 = (1-ALPHA_F)*g_ef
            j24 = (1-ALPHA_R)*g_cr
            j32 = g_ef - ALPHA_R*g_cr
            j33 = -g4 - g_ef - g_c2
            j34 = ALPHA_R*g_cr
            j42 = -ALPHA_F*g_ef + g_cr
            j43 = ALPHA_F*g_ef
            j44 = -g5 - g_cr

            # Schur縮約
            j22_p = j22 - self.schur_j11_factor
            f2_p = f2 - self.schur_f1_factor * f1

            inv_j44 = 1.0 / j44
            j24_inv = j24 * inv_j44
            j34_inv = j34 * inv_j44

            j22_pp = j22_p - j24_inv * j42
            j23_pp = j23 - j24_inv * j43
            f2_pp = f2_p + j24_inv * f4

            j32_pp = j32 - j34_inv * j42
            j33_pp = j33 - j34_inv * j43
            f3_pp = f3 + j34_inv * f4

            # 2x2 Cramer
            det = j22_pp * j33_pp - j23_pp * j32_pp
            inv_det = 1.0 / det

            dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
            dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det

            # 後退代入
            dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
            dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

            # ダンピング
            max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
            if max_dv > 0.5:
                damp = 0.5 / max_dv
            elif max_dv > 0.1:
                damp = 0.1 / max_dv
            else:
                damp = 1.0

            v_cap += damp * dv_cap
            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b = v_b
        self.v_e = v_e
        self.v_c = v_c

        return v_c


class WaveShaperSchurLambertWAdaptive:
    """Schur complement + Lambert W with adaptive iteration for ripple-free output"""
    def __init__(self, base_iter=2):
        self.base_iter = base_iter
        self.reset()
        # Schur係数の事前計算
        g_c1 = C1 / DT
        j11 = -g_c1 - 1/R3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = (1/R3)**2 * self.inv_j11
        self.schur_f1_factor = (1/R3) * self.inv_j11

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL
        self.prev_v_in = 8.0

    def process(self, v_in):
        g2, g3, g4, g5 = 1/R2, 1/R3, 1/R4, 1/R5
        g_c1, g_c2 = C1/DT, C2/DT
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        # 適応的反復：残差ベースで収束するまで
        max_iter = 20  # 安全上限

        for iteration in range(max_iter):
            # Lambert W によるダイオード評価
            v_eb, v_cb = v_e - v_b, v_c - v_b
            i_ef, g_ef = diode_iv_lambertw(v_eb)
            i_cr, g_cr = diode_iv_lambertw(v_cb)

            # Ebers-Moll電流
            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            # KCL残差
            f1 = g_c1*(v_in - v_cap - v_c1_prev) - g3*(v_cap - v_b)
            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(V_COLL - v_c) + i_c

            residual = abs(f1) + abs(f2) + abs(f3) + abs(f4)

            # 厳しい収束判定
            if residual < 1e-10:
                break

            # ヤコビアン
            j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
            j23 = (1-ALPHA_F)*g_ef
            j24 = (1-ALPHA_R)*g_cr
            j32 = g_ef - ALPHA_R*g_cr
            j33 = -g4 - g_ef - g_c2
            j34 = ALPHA_R*g_cr
            j42 = -ALPHA_F*g_ef + g_cr
            j43 = ALPHA_F*g_ef
            j44 = -g5 - g_cr

            # Schur縮約
            j22_p = j22 - self.schur_j11_factor
            f2_p = f2 - self.schur_f1_factor * f1

            inv_j44 = 1.0 / j44
            j24_inv = j24 * inv_j44
            j34_inv = j34 * inv_j44

            j22_pp = j22_p - j24_inv * j42
            j23_pp = j23 - j24_inv * j43
            f2_pp = f2_p + j24_inv * f4

            j32_pp = j32 - j34_inv * j42
            j33_pp = j33 - j34_inv * j43
            f3_pp = f3 + j34_inv * f4

            # 2x2 Cramer
            det = j22_pp * j33_pp - j23_pp * j32_pp
            inv_det = 1.0 / det

            dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
            dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det

            # 後退代入
            dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
            dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

            # ダンピング（より保守的）
            max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
            if max_dv > 0.5:
                damp = 0.5 / max_dv
            elif max_dv > 0.1:
                damp = 0.1 / max_dv
            else:
                damp = 1.0

            v_cap += damp * dv_cap
            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b = v_b
        self.v_e = v_e
        self.v_c = v_c
        self.prev_v_in = v_in

        return v_c


def fast_exp_poly(x):
    """
    高速 exp 近似（範囲分割 + 多項式）

    アイデア: exp(x) = 2^(x/ln2) = 2^n * 2^f  (n=整数, 0<=f<1)
    2^f を多項式近似
    """
    if x > 20.0:
        return 4.85e8  # exp(20)
    elif x < -20.0:
        return 0.0

    # x/ln(2) を整数部と小数部に分離
    ln2_inv = 1.4426950408889634  # 1/ln(2)
    y = x * ln2_inv
    n = int(y)
    if y < 0:
        n -= 1
    f = y - n  # 0 <= f < 1

    # 2^f を多項式近似 (Horner形式)
    # 2^f ≈ 1 + f*ln2 + (f*ln2)²/2 + ...
    # より精度の高い近似: 2^f ≈ 1 + 0.6931472*f + 0.2402265*f² + 0.0555041*f³
    f2 = f * f
    pow2_f = 1.0 + f * (0.6931472 + f * (0.2402265 + f * 0.0555041))

    # 2^n を計算（ビットシフト相当）
    if n >= 0:
        pow2_n = float(1 << min(n, 30))
    else:
        pow2_n = 1.0 / float(1 << min(-n, 30))

    return pow2_n * pow2_f


def diode_iv_fast(v):
    """
    高速ダイオード I-V（fast_exp_poly使用）
    """
    v_crit = V_T * 30.0

    if v > v_crit:
        exp_crit = fast_exp_poly(v_crit / V_T)
        g = I_S / V_T * exp_crit
        i = I_S * (exp_crit - 1) + g * (v - v_crit)
    elif v < -10 * V_T:
        i = -I_S
        g = 1e-12
    else:
        exp_v = fast_exp_poly(v / V_T)
        i = I_S * (exp_v - 1)
        g = I_S / V_T * exp_v + 1e-12

    return i, g


class WaveShaperDecoupled:
    """
    高速ソルバ - 状態適応型 2x2 + fast_exp

    以前成功したハイブリッドアプローチ:
    - 遷移時（v_eb < 0.4V）: v_b/v_e を 2x2
    - 飽和時（v_eb >= 0.4V）: v_b/v_c を 2x2

    fast_exp_poly で指数関数を高速化
    """
    def __init__(self, n_iter=5):
        self.n_iter = n_iter
        self.reset()
        g_c1 = C1 / DT
        self.g_c1 = g_c1
        self.g_c2 = C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, 1/R3, 1/R4, 1/R5
        self.inv_gc1_g3 = 1.0 / (g_c1 + self.g3)

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2

        v_b = self.v_b
        v_e = v_c2_prev
        v_c = self.v_c

        for iteration in range(self.n_iter):
            # === 両接合を評価（標準exp - 精度優先）===
            v_eb = v_e - v_b
            v_cb = v_c - v_b
            i_ef, g_ef = diode_iv_lambertw(v_eb)
            i_cr, g_cr = diode_iv_lambertw(v_cb)

            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            v_cap = (g_c1*(v_in - v_c1_prev) + g3*v_b) * self.inv_gc1_g3

            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(V_COLL - v_c) + i_c

            # 状態適応
            is_saturated = (v_eb > 0.4)

            if not is_saturated:
                # 遷移: v_b/v_e を 2x2
                dvcap_dvb = g3 * self.inv_gc1_g3
                j22 = -g2 - g3*(1 - dvcap_dvb) - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
                j23 = (1-ALPHA_F)*g_ef
                j32 = g_ef - ALPHA_R*g_cr
                j33 = -g4 - g_ef - g_c2

                det = j22 * j33 - j23 * j32
                if abs(det) > 1e-15:
                    inv_det = 1.0 / det
                    dv_b = (j33 * (-f2) - j23 * (-f3)) * inv_det
                    dv_e = (j22 * (-f3) - j32 * (-f2)) * inv_det
                else:
                    dv_b = -f2 / j22
                    dv_e = -f3 / j33

                j44 = -g5 - g_cr
                dv_c = -f4 / j44
            else:
                # 飽和: v_b/v_c を 2x2
                dvcap_dvb = g3 * self.inv_gc1_g3
                j22 = -g2 - g3*(1 - dvcap_dvb) - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
                j24 = (1-ALPHA_R)*g_cr
                j42 = -ALPHA_F*g_ef + g_cr
                j44 = -g5 - g_cr

                det_bc = j22 * j44 - j24 * j42
                if abs(det_bc) > 1e-15:
                    inv_det = 1.0 / det_bc
                    dv_b = (j44 * (-f2) - j24 * (-f4)) * inv_det
                    dv_c = (j22 * (-f4) - j42 * (-f2)) * inv_det
                else:
                    dv_b = -f2 / j22
                    dv_c = -f4 / j44

                j33 = -g4 - g_ef - g_c2
                dv_e = -f3 / j33

            max_dv = max(abs(dv_b), abs(dv_e), abs(dv_c))
            if max_dv > 0.5:
                damp = 0.5 / max_dv
            elif max_dv > 0.1:
                damp = 0.1 / max_dv
            else:
                damp = 1.0

            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b = v_b
        self.v_e = v_e
        self.v_c = v_c

        return v_c


class WaveShaperOmega3:
    """
    高速ソルバ - omega3 only (Newton補正なし)

    omega4 の Newton 補正を省略することで高速化
    ベンチマーク: omega_fast2 (38 cycles) vs omega3推定 (~25 cycles)
    """
    def __init__(self, n_iter=5):
        self.n_iter = n_iter
        self.reset()
        g_c1 = C1 / DT
        self.g_c1 = g_c1
        self.g_c2 = C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, 1/R3, 1/R4, 1/R5
        self.inv_gc1_g3 = 1.0 / (g_c1 + self.g3)

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2

        v_b = self.v_b
        v_e = v_c2_prev
        v_c = self.v_c

        for iteration in range(self.n_iter):
            # omega3 のみで評価
            v_eb = v_e - v_b
            v_cb = v_c - v_b
            i_ef, g_ef = diode_iv_omega3(v_eb)
            i_cr, g_cr = diode_iv_omega3(v_cb)

            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            v_cap = (g_c1*(v_in - v_c1_prev) + g3*v_b) * self.inv_gc1_g3

            f2 = g2*(v_in - v_b) + g3*(v_cap - v_b) + i_b
            f3 = g4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = g5*(V_COLL - v_c) + i_c

            is_saturated = (v_eb > 0.4)

            if not is_saturated:
                dvcap_dvb = g3 * self.inv_gc1_g3
                j22 = -g2 - g3*(1 - dvcap_dvb) - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
                j23 = (1-ALPHA_F)*g_ef
                j32 = g_ef - ALPHA_R*g_cr
                j33 = -g4 - g_ef - g_c2

                det = j22 * j33 - j23 * j32
                if abs(det) > 1e-15:
                    inv_det = 1.0 / det
                    dv_b = (j33 * (-f2) - j23 * (-f3)) * inv_det
                    dv_e = (j22 * (-f3) - j32 * (-f2)) * inv_det
                else:
                    dv_b = -f2 / j22
                    dv_e = -f3 / j33

                j44 = -g5 - g_cr
                dv_c = -f4 / j44
            else:
                dvcap_dvb = g3 * self.inv_gc1_g3
                j22 = -g2 - g3*(1 - dvcap_dvb) - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
                j24 = (1-ALPHA_R)*g_cr
                j42 = -ALPHA_F*g_ef + g_cr
                j44 = -g5 - g_cr

                det_bc = j22 * j44 - j24 * j42
                if abs(det_bc) > 1e-15:
                    inv_det = 1.0 / det_bc
                    dv_b = (j44 * (-f2) - j24 * (-f4)) * inv_det
                    dv_c = (j22 * (-f4) - j42 * (-f2)) * inv_det
                else:
                    dv_b = -f2 / j22
                    dv_c = -f4 / j44

                j33 = -g4 - g_ef - g_c2
                dv_e = -f3 / j33

            max_dv = max(abs(dv_b), abs(dv_e), abs(dv_c))
            if max_dv > 0.5:
                damp = 0.5 / max_dv
            elif max_dv > 0.1:
                damp = 0.1 / max_dv
            else:
                damp = 1.0

            v_b += damp * dv_b
            v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
            v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b = v_b
        self.v_e = v_e
        self.v_c = v_c

        return v_c


# =============================================================================
# パデ近似 [2,2] + レンジリダクション によるexp近似
# =============================================================================
def pade_exp(x):
    """パデ近似 [2,2] + レンジリダクション"""
    x = np.clip(x, -87.0, 88.0)
    LOG2E = 1.4426950408889634
    LN2 = 0.6931471805599453
    n_f = np.floor(x * LOG2E + 0.5)
    n = int(n_f)
    r = x - n_f * LN2  # |r| < ln(2)/2

    # パデ [2,2]: exp(r) ≈ (12 + 6r + r²) / (12 - 6r + r²)
    r2 = r * r
    num = 12.0 + 6.0 * r + r2
    den = 12.0 - 6.0 * r + r2
    exp_r = num / den

    # 2^n
    return (2.0 ** n) * exp_r


def pade33_exp(x):
    """パデ近似 [3,3] + レンジリダクション (より高精度)"""
    x = np.clip(x, -87.0, 88.0)
    LOG2E = 1.4426950408889634
    LN2 = 0.6931471805599453
    n_f = np.floor(x * LOG2E + 0.5)
    n = int(n_f)
    r = x - n_f * LN2

    # パデ [3,3]: exp(r) ≈ (120 + 60r + 12r² + r³) / (120 - 60r + 12r² - r³)
    r2 = r * r
    r3 = r2 * r
    num = 120.0 + 60.0 * r + 12.0 * r2 + r3
    den = 120.0 - 60.0 * r + 12.0 * r2 - r3
    exp_r = num / den

    return (2.0 ** n) * exp_r


# LUT版（Pythonでは単純に辞書ベースのキャッシュで代用）
class ExpLUT:
    """LUT + 線形補間によるexp近似"""
    def __init__(self, lut_size=1024, x_min=-10.0, x_max=50.0):
        self.lut_size = lut_size
        self.x_min = x_min
        self.x_max = x_max
        self.x_range = x_max - x_min
        self.scale = (lut_size - 1) / self.x_range
        self.inv_scale = self.x_range / (lut_size - 1)
        self.lut = np.array([np.exp(x_min + i * self.inv_scale) for i in range(lut_size)])

    def __call__(self, x):
        if x <= self.x_min:
            return self.lut[0]
        if x >= self.x_max:
            return self.lut[-1]
        idx_f = (x - self.x_min) * self.scale
        idx = int(idx_f)
        frac = idx_f - idx
        return self.lut[idx] * (1.0 - frac) + self.lut[idx + 1] * frac


# グローバルLUTインスタンス
_exp_lut = ExpLUT()


def lut_exp(x):
    """LUT + 線形補間によるexp近似"""
    return _exp_lut(x)


def diode_iv_pade(v):
    """パデ近似版ダイオード I-V"""
    v_crit = V_T * 40.0
    if v > v_crit:
        exp_crit = pade_exp(v_crit / V_T)
        g = I_S / V_T * exp_crit
        i = I_S * (exp_crit - 1) + g * (v - v_crit)
    elif v < -10 * V_T:
        i, g = -I_S, 1e-12
    else:
        exp_v = pade_exp(v / V_T)
        i = I_S * (exp_v - 1)
        g = I_S / V_T * exp_v + 1e-12
    return i, g


def diode_iv_lut(v):
    """LUT版ダイオード I-V"""
    v_crit = V_T * 40.0
    if v > v_crit:
        exp_crit = lut_exp(v_crit / V_T)
        g = I_S / V_T * exp_crit
        i = I_S * (exp_crit - 1) + g * (v - v_crit)
    elif v < -10 * V_T:
        i, g = -I_S, 1e-12
    else:
        exp_v = lut_exp(v / V_T)
        i = I_S * (exp_v - 1)
        g = I_S / V_T * exp_v + 1e-12
    return i, g


class WaveShaperPade:
    """パデ近似 [2,2] + Schur縮約"""
    def __init__(self):
        self.reset()
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1, self.g_c2 = g_c1, C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        v_eb, v_cb = v_e - v_b, v_c - v_b
        i_ef, g_ef = diode_iv_pade(v_eb)
        i_cr, g_cr = diode_iv_pade(v_cb)

        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
        f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
        f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
        f4 = g5 * (V_COLL - v_c) + i_c

        j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
        j23 = (1-ALPHA_F)*g_ef
        j24 = (1-ALPHA_R)*g_cr
        j32 = g_ef - ALPHA_R*g_cr
        j33 = -g4 - g_ef - g_c2
        j34 = ALPHA_R*g_cr
        j42 = -ALPHA_F*g_ef + g_cr
        j43 = ALPHA_F*g_ef
        j44 = -g5 - g_cr

        j22_p = j22 - self.schur_j11_factor
        f2_p = f2 - self.schur_f1_factor * f1
        inv_j44 = 1.0 / j44
        j24_inv, j34_inv = j24 * inv_j44, j34 * inv_j44
        j22_pp = j22_p - j24_inv * j42
        j23_pp = j23 - j24_inv * j43
        f2_pp = f2_p + j24_inv * f4
        j32_pp = j32 - j34_inv * j42
        j33_pp = j33 - j34_inv * j43
        f3_pp = f3 + j34_inv * f4

        det = j22_pp * j33_pp - j23_pp * j32_pp
        if abs(det) < 1e-15:
            self.v_c1 = v_in - v_cap
            self.v_c2 = v_e
            return v_c

        inv_det = 1.0 / det
        dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
        dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det
        dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
        dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

        max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0
        v_cap += damp * dv_cap
        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


class WaveShaperLUT:
    """LUT + 線形補間 + Schur縮約"""
    def __init__(self):
        self.reset()
        g_c1 = C1 / DT
        g3 = 1/R3
        j11 = -g_c1 - g3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = g3 * g3 * self.inv_j11
        self.schur_f1_factor = g3 * self.inv_j11
        self.g_c1, self.g_c2 = g_c1, C2 / DT
        self.g2, self.g3, self.g4, self.g5 = 1/R2, g3, 1/R4, 1/R5

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def process(self, v_in):
        g2, g3, g4, g5 = self.g2, self.g3, self.g4, self.g5
        g_c1, g_c2 = self.g_c1, self.g_c2
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        v_eb, v_cb = v_e - v_b, v_c - v_b
        i_ef, g_ef = diode_iv_lut(v_eb)
        i_cr, g_cr = diode_iv_lut(v_cb)

        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        f1 = g_c1 * (v_in - v_cap - v_c1_prev) - g3 * (v_cap - v_b)
        f2 = g2 * (v_in - v_b) + g3 * (v_cap - v_b) + i_b
        f3 = g4 * (V_CC - v_e) - i_e - g_c2 * (v_e - v_c2_prev)
        f4 = g5 * (V_COLL - v_c) + i_c

        j22 = -g2 - g3 - (1-ALPHA_F)*g_ef - (1-ALPHA_R)*g_cr
        j23 = (1-ALPHA_F)*g_ef
        j24 = (1-ALPHA_R)*g_cr
        j32 = g_ef - ALPHA_R*g_cr
        j33 = -g4 - g_ef - g_c2
        j34 = ALPHA_R*g_cr
        j42 = -ALPHA_F*g_ef + g_cr
        j43 = ALPHA_F*g_ef
        j44 = -g5 - g_cr

        j22_p = j22 - self.schur_j11_factor
        f2_p = f2 - self.schur_f1_factor * f1
        inv_j44 = 1.0 / j44
        j24_inv, j34_inv = j24 * inv_j44, j34 * inv_j44
        j22_pp = j22_p - j24_inv * j42
        j23_pp = j23 - j24_inv * j43
        f2_pp = f2_p + j24_inv * f4
        j32_pp = j32 - j34_inv * j42
        j33_pp = j33 - j34_inv * j43
        f3_pp = f3 + j34_inv * f4

        det = j22_pp * j33_pp - j23_pp * j32_pp
        if abs(det) < 1e-15:
            self.v_c1 = v_in - v_cap
            self.v_c2 = v_e
            return v_c

        inv_det = 1.0 / det
        dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
        dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det
        dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
        dv_cap = (-f1 - g3 * dv_b) * self.inv_j11

        max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0
        v_cap += damp * dv_cap
        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        self.v_c1 = v_in - v_cap
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


def main():
    print('=' * 70)
    print('TB-303 Wave Shaper: Implementation Comparison')
    print('=' * 70)

    v_in = generate_sawtooth(40.0, 0.1)

    # リファレンス
    ref = WaveShaperReference()
    ref_output = np.array([ref.process(v) for v in v_in])

    print('\nComparison with Reference (100 iterations):')
    print(f'{"Method":<35} {"RMS(mV)":>10} {"Max(mV)":>10}')
    print('-' * 60)

    # Newton反復版
    for max_iter in [1, 2, 5]:
        ws = WaveShaperCppEquivalent(max_iter=max_iter)
        output = np.array([ws.process(v) for v in v_in])
        rms = np.sqrt(np.mean((output - ref_output) ** 2)) * 1000
        mx = np.max(np.abs(output - ref_output)) * 1000
        print(f'Schur + Newton {max_iter} iter         {rms:>10.1f} {mx:>10.1f}')

    # SchurLambertW版
    for lw_iter in [1, 2, 3]:
        ws_lw = WaveShaperSchurLambertW(max_iter=lw_iter)
        lw_output = np.array([ws_lw.process(v) for v in v_in])
        rms_lw = np.sqrt(np.mean((lw_output - ref_output) ** 2)) * 1000
        mx_lw = np.max(np.abs(lw_output - ref_output)) * 1000
        print(f'SchurLambertW {lw_iter} iter (omega)     {rms_lw:>10.1f} {mx_lw:>10.1f}')

    # Adaptive版
    ws_adaptive = WaveShaperSchurLambertWAdaptive()
    adp_output = np.array([ws_adaptive.process(v) for v in v_in])
    rms_adp = np.sqrt(np.mean((adp_output - ref_output) ** 2)) * 1000
    mx_adp = np.max(np.abs(adp_output - ref_output)) * 1000
    print(f'SchurLambertW Adaptive             {rms_adp:>10.1f} {mx_adp:>10.1f}')

    # Decoupled版（状態適応型）
    ws_decoupled = WaveShaperDecoupled()
    dec_output = np.array([ws_decoupled.process(v) for v in v_in])
    rms_dec = np.sqrt(np.mean((dec_output - ref_output) ** 2)) * 1000
    mx_dec = np.max(np.abs(dec_output - ref_output)) * 1000
    print(f'Decoupled (hybrid 2x2)             {rms_dec:>10.1f} {mx_dec:>10.1f}')

    # Omega3版（Newton補正なし）
    ws_omega3 = WaveShaperOmega3()
    o3_output = np.array([ws_omega3.process(v) for v in v_in])
    rms_o3 = np.sqrt(np.mean((o3_output - ref_output) ** 2)) * 1000
    mx_o3 = np.max(np.abs(o3_output - ref_output)) * 1000
    print(f'Omega3 only (no Newton)            {rms_o3:>10.1f} {mx_o3:>10.1f}')

    # グラフ出力
    try:
        import matplotlib.pyplot as plt
        import matplotlib
        matplotlib.use('Agg')

        samples_per_cycle = int(SAMPLE_RATE / 40.0)
        t_ms = np.arange(samples_per_cycle) / SAMPLE_RATE * 1000

        # 各実装での出力
        outputs = {}
        for max_iter in [1, 2, 5]:
            ws = WaveShaperCppEquivalent(max_iter=max_iter)
            outputs[f'Newton {max_iter}'] = np.array([ws.process(v) for v in v_in])

        for lw_iter in [1, 2]:
            ws_lw = WaveShaperSchurLambertW(max_iter=lw_iter)
            outputs[f'LambertW {lw_iter}'] = np.array([ws_lw.process(v) for v in v_in])

        ws_adp = WaveShaperSchurLambertWAdaptive()
        outputs['Adaptive'] = np.array([ws_adp.process(v) for v in v_in])

        ws_dec = WaveShaperDecoupled()
        outputs['Decoupled'] = np.array([ws_dec.process(v) for v in v_in])

        # Omega3版
        ws_o3 = WaveShaperOmega3()
        outputs['Omega3'] = np.array([ws_o3.process(v) for v in v_in])

        # HybridAdaptive版 (BJT状態適応)
        ws_hyb = WaveShaperHybrid()
        outputs['HybridAdaptive'] = np.array([ws_hyb.process(v) for v in v_in])

        # ======================================================================
        # C++ベンチマーク実測値（Cortex-M4 @168MHz, Renode）
        # 2026-01-27 最新: diode_iv_lambertw/omega3がWright Omegaを実際に使用
        # ======================================================================
        # サイクル数とRMS/Maxは実測値を使用
        cpp_benchmark = {
            # name: (cycles, rms_mV, max_mV, category, note)
            'Schur (baseline)':   (364, 0.0, 0.0, 'Ebers-Moll', 'standard exp'),
            'SchurLambertW':      (419, 3.4, 26.8, 'Ebers-Moll', 'omega4 (W*exp(W))'),
            'SchurOmega3':        (392, 6.8, 81.8, 'Ebers-Moll', 'omega3 (W*exp(W))'),
            'Decoupled':          (426, 3.7, 23.6, 'Ebers-Moll', 'conductance delay'),
            'HybridAdaptive':     (396, 0.9, 11.5, 'Ebers-Moll', 'BJT state adaptive'),
            'SchurMo':            (370, None, None, 'Ebers-Moll', 'mo::pow2'),
            'SchurUltra':         (372, None, None, 'Ebers-Moll', 'BC delayed'),
            'WDFFull':            (379, None, None, 'WDF', 'Lambert W'),
            'WDF':                (384, None, None, 'WDF', 'DiodePair'),
            'SchurTable':         (413, None, None, 'Ebers-Moll', 'Meijer LUT'),
            'Fast (Euler)':       (208, 501550.0, 503477.4, 'Non-Ebers-Moll', 'Forward Euler'),
            'SquareShaper':       (174, None, None, 'Non-Ebers-Moll', 'PNP approx'),
            # 新しい高速化バリエーション (2026-01-27追加)
            'VCCS':               (126, 7026.6, 8807.4, 'Non-Ebers-Moll', 'linear VCCS'),
            'PWL':                (259, 2514.5, 4797.9, 'Non-Ebers-Moll', 'piecewise linear'),
            'OneIter':            (370, 0.9, 11.5, 'Ebers-Moll', '1 Newton + prev g'),
            'TwoIter':            (686, 12.0, 134.1, 'Ebers-Moll', '2 Newton'),
            'Schraudolph':        (1614, 15.7, 168.8, 'Ebers-Moll', 'bitwise exp'),
        }

        # 色定義
        colors_cpp = {
            'Schur (baseline)': '#cc4444',
            'SchurLambertW': '#4ecdc4',
            'SchurOmega3': '#9b59b6',
            'Decoupled': '#6bcb77',
            'HybridAdaptive': '#2ecc71',
            'SchurMo': '#ff9999',
            'SchurUltra': '#ffb366',
            'WDFFull': '#66b3ff',
            'WDF': '#3399ff',
            'SchurTable': '#ff66b3',
            'Fast (Euler)': '#999999',
            'SquareShaper': '#e67e22',
        }

        # ======================================================================
        # サマリーテーブル出力（C++実測値）
        # ======================================================================
        print('\n' + '=' * 90)
        print('C++ BENCHMARK RESULTS: All Implementations (Cortex-M4 @168MHz, 48kHz)')
        print('=' * 90)
        print(f'{"Implementation":<20} {"Cycles":>8} {"RT ratio":>10} {"RMS(mV)":>12} {"Max(mV)":>12} {"Category":<15}')
        print('-' * 90)

        # サイクル数でソート
        sorted_cpp = sorted(cpp_benchmark.items(), key=lambda x: x[1][0])

        for name, (cycles, rms, mx, category, note) in sorted_cpp:
            rt_ratio = 168e6 / (cycles * SAMPLE_RATE)
            rms_str = f'{rms:.1f}' if rms is not None else '-'
            mx_str = f'{mx:.1f}' if mx is not None else '-'
            print(f'{name:<20} {cycles:>8} {rt_ratio:>9.1f}x {rms_str:>12} {mx_str:>12} {category:<15}')

        print('-' * 90)
        print('Note: RMS/Max error vs Schur baseline @ 40Hz (C++ Renode benchmark)')
        print('      RT ratio = 168MHz / (cycles * 48kHz) - higher is better')
        print('      LambertW/Omega3 now use actual Wright Omega: exp(x) = W(e^x) * exp(W(e^x))')
        print('=' * 90)

        out_dir = os.path.dirname(os.path.abspath(__file__))

        # 5つの周波数でグラフを生成
        frequencies = [40, 80, 160, 440, 1000]

        # プロット用の間引き設定（48kHzで処理、表示は間引き）
        MAX_PLOT_POINTS = 1200  # グラフの最大ポイント数

        for freq in frequencies:
            # 48kHzで1周期分のサンプル数
            samples_per_cycle_freq = int(SAMPLE_RATE / freq)

            # 入力信号（指定周波数のノコギリ波）- 48kHzで生成
            v_in_freq = generate_sawtooth(freq, 0.1)

            # リファレンス出力（100反復）- Schur baselineと同等
            ws_ref_freq = WaveShaperCppEquivalent(max_iter=100)
            ref_output_freq = np.array([ws_ref_freq.process(v) for v in v_in_freq])

            # Python実装とC++実測サイクル数の対応
            # key: Python実装名, value: (class, init_args, C++サイクル数, C++名, カテゴリ, 色)
            # 2026-01-27 最新: サイクル数をC++ベンチマーク結果に合わせて更新
            impl_mapping = {
                'Schur (baseline)': (WaveShaperCppEquivalent, {'max_iter': 5}, 364, 'Schur', 'Ebers-Moll', '#cc4444'),
                'OneIter': (WaveShaperOneIter, {}, 370, 'OneIter', 'Ebers-Moll', '#f39c12'),
                'HybridAdaptive': (WaveShaperHybrid, {'veb_threshold': 0.3}, 396, 'HybridAdaptive', 'Ebers-Moll', '#2ecc71'),
                'Pade [2,2]': (WaveShaperPade, {}, 350, 'Pade22', 'Ebers-Moll', '#8e44ad'),
                'LUT (1024)': (WaveShaperLUT, {}, 340, 'LUT', 'Ebers-Moll', '#16a085'),
                'VCCS': (WaveShaperVCCS, {}, 126, 'VCCS', 'Non-Ebers-Moll', '#3498db'),
                'PWL': (WaveShaperPWL, {'n_iter': 1}, 259, 'PWL', 'Non-Ebers-Moll', '#1abc9c'),
                'Schraudolph': (WaveShaperSchraudolph, {'n_iter': 5}, 1614, 'Schraudolph', 'Ebers-Moll', '#9b59b6'),
                'TwoIter': (WaveShaperTwoIter, {}, 686, 'TwoIter', 'Ebers-Moll', '#e74c3c'),
                'SquareShaper': (SquareShaper, {'shape': 0.5}, 174, 'SquareShaper', 'Non-Ebers-Moll', '#e67e22'),
            }

            # 各実装での出力を計算
            outputs_freq = {}
            for name, (cls, kwargs, cycles, cpp_name, category, color) in impl_mapping.items():
                ws = cls(**kwargs)
                outputs_freq[name] = np.array([ws.process(v) for v in v_in_freq])

            # プロット用の間引きステップを計算
            if samples_per_cycle_freq > MAX_PLOT_POINTS:
                step = samples_per_cycle_freq // MAX_PLOT_POINTS
            else:
                step = 1
            plot_indices = np.arange(0, samples_per_cycle_freq, step)

            # 時間軸（間引き後）
            t_ms_plot = plot_indices / SAMPLE_RATE * 1000

            # 3段構成: 波形 + 誤差 + サマリーテーブル
            fig = plt.figure(figsize=(18, 16))
            gs = fig.add_gridspec(3, 1, height_ratios=[1, 1, 0.6], hspace=0.3)
            ax_wave = fig.add_subplot(gs[0])
            ax_err = fig.add_subplot(gs[1])
            ax_table = fig.add_subplot(gs[2])

            # 波形比較（間引き表示）
            ax_wave.plot(t_ms_plot, v_in_freq[plot_indices], 'b-', alpha=0.3, lw=1.5, label='Input (v_in)')
            ax_wave.plot(t_ms_plot, ref_output_freq[plot_indices], 'k-', lw=3, label='Reference (100 iter)')

            for name, (cls, kwargs, cycles, cpp_name, category, color) in impl_mapping.items():
                if name in outputs_freq:
                    label = f"{name} ({cycles} cyc)"
                    ax_wave.plot(t_ms_plot, outputs_freq[name][plot_indices], '-', color=color, alpha=0.7, lw=1.5, label=label)

            ax_wave.set_ylabel('Voltage [V]', fontsize=12)
            ax_wave.set_title(f'TB-303 Wave Shaper @ {freq}Hz @ 48kHz (Cortex-M4 @168MHz cycles from C++ benchmark)', fontsize=14)
            ax_wave.legend(loc='upper right', fontsize=10, ncol=2)
            ax_wave.grid(True, alpha=0.3)

            # 誤差グラフ + RMS/Maxを収集（Python実装で計算）
            table_data = []
            for name, (cls, kwargs, cycles, cpp_name, category, color) in impl_mapping.items():
                if name in outputs_freq:
                    err_full = (outputs_freq[name][:samples_per_cycle_freq] - ref_output_freq[:samples_per_cycle_freq]) * 1000
                    rms = np.sqrt(np.mean(err_full ** 2))
                    mx = np.max(np.abs(err_full))
                    label = f"{name} (RMS:{rms:.1f}mV)"
                    ax_err.plot(t_ms_plot, err_full[plot_indices], '-', color=color, alpha=0.7, lw=1.5, label=label)
                    # テーブル用データ収集（サイクル数はC++実測値、RMS/MaxはPython計算値）
                    rt_ratio = 168e6 / (cycles * SAMPLE_RATE)
                    table_data.append([name, cycles, f'{rt_ratio:.1f}x', f'{rms:.1f}', f'{mx:.1f}', category])

            ax_err.set_xlabel('Time [ms]', fontsize=12)
            ax_err.set_ylabel('Error [mV]', fontsize=12)
            ax_err.set_title(f'Error vs Reference @ {freq}Hz (cycles: C++ benchmark, RMS/Max: Python)', fontsize=14)
            ax_err.legend(loc='lower right', fontsize=10, ncol=2)
            ax_err.grid(True, alpha=0.3)

            # サマリーテーブル（サイクル数でソート）
            ax_table.axis('off')
            table_data_sorted = sorted(table_data, key=lambda x: x[1])
            col_labels = ['Implementation', 'Cycles', 'RT ratio', 'RMS(mV)', 'Max(mV)', 'Category']
            table = ax_table.table(
                cellText=table_data_sorted,
                colLabels=col_labels,
                loc='center',
                cellLoc='center',
                colWidths=[0.20, 0.10, 0.12, 0.12, 0.12, 0.18]
            )
            table.auto_set_font_size(False)
            table.set_fontsize(10)
            table.scale(1.2, 1.5)
            # ヘッダー色
            for j in range(len(col_labels)):
                table[(0, j)].set_facecolor('#4472C4')
                table[(0, j)].set_text_props(color='white', fontweight='bold')
            # 行色（サイクル数が少ないほど緑）
            # 閾値: <250=緑(高速), <400=黄(中速), >=400=赤(低速)
            for i, row in enumerate(table_data_sorted):
                cycles = row[1]
                if cycles <= 250:
                    bg = '#C6EFCE'  # 緑 (高速: SquareShaper等)
                elif cycles <= 400:
                    bg = '#FFEB9C'  # 黄 (中速: Schur, HybridAdaptive等)
                else:
                    bg = '#FFC7CE'  # 赤 (低速: LambertW, Decoupled等)
                for j in range(len(col_labels)):
                    table[(i+1, j)].set_facecolor(bg)
            ax_table.set_title(f'Performance Summary @ {freq}Hz (Cycles: C++ Renode benchmark, 2026-01-27)', fontsize=12, pad=10)

            fig.subplots_adjust(top=0.95, bottom=0.05, left=0.06, right=0.98, hspace=0.25)
            filename = f'waveshaper_comparison_{freq}hz.png'
            plt.savefig(os.path.join(out_dir, filename), dpi=150)
            plt.close(fig)
            print(f'Saved: {filename}')

    except ImportError:
        print('\nMatplotlib not available, skipping graph.')


if __name__ == '__main__':
    main()
