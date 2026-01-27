# TB-303 WaveShaper テスト

TB-303 VCO WaveShaperのC++実装をPythonから検証するためのテスト環境。

## ディレクトリ構成

```
vco/
├── code/
│   ├── tb303_waveshaper_fast.hpp    # C++実装（ヘッダオンリー）
│   ├── waveshaper_pybind.cpp        # pybind11バインディング
│   └── tb303_waveshaper.*.so        # ビルド済みモジュール
└── test/
    ├── compare_all_models.py        # 比較テストスクリプト
    ├── waveshaper_comparison.png    # 出力グラフ
    └── README.md                    # このファイル
```

## ビルド手順

### 前提条件

- Python 3.x
- pybind11 (`pip install pybind11`)
- NumPy (`pip install numpy`)
- Matplotlib (`pip install matplotlib`)
- clang++ (C++17対応)

### Pythonモジュールのビルド

```bash
cd docs/dsp/tb303/vco/code

# macOS
clang++ -std=c++17 -O3 -shared -fPIC -undefined dynamic_lookup \
    $(python3 -m pybind11 --includes) waveshaper_pybind.cpp \
    -o tb303_waveshaper$(python3-config --extension-suffix)

# Linux
g++ -std=c++17 -O3 -shared -fPIC \
    $(python3 -m pybind11 --includes) waveshaper_pybind.cpp \
    -o tb303_waveshaper$(python3-config --extension-suffix)
```

## テスト実行

```bash
cd docs/dsp/tb303/vco/test

# 全比較 + グラフ表示
python3 compare_all_models.py

# グラフ表示なし（保存のみ）
python3 compare_all_models.py --no-plot

# 出力ディレクトリ指定
python3 compare_all_models.py -o /path/to/output
```

## 実装一覧

| クラス名 | 説明 | 反復数 |
|----------|------|--------|
| `WaveShaperReference` | 高精度リファレンス (std::exp使用) | 100 |
| `WaveShaperNewton1` | Newton法 | 1 |
| `WaveShaperNewton2` | Newton法 | 2 |
| `WaveShaperNewton3` | Newton法 | 3 |
| `WaveShaperSchur1` | Schur補行列法 | 1 |
| `WaveShaperSchur2` | Schur補行列法（推奨） | 2 |

### Newton法 vs Schur補行列法

- **Newton法**: 4x4線形系を直接解く
- **Schur補行列法**: j22ピボットで4x4→2x2に縮約して解く

両者は数学的に同値（Newton1=Schur1, Newton2=Schur2）。
Schur法は定数の事前計算により若干高速。

## 出力

### コンソール出力例

```
======================================================================
TB-303 WaveShaper: Model Comparison
======================================================================

Accuracy Summary (vs Reference)
======================================================================
Model           RMS [mV]     Max [mV]
---------------------------------------
Newton1         616.22       3855.41
Newton2         315.08       3020.28
Newton3         248.87       2509.56
Schur1          616.22       3855.41
Schur2          315.08       3020.28

Performance Benchmark (1 second of audio)
======================================================================
Model           Time [ms]    Samples/s       Realtime
----------------------------------------------------
Newton2         4.62         10395861        216.6     x
Schur2          4.55         10552125        219.8     x
```

### グラフ出力

`waveshaper_comparison.png` に以下を含む統合グラフを出力：

1. **波形比較**: 5周波数（40, 110, 220, 440, 880 Hz）での出力波形
2. **誤差時系列**: リファレンスとの誤差推移
3. **誤差＆性能**: RMS誤差とリアルタイム比の棒グラフ
4. **高周波ズーム**: 440Hz区間の拡大
5. **低周波ズーム**: 40Hz区間（ソフトニー領域）の拡大

## Python API

```python
import tb303_waveshaper as ws

# インスタンス作成
shaper = ws.WaveShaperSchur2()

# サンプルレート設定
shaper.set_sample_rate(48000.0)

# 状態リセット
shaper.reset()

# 単一サンプル処理
output = shaper.process(9.0)

# 配列処理（NumPy）
import numpy as np
input_array = np.array([9.0, 8.5, 8.0], dtype=np.float32)
output_array = shaper.process_array(input_array)
```

### エクスポートされた定数 (TB-303 回路図準拠)

```python
ws.V_T       # 熱電圧 (0.025865)
ws.I_S       # 飽和電流 (1e-13)
ws.BETA_F    # 順方向β (100)
ws.ALPHA_F   # 順方向α (0.99)
ws.V_CC      # 電源電圧 (12.0)
ws.V_COLL    # コレクタ電圧 (5.33)

# 抵抗 (TB-303 回路図の部品番号)
ws.R34       # 10kΩ (Input)
ws.R35       # 100kΩ (Input)
ws.R36       # 10kΩ
ws.R45       # 22kΩ

# キャパシタ
ws.C10       # 0.01μF (10nF)
ws.C11       # 1μF
```
