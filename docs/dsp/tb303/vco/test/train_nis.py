#!/usr/bin/env python3
"""
Neural Implicit Solver (NIS) の学習スクリプト

Newton反復の「解」をニューラルネットで直接予測する
入力: (v_in, v_c1_prev, v_c2_prev)
出力: (v_out, v_c1_new, v_c2_new)
"""

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset
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
        self.g_c1 = C1 / DT
        self.g_c2 = C2 / DT

    def reset(self):
        self.v_c1, self.v_c2 = 0.0, 8.0
        self.v_b, self.v_e, self.v_c = 8.0, 8.0, V_COLL

    def set_state(self, v_c1, v_c2):
        self.v_c1, self.v_c2 = v_c1, v_c2
        self.v_b = 8.0
        self.v_e = v_c2
        self.v_c = V_COLL

    def get_state(self):
        return self.v_c1, self.v_c2

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
        v_cap = v_in - v_c1_prev
        v_b, v_e, v_c = self.v_b, v_c2_prev, self.v_c

        for _ in range(100):
            i_ef, g_ef = self.diode_iv(v_e - v_b)
            i_cr, g_cr = self.diode_iv(v_c - v_b)

            i_e = i_ef - ALPHA_R * i_cr
            i_c = ALPHA_F * i_ef - i_cr
            i_b = i_e - i_c

            f1 = self.g_c1*(v_in - v_cap - v_c1_prev) - G3*(v_cap - v_b)
            f2 = G2*(v_in - v_b) + G3*(v_cap - v_b) + i_b
            f3 = G4*(V_CC - v_e) - i_e - self.g_c2*(v_e - v_c2_prev)
            f4 = G5*(V_COLL - v_c) + i_c

            if abs(f1) + abs(f2) + abs(f3) + abs(f4) < 1e-12:
                break

            J = np.array([
                [-self.g_c1-G3, G3, 0, 0],
                [G3, -G2-G3-(1-ALPHA_F)*g_ef-(1-ALPHA_R)*g_cr, (1-ALPHA_F)*g_ef, (1-ALPHA_R)*g_cr],
                [0, g_ef-ALPHA_R*g_cr, -G4-g_ef-self.g_c2, ALPHA_R*g_cr],
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


class WaveShaperNN(nn.Module):
    """Neural Implicit Solver ネットワーク"""
    def __init__(self, hidden_size=16, num_layers=4):
        super().__init__()
        layers = [nn.Linear(3, hidden_size), nn.ReLU()]
        for _ in range(num_layers - 2):
            layers.extend([nn.Linear(hidden_size, hidden_size), nn.ReLU()])
        layers.append(nn.Linear(hidden_size, 3))
        self.net = nn.Sequential(*layers)

    def forward(self, x):
        return self.net(x)


def generate_training_data(n_samples=100000, sequential_ratio=0.5):
    """学習データ生成

    2種類のデータを混合:
    1. ランダムサンプリング: 状態空間を広くカバー
    2. シーケンシャル: 時系列的な連続性を学習
    """
    ref = WaveShaperReference()

    # ランダムサンプリング
    n_random = int(n_samples * (1 - sequential_ratio))
    v_in_rand = np.random.uniform(5.5, 12.0, n_random)
    v_c1_rand = np.random.uniform(-5.0, 5.0, n_random)
    v_c2_rand = np.random.uniform(6.0, 10.0, n_random)

    v_out_rand = np.zeros(n_random)
    v_c1_new_rand = np.zeros(n_random)
    v_c2_new_rand = np.zeros(n_random)

    print("Generating random samples...")
    for i in range(n_random):
        if i % 10000 == 0:
            print(f"  {i}/{n_random}")
        ref.set_state(v_c1_rand[i], v_c2_rand[i])
        v_out_rand[i] = ref.process(v_in_rand[i])
        v_c1_new_rand[i], v_c2_new_rand[i] = ref.get_state()

    # シーケンシャルデータ
    n_seq = n_samples - n_random
    n_sequences = n_seq // 1200  # 1200サンプル/シーケンス (25ms @ 48kHz)

    v_in_seq = []
    v_c1_seq = []
    v_c2_seq = []
    v_out_seq = []
    v_c1_new_seq = []
    v_c2_new_seq = []

    print(f"Generating {n_sequences} sequential traces...")
    for seq in range(n_sequences):
        if seq % 100 == 0:
            print(f"  {seq}/{n_sequences}")

        # ランダムな周波数のノコギリ波
        freq = np.random.uniform(20, 200)
        phase = np.random.uniform(0, 1)

        ref.reset()

        for i in range(1200):
            t = i / SAMPLE_RATE
            v_in = 12.0 - 6.5 * ((t * freq + phase) % 1.0)

            v_c1_prev, v_c2_prev = ref.get_state()
            v_out = ref.process(v_in)
            v_c1_new, v_c2_new = ref.get_state()

            v_in_seq.append(v_in)
            v_c1_seq.append(v_c1_prev)
            v_c2_seq.append(v_c2_prev)
            v_out_seq.append(v_out)
            v_c1_new_seq.append(v_c1_new)
            v_c2_new_seq.append(v_c2_new)

    # 結合
    X = np.stack([
        np.concatenate([v_in_rand, np.array(v_in_seq)]),
        np.concatenate([v_c1_rand, np.array(v_c1_seq)]),
        np.concatenate([v_c2_rand, np.array(v_c2_seq)])
    ], axis=1)

    Y = np.stack([
        np.concatenate([v_out_rand, np.array(v_out_seq)]),
        np.concatenate([v_c1_new_rand, np.array(v_c1_new_seq)]),
        np.concatenate([v_c2_new_rand, np.array(v_c2_new_seq)])
    ], axis=1)

    return X.astype(np.float32), Y.astype(np.float32)


def train_model(X, Y, hidden_size=16, num_layers=4, epochs=200, batch_size=512, lr=1e-3):
    """モデルを学習"""
    # 正規化
    X_mean = X.mean(axis=0)
    X_std = X.std(axis=0) + 1e-8
    Y_mean = Y.mean(axis=0)
    Y_std = Y.std(axis=0) + 1e-8

    X_norm = (X - X_mean) / X_std
    Y_norm = (Y - Y_mean) / Y_std

    # PyTorchテンソル
    X_tensor = torch.tensor(X_norm)
    Y_tensor = torch.tensor(Y_norm)

    dataset = TensorDataset(X_tensor, Y_tensor)
    loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

    # モデル
    model = WaveShaperNN(hidden_size=hidden_size, num_layers=num_layers)
    optimizer = torch.optim.Adam(model.parameters(), lr=lr)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, patience=10, factor=0.5)
    criterion = nn.MSELoss()

    losses = []
    print(f"\nTraining: hidden={hidden_size}, layers={num_layers}")

    for epoch in range(epochs):
        total_loss = 0
        for X_batch, Y_batch in loader:
            pred = model(X_batch)
            loss = criterion(pred, Y_batch)

            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            total_loss += loss.item() * len(X_batch)

        avg_loss = total_loss / len(X_tensor)
        losses.append(avg_loss)
        scheduler.step(avg_loss)

        if (epoch + 1) % 20 == 0:
            # 実際の電圧誤差に変換
            with torch.no_grad():
                pred_all = model(X_tensor)
                pred_denorm = pred_all.numpy() * Y_std + Y_mean
                err = pred_denorm - Y
                rms_v_out = np.sqrt(np.mean(err[:, 0]**2)) * 1000
                print(f"Epoch {epoch+1}: Loss={avg_loss:.6f}, v_out RMS={rms_v_out:.2f}mV")

    return model, X_mean, X_std, Y_mean, Y_std, losses


def export_weights_to_cpp(model, X_mean, X_std, Y_mean, Y_std, filename="nis_weights.h"):
    """C++用に重みをエクスポート"""
    with open(filename, 'w') as f:
        f.write("// Auto-generated Neural Implicit Solver weights\n")
        f.write("// Generated by train_nis.py\n")
        f.write("#pragma once\n\n")
        f.write("namespace nis_weights {\n\n")

        # 正規化パラメータ
        f.write("// Normalization parameters\n")
        f.write(f"constexpr float input_mean[3] = {{{X_mean[0]:.8f}f, {X_mean[1]:.8f}f, {X_mean[2]:.8f}f}};\n")
        f.write(f"constexpr float input_std[3] = {{{X_std[0]:.8f}f, {X_std[1]:.8f}f, {X_std[2]:.8f}f}};\n")
        f.write(f"constexpr float output_mean[3] = {{{Y_mean[0]:.8f}f, {Y_mean[1]:.8f}f, {Y_mean[2]:.8f}f}};\n")
        f.write(f"constexpr float output_std[3] = {{{Y_std[0]:.8f}f, {Y_std[1]:.8f}f, {Y_std[2]:.8f}f}};\n\n")

        # ネットワーク重み
        layer_idx = 0
        for name, param in model.named_parameters():
            data = param.detach().cpu().numpy()

            if 'weight' in name:
                f.write(f"// Layer {layer_idx // 2} weight: {data.shape}\n")
                f.write(f"constexpr float weight{layer_idx // 2}[{data.shape[0]}][{data.shape[1]}] = {{\n")
                for row in data:
                    f.write("    {" + ", ".join(f"{v:.8f}f" for v in row) + "},\n")
                f.write("};\n\n")
            else:  # bias
                f.write(f"// Layer {layer_idx // 2} bias: {data.shape}\n")
                f.write(f"constexpr float bias{layer_idx // 2}[{data.shape[0]}] = {{\n")
                f.write("    " + ", ".join(f"{v:.8f}f" for v in data) + "\n")
                f.write("};\n\n")
                layer_idx += 2

        f.write("} // namespace nis_weights\n")

    print(f"Weights exported to: {filename}")


def evaluate_model(model, X_mean, X_std, Y_mean, Y_std):
    """モデルを評価"""
    ref = WaveShaperReference()

    # テスト: 40Hzノコギリ波
    n_samples = 2400  # 50ms
    freq = 40.0

    v_in = np.zeros(n_samples)
    out_ref = np.zeros(n_samples)
    out_nis = np.zeros(n_samples)

    ref.reset()
    v_c1_nis, v_c2_nis = 0.0, 8.0

    model.eval()
    with torch.no_grad():
        for i in range(n_samples):
            t = i / SAMPLE_RATE
            v_in[i] = 12.0 - 6.5 * ((t * freq) % 1.0)

            # Reference
            out_ref[i] = ref.process(v_in[i])

            # NIS
            x = np.array([[v_in[i], v_c1_nis, v_c2_nis]], dtype=np.float32)
            x_norm = (x - X_mean) / X_std
            y_norm = model(torch.tensor(x_norm)).numpy()
            y = y_norm * Y_std + Y_mean

            out_nis[i] = y[0, 0]
            v_c1_nis = y[0, 1]
            v_c2_nis = y[0, 2]

    # 誤差
    err = out_nis - out_ref
    rms = np.sqrt(np.mean(err**2)) * 1000
    max_err = np.max(np.abs(err)) * 1000

    print(f"\n=== Evaluation (40Hz sawtooth, 50ms) ===")
    print(f"RMS Error:  {rms:.2f} mV")
    print(f"Max Error:  {max_err:.2f} mV")

    # プロット
    t = np.arange(n_samples) / SAMPLE_RATE * 1000

    fig, axes = plt.subplots(2, 1, figsize=(12, 8))

    ax1 = axes[0]
    ax1.plot(t, out_ref, 'b-', label='Reference', linewidth=1.5)
    ax1.plot(t, out_nis, 'r--', label=f'NIS (RMS={rms:.1f}mV)', linewidth=1.2, alpha=0.8)
    ax1.set_xlabel('Time [ms]')
    ax1.set_ylabel('Voltage [V]')
    ax1.set_title('Neural Implicit Solver vs Reference')
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    ax2 = axes[1]
    ax2.plot(t, err * 1000, 'r-', linewidth=1)
    ax2.set_xlabel('Time [ms]')
    ax2.set_ylabel('Error [mV]')
    ax2.set_title(f'Error (Max={max_err:.1f}mV)')
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig('/Users/tekitou/work/umi/docs/dsp/tb303/vco/test/nis_evaluation.png', dpi=150)
    print("Saved: nis_evaluation.png")
    plt.show()

    return rms, max_err


def main():
    # データ生成
    print("=== Generating Training Data ===")
    X, Y = generate_training_data(n_samples=100000, sequential_ratio=0.5)
    print(f"Data shape: X={X.shape}, Y={Y.shape}")

    # 学習
    print("\n=== Training ===")
    model, X_mean, X_std, Y_mean, Y_std, losses = train_model(
        X, Y,
        hidden_size=16,  # 8→16に増加
        num_layers=4,
        epochs=200,
        batch_size=512,
        lr=1e-3
    )

    # 評価
    rms, max_err = evaluate_model(model, X_mean, X_std, Y_mean, Y_std)

    # 重みエクスポート
    if rms < 50:  # 50mV以下なら保存
        export_weights_to_cpp(
            model, X_mean, X_std, Y_mean, Y_std,
            '/Users/tekitou/work/umi/docs/dsp/tb303/vco/test/nis_weights.h'
        )

    # パラメータ数
    total_params = sum(p.numel() for p in model.parameters())
    print(f"\nTotal parameters: {total_params}")
    print(f"Memory: {total_params * 4} bytes")


if __name__ == '__main__':
    main()
