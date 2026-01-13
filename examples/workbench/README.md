# UMI-OS Testbench Example

Synth + Effect + Audio I/O + MIDI を統合したテストベンチアプリケーション。
UMI-OS の機能を包括的にテストするためのリファレンス実装です。

## ファイル構成

```
examples/testbench/
├── README.md              ← このファイル
├── testbench_map.hh       ← パラメータ定義（Single Source of Truth）
├── testbench_app.hh       ← アプリ固有ロジック（Processor chain）
├── testbench_wasm.cc      ← WASM エントリポイント（薄いグルーコード）
└── testbench.html         ← Web UI
```

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│  UI Layer                                                    │
│  testbench.html                                              │
│  ・連番 index でパラメータにアクセス                          │
│  ・メタデータ（名前・範囲・単位）は WASM API から取得          │
│  ・MIDI Learn 対応                                           │
└─────────────────────────────────────────────────────────────┘
                         ↓ WASM API (index-based)
┌─────────────────────────────────────────────────────────────┐
│  WASM Adapter (shared)                                       │
│  adapter/wasm/wasm_adapter.hh                                │
│  ・共通テンプレート WasmAdapter<App>                          │
│  ・AudioContext 構築                                         │
│  ・パラメータ API / MIDI CC / MIDI Learn                     │
│  ・C API 生成マクロ UMI_WASM_DEFINE_API                       │
└─────────────────────────────────────────────────────────────┘
                         ↓ uses
┌─────────────────────────────────────────────────────────────┐
│  Application Layer                                           │
│  testbench_app.hh                                            │
│  ├── TestbenchApp class     アプリ固有の処理チェーン         │
│  ├── process()              Synth → Mixer → Effect          │
│  ├── set_param/get_param    パラメータルーティング           │
│  └── find_param_index_by_cc MIDI CC → param 変換            │
│                                                              │
│  testbench_map.hh                                            │
│  ├── enum class ParamId     型安全な内部 ID                  │
│  ├── ParamMeta[]            メタデータ配列                   │
│  └── MidiMapEntry[]         デフォルト MIDI マップ            │
│                                                              │
│  ../shared/synth_processor.hh   8 ボイスポリフォニックシンセ  │
│  ../shared/effect_processor.hh  ディレイエフェクト            │
└─────────────────────────────────────────────────────────────┘
                         ↓ uses
┌─────────────────────────────────────────────────────────────┐
│  Framework Layer (UMI-OS)                                    │
│  include/umi/ui_map.hh      Curve, 値変換                    │
│  include/umi/audio_context.hh                                │
│  include/umi/event.hh       イベントキュー                    │
└─────────────────────────────────────────────────────────────┘
```

## パラメータ設計

### 型安全な内部 ID

```cpp
enum class ParamId : uint32_t {
    MasterVolume = 0,
    FilterCutoff,
    FilterResonance,
    DelayTime,
    // ...
    Count
};
```

- **アプリ内部**: `ParamId` enum で型安全にアクセス
- **UI / 外部 API**: 連番 `index` でアクセス（0, 1, 2, ...）
- `static_assert` でメタデータ配列と enum の整合性を保証

### メタデータ

```cpp
struct ParamMeta {
    const char* name;         // "Master Volume"
    const char* unit;         // "Hz", "dB", "ms", ""
    float min, max;           // 表示範囲
    float default_val;        // デフォルト値
    Curve curve;              // Linear, Log, Exp, ...
    uint8_t decimal_places;   // 表示桁数
};
```

### WASM API

| 関数 | 説明 |
|------|------|
| `testbench_get_param_count()` | パラメータ総数 |
| `testbench_get_param_name(index)` | 名前 |
| `testbench_get_param_min(index)` | 最小値 |
| `testbench_get_param_max(index)` | 最大値 |
| `testbench_get_param_default(index)` | デフォルト値 |
| `testbench_get_param_curve(index)` | カーブ種別 |
| `testbench_get_param_unit(index)` | 単位 |
| `testbench_set_param(index, value)` | 値設定 |
| `testbench_get_param(index)` | 値取得 |

## ビルド

```bash
# WASM ビルド
xmake build wasm_testbench

# シンボリックリンク作成（初回のみ）
cd web && ln -sf ../.build/wasm wasm

# HTTP サーバー起動
cd web && python3 -m http.server 8080

# ブラウザでアクセス
open http://localhost:8080/testbench.html
```

**注意**: `testbench.html` は `examples/testbench/` にありますが、
実行時は `web/` から `wasm/` へのシンボリックリンク経由でアクセスします。

```bash
# testbench.html を web/ にコピー（オプション）
cp examples/testbench/testbench.html web/
```

## 機能

- **シンセ**: 8ボイスポリフォニック、フィルター
- **エフェクト**: ディレイ（時間、フィードバック、フィルター、バイパス）
- **オーディオ I/O**: マイク入力 → エフェクト → 出力
- **MIDI**: ノートオン/オフ、CC、MIDI Learn

## 設計原則

1. **Single Source of Truth**: `testbench_map.hh` がすべてのパラメータ情報を持つ
2. **UI は意味を持たない**: 連番 index でアクセス、メタデータはアプリから取得
3. **型安全性**: アプリ内部では `enum class ParamId` を使用
4. **業界標準準拠**: VST3/AU/CLAP と同等のパラメータ管理方式
