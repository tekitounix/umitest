import math
import timeit

# ==========================================
# 定数とヘルパー
# ==========================================
PI = 3.141592653589793
z = [0.0] * 5  # 共通の状態変数（簡易的な共有）

def clip(x):
    return x / (1.0 + abs(x))

# ==========================================
# 1. Original (展開形)
# ==========================================
def tick_original(x, fc, k=0.5, ah=0.0, bh=0.5):
    # 係数計算
    a = PI * fc
    a2 = a * a
    b = 2 * a + 1
    b2 = b * b
    c = 1.0 / (2 * a2 * a2 - 4 * a2 * b2 + b2 * b2)

    g0 = 2 * a2 * a2 * c
    g = g0 * bh

    # s0 計算 (展開形: 乗算が多い)
    s0 = (a2 * a * z[0] + a2 * b * z[1] + z[2] * (b2 - 2 * a2) * a + z[3] * (b2 - 3 * a2) * b) * c

    # 以下共通フロー
    s = bh * s0 - z[4]
    y5 = (g * x + s) / (1 + g * k)
    y0 = clip(x - k * y5)
    y5 = g * y0 + s

    y4 = g0 * y0 + s0
    ainv = 1.0 / a
    y3 = (b * y4 - z[3]) * ainv
    y2 = (b * y3 - a * y4 - z[2]) * ainv
    y1 = (b * y2 - a * y3 - z[1]) * ainv

    z[0] += 4 * a * (y0 - y1 + y2)
    z[1] += 2 * a * (y1 - 2 * y2 + y3)
    z[2] += 2 * a * (y2 - 2 * y3 + y4)
    z[3] += 2 * a * (y3 - 2 * y4)
    z[4] = bh * y4 + ah * y5
    return y4

# ==========================================
# 2. Method 2 (因数分解 + D)
# ==========================================
def tick_method2(x, fc, k=0.5, ah=0.0, bh=0.5):
    a = PI * fc
    a2 = a * a
    b = 2 * a + 1
    b2 = b * b

    # 最適化: D の導入
    D = b2 - 2 * a2
    c = 1.0 / (D * D - 2 * a2 * a2)

    # 最適化: s0 の因数分解
    term1 = a * z[0] + b * (z[1] - z[3])
    term2 = a * z[2] + b * z[3]
    s0 = c * (a2 * term1 + D * term2)

    g0 = 2 * a2 * a2 * c
    g = g0 * bh
    s = bh * s0 - z[4]

    y5 = (g * x + s) / (1 + g * k)
    y0 = clip(x - k * y5)
    y5 = g * y0 + s

    y4 = g0 * y0 + s0
    ainv = 1.0 / a
    y3 = (b * y4 - z[3]) * ainv
    y2 = (b * y3 - a * y4 - z[2]) * ainv
    y1 = (b * y2 - a * y3 - z[1]) * ainv

    z[0] += 4 * a * (y0 - y1 + y2)
    z[1] += 2 * a * (y1 - 2 * y2 + y3)
    z[2] += 2 * a * (y2 - 2 * y3 + y4)
    z[3] += 2 * a * (y3 - 2 * y4)
    z[4] = bh * y4 + ah * y5
    return y4

# ==========================================
# 3. Method 3 (Fully Optimized)
# ==========================================
def tick_method3(x, fc, k=0.5, ah=0.0, bh=0.5):
    a = PI * fc
    a2 = a * a

    # 最適化: 2*a の共有
    two_a = 2 * a
    b = two_a + 1
    b2 = b * b

    # 最適化: sub_term の共有
    sub_term = 2 * a2 * a2
    D = b2 - 2 * a2

    c = 1.0 / (D * D - sub_term)
    g0 = sub_term * c # 計算省略

    term1 = a * z[0] + b * (z[1] - z[3])
    term2 = a * z[2] + b * z[3]
    s0 = c * (a2 * term1 + D * term2)

    g = g0 * bh
    s = bh * s0 - z[4]

    y5 = (g * x + s) / (1 + g * k)
    y0 = clip(x - k * y5)
    y5 = g * y0 + s

    y4 = g0 * y0 + s0
    ainv = 1.0 / a
    y3 = (b * y4 - z[3]) * ainv
    y2 = (b * y3 - a * y4 - z[2]) * ainv
    y1 = (b * y2 - a * y3 - z[1]) * ainv

    # 最適化: two_a の再利用
    z[0] += 2 * two_a * (y0 - y1 + y2)
    z[1] += two_a * (y1 - 2 * y2 + y3)
    z[2] += two_a * (y2 - 2 * y3 + y4)
    z[3] += two_a * (y3 - 2 * y4)
    z[4] = bh * y4 + ah * y5
    return y4

# ==========================================
# ベンチマーク実行
# ==========================================
if __name__ == "__main__":
    N = 200_000  # 反復回数
    setup = "from __main__ import tick_original, tick_method2, tick_method3, z"

    # キャッシュ等の影響を避けるため、パラメータを変えながら実行するラッパー
    wrapper_orig = "tick_original(0.5, 0.1)"
    wrapper_m2   = "tick_method2(0.5, 0.1)"
    wrapper_m3   = "tick_method3(0.5, 0.1)"

    t1 = timeit.timeit(wrapper_orig, setup=setup, number=N)
    t2 = timeit.timeit(wrapper_m2, setup=setup, number=N)
    t3 = timeit.timeit(wrapper_m3, setup=setup, number=N)

    print(f"Iterations: {N}")
    print(f"1. Original: {t1:.4f} sec (100.0%)")
    print(f"2. Method 2: {t2:.4f} sec ({t2/t1*100:.1f}%)")
    print(f"3. Method 3: {t3:.4f} sec ({t3/t1*100:.1f}%)")

    print("\nWinner: Method 3 is {:.2f}x faster than Original".format(t1/t3))
