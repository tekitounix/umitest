#!/usr/bin/env python3
"""
WaveShaperGeometric vs Reference 波形比較

主要実装の波形を比較:
- Reference (100回Newton)
- WaveShaper3Var (3変数Newton, 1回)
- WaveShaperGeometric (双曲幾何写像)
"""

import numpy as np
import matplotlib.pyplot as plt

SAMPLE_RATE = 48000.0
DT = 1.0 / SAMPLE_RATE

# 回路定数
V_CC = 12.0
V_COLL = 5.33
R2, R3, R4, R5 = 100e3, 10e3, 22e3, 10e3
C1, C2 = 10e-9, 1e-6
V_T = 0.025865
V_T_INV = 1.0 / V_T
I_S = 1e-13
BETA_F = 100.0
ALPHA_F = BETA_F / (BETA_F + 1.0)
ALPHA_R = 0.5 / 1.5

G2, G3, G4, G5 = 1/R2, 1/R3, 1/R4, 1/R5


class WaveShaperReference:
    """リファレンス（100反復Newton）"""
    def __init__(self):
        self.reset()

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def diode_iv(self, v):
        v_crit = V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / V_T)
            i = I_S * (exp_crit - 1) + I_S * V_T_INV * exp_crit * (v - v_crit)
            g = I_S * V_T_INV * exp_crit
        elif v < -10 * V_T:
            i, g = -I_S, 1e-12
        else:
            exp_v = np.exp(v / V_T)
            i = I_S * (exp_v - 1)
            g = I_S * V_T_INV * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        g_c1, g_c2 = C1/DT, C2/DT
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(100):
            i_ef, g_ef = self.diode_iv(v_e - v_b)
            i_cr, g_cr = self.diode_iv(v_c - v_b)

            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = g_c1*(v_in - v_cap - v_c1_prev) - G3*(v_cap - v_b)
            f2 = G2*(v_in - v_b) + G3*(v_cap - v_b) + i_b
            f3 = G4*(V_CC - v_e) - i_e - g_c2*(v_e - v_c2_prev)
            f4 = G5*(V_COLL - v_c) + i_c

            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-12:
                break

            J = np.array([
                [-g_c1-G3, G3, 0, 0],
                [G3, -G2-G3-(1-ALPHA_F)*g_ef-(1-ALPHA_R)*g_cr, (1-ALPHA_F)*g_ef, (1-ALPHA_R)*g_cr],
                [0, g_ef-ALPHA_R*g_cr, -G4-g_ef-g_c2, ALPHA_R*g_cr],
                [0, -ALPHA_F*g_ef+g_cr, ALPHA_F*g_ef, -G5-g_cr]
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
            v_e = np.clip(v_e + damp*dv[2], 0, V_CC+0.5)
            v_c = np.clip(v_c + damp*dv[3], 0, V_CC+0.5)

        self.v_c1, self.v_c2 = v_in - v_cap, v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


class WaveShaper3Var:
    """3変数Newton法 (v_cap消去)"""
    def __init__(self):
        self.reset()
        g_c1 = C1 / DT
        self.g_c1 = g_c1
        self.g_c2 = C2 / DT
        self.den = g_c1 + G3
        self.inv_den = 1.0 / self.den
        self.j22_linear = -G2 - G3 + G3 * G3 * self.inv_den

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def diode_iv(self, v):
        v_crit = V_T * 40.0
        if v > v_crit:
            exp_crit = np.exp(v_crit / V_T)
            i = I_S * (exp_crit - 1) + I_S * V_T_INV * exp_crit * (v - v_crit)
            g = I_S * V_T_INV * exp_crit
        elif v < -10 * V_T:
            i, g = -I_S, 1e-12
        else:
            exp_v = np.exp(v / V_T)
            i = I_S * (exp_v - 1)
            g = I_S * V_T_INV * exp_v + 1e-12
        return i, g

    def process(self, v_in):
        v_c1_prev, v_c2_prev = self.v_c1, self.v_c2
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c
        B = self.g_c1 * (v_in - v_c1_prev)

        # 1回Newton反復
        i_ef, g_ef = self.diode_iv(v_e - v_b)
        i_cr, g_cr = self.diode_iv(v_c - v_b)

        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        # 残差
        f2 = G2 * (v_in - v_b) + G3 * B * self.inv_den + (self.j22_linear + G2) * v_b + i_b
        f3 = G4 * (V_CC - v_e) - i_e - self.g_c2 * (v_e - v_c2_prev)
        f4 = G5 * (V_COLL - v_c) + i_c

        # ヤコビアン
        omaf_g_ef = (1 - ALPHA_F) * g_ef
        omar_g_cr = (1 - ALPHA_R) * g_cr
        af_g_ef = ALPHA_F * g_ef
        ar_g_cr = ALPHA_R * g_cr

        j22 = self.j22_linear - omaf_g_ef - omar_g_cr
        j23 = omaf_g_ef
        j24 = omar_g_cr
        j32 = g_ef - ar_g_cr
        j33 = -G4 - g_ef - self.g_c2
        j34 = ar_g_cr
        j42 = -af_g_ef + g_cr
        j43 = af_g_ef
        j44 = -G5 - g_cr

        # 3x3 Gauss消去
        inv_j22 = 1.0 / j22
        m32 = j32 * inv_j22
        m42 = j42 * inv_j22

        j33_p = j33 - m32 * j23
        j34_p = j34 - m32 * j24
        f3_p = f3 + m32 * f2

        j43_p = j43 - m42 * j23
        j44_p = j44 - m42 * j24
        f4_p = f4 + m42 * f2

        # 2x2 Cramer
        det = j33_p * j44_p - j34_p * j43_p
        inv_det = 1.0 / det

        dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det
        dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det
        dv_b = (-f2 - j23 * dv_e - j24 * dv_c) * inv_j22

        # ダンピング
        max_dv = max(abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0

        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0, V_CC + 0.5)

        # v_cap更新
        v_cap_new = (B + G3 * v_b) * self.inv_den

        self.v_c1 = v_in - v_cap_new
        self.v_c2 = v_e
        self.v_b, self.v_e, self.v_c = v_b, v_e, v_c
        return v_c


class WaveShaperGeometric:
    """双曲幾何写像 (Newton不要, 12 FLOPs)"""
    def __init__(self):
        self.reset()

    def reset(self):
        self.theta = 1.8
        self.phi = 0.0

    def process(self, v_in):
        # STEP 1: 入力→双曲角変換
        env = abs(v_in) * 0.3 + 7.8
        v_be = v_in - env + 0.65
        d_theta = v_be * 0.035

        # STEP 2: トーラス状態更新
        COS_PHI = 0.9998
        SIN_PHI = 0.02
        theta_new = COS_PHI * self.theta - SIN_PHI * self.phi + d_theta
        self.phi = SIN_PHI * self.theta + COS_PHI * self.phi
        self.theta = theta_new

        # STEP 3: sinh近似による出力生成
        t2 = self.theta * self.theta
        sinh_approx = self.theta * (1.0 + t2 * 0.1666667) / (1.0 - t2 * 0.05)

        v_out = 5.33 - 0.42 * sinh_approx * (1.0 + 0.15 * COS_PHI)
        return np.clip(v_out, 0.25, 11.85)


def generate_sawtooth(freq, duration, sample_rate, v_min=5.5, v_max=12.0):
    """ノコギリ波入力を生成"""
    n_samples = int(duration * sample_rate)
    t = np.arange(n_samples) / sample_rate
    phase = (t * freq) % 1.0
    return v_max - (v_max - v_min) * phase


def main():
    # 入力信号: 40Hz ノコギリ波, 50ms
    freq = 40.0
    duration = 0.05
    n_samples = int(duration * SAMPLE_RATE)
    v_in = generate_sawtooth(freq, duration, SAMPLE_RATE)
    t = np.arange(n_samples) / SAMPLE_RATE * 1000  # ms

    # 各実装を実行
    ref = WaveShaperReference()
    var3 = WaveShaper3Var()
    geo = WaveShaperGeometric()

    out_ref = np.zeros(n_samples)
    out_3var = np.zeros(n_samples)
    out_geo = np.zeros(n_samples)

    for i in range(n_samples):
        out_ref[i] = ref.process(v_in[i])
        out_3var[i] = var3.process(v_in[i])
        out_geo[i] = geo.process(v_in[i])

    # 誤差計算
    err_3var = out_3var - out_ref
    err_geo = out_geo - out_ref
    rms_3var = np.sqrt(np.mean(err_3var**2)) * 1000
    rms_geo = np.sqrt(np.mean(err_geo**2)) * 1000
    max_3var = np.max(np.abs(err_3var)) * 1000
    max_geo = np.max(np.abs(err_geo)) * 1000

    # プロット
    fig, axes = plt.subplots(3, 1, figsize=(14, 10))

    # 波形比較
    ax1 = axes[0]
    ax1.plot(t, v_in, 'k--', alpha=0.3, label='Input (Vin)', linewidth=1)
    ax1.plot(t, out_ref, 'b-', label='Reference (100 iter)', linewidth=1.5)
    ax1.plot(t, out_3var, 'g-', label=f'3Var (1 iter) RMS={rms_3var:.1f}mV', linewidth=1.2, alpha=0.8)
    ax1.plot(t, out_geo, 'r-', label=f'Geometric RMS={rms_geo:.1f}mV', linewidth=1.2, alpha=0.8)
    ax1.set_xlabel('Time [ms]')
    ax1.set_ylabel('Voltage [V]')
    ax1.set_title('TB-303 WaveShaper: Output Waveform Comparison')
    ax1.legend(loc='upper right')
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim([4, 7])

    # 誤差
    ax2 = axes[1]
    ax2.plot(t, err_3var * 1000, 'g-', label=f'3Var Error (Max={max_3var:.1f}mV)', linewidth=1)
    ax2.plot(t, err_geo * 1000, 'r-', label=f'Geometric Error (Max={max_geo:.1f}mV)', linewidth=1)
    ax2.set_xlabel('Time [ms]')
    ax2.set_ylabel('Error [mV]')
    ax2.set_title('Error vs Reference')
    ax2.legend(loc='upper right')
    ax2.grid(True, alpha=0.3)

    # ズーム (1サイクル)
    cycle_start = int(0.01 * SAMPLE_RATE)  # 10ms
    cycle_end = int(0.035 * SAMPLE_RATE)   # 35ms (1 cycle at 40Hz)
    ax3 = axes[2]
    ax3.plot(t[cycle_start:cycle_end], out_ref[cycle_start:cycle_end], 'b-',
             label='Reference', linewidth=2)
    ax3.plot(t[cycle_start:cycle_end], out_3var[cycle_start:cycle_end], 'g--',
             label='3Var', linewidth=1.5, alpha=0.8)
    ax3.plot(t[cycle_start:cycle_end], out_geo[cycle_start:cycle_end], 'r--',
             label='Geometric', linewidth=1.5, alpha=0.8)
    ax3.set_xlabel('Time [ms]')
    ax3.set_ylabel('Voltage [V]')
    ax3.set_title('Zoomed: 1 Cycle (10-35ms)')
    ax3.legend(loc='upper right')
    ax3.grid(True, alpha=0.3)

    plt.tight_layout()

    # 保存
    out_path = '/Users/tekitou/work/umi/docs/dsp/tb303/vco/test/geometric_comparison.png'
    plt.savefig(out_path, dpi=150)
    print(f"Saved: {out_path}")

    # 統計情報
    print("\n=== Accuracy Summary ===")
    print(f"3Var:      RMS={rms_3var:7.2f}mV, Max={max_3var:7.2f}mV")
    print(f"Geometric: RMS={rms_geo:7.2f}mV, Max={max_geo:7.2f}mV")

    plt.show()


if __name__ == '__main__':
    main()
