# UMI Simulation Backends

UMIは複数のシミュレーションバックエンドをサポートし、用途に応じて使い分けられます。

## バックエンド比較

| 特性 | WASM | Renode | Cortex-M (Web) |
|------|------|--------|----------------|
| 実行速度 | ⚡ リアルタイム | 🐢 サイクル精度 | 🚶 中程度 |
| オーディオ | ✅ 直接 | ✅ ブリッジ経由 | ✅ ブリッジ経由 |
| MIDI | ✅ WebMIDI | ✅ WebMIDI | ✅ WebMIDI |
| サーバー不要 | ✅ | ❌ | ✅ |
| デバッグ | 基本 | 詳細 (GDB) | 基本 |
| ペリフェラル | エミュレート | 正確 | エミュレート |
| 用途 | 開発・デモ | テスト・検証 | ポータブルデモ |

## 1. WASM Backend（デフォルト）

最も高速なバックエンド。組み込みコードをEmscriptenでWASMにクロスコンパイルし、
AudioWorkletで実行します。

```
┌─────────────────────────────────────────────────────────┐
│                    Browser                               │
│  ┌─────────────────────────────────────────────────┐    │
│  │              AudioWorklet Thread                 │    │
│  │  ┌─────────────────────────────────────────┐    │    │
│  │  │  synth_worklet.js                        │    │    │
│  │  │  ┌─────────────────────────────────┐    │    │    │
│  │  │  │  synth_sim.wasm                  │    │    │    │
│  │  │  │  (Cross-compiled embedded code)  │    │    │    │
│  │  │  └─────────────────────────────────┘    │    │    │
│  │  └─────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────┘    │
│                         │                                │
│                         ▼                                │
│                   WebAudio Output                        │
└─────────────────────────────────────────────────────────┘
```

### 使用方法

1. WASMをビルド:
```bash
xmake config --plat=wasm
xmake build synth_example
```

2. サーバー起動:
```bash
python3 -m http.server 8088 --directory examples
```

3. ブラウザで開く:
```
http://localhost:8088/workbench/synth_sim.html
```

## 2. Renode Backend

サイクル精度のハードウェアシミュレーション。実際のARMバイナリを実行し、
WebSocketブリッジ経由でブラウザに接続します。

```
┌─────────────────────────────────────────────────────────┐
│                    Browser                               │
│  ┌─────────────────────────────────────────────────┐    │
│  │  synth_sim.html + renode_adapter.js             │    │
│  │  WebSocket ←────────────────────────────────────┼────┤
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼ WebSocket (ws://localhost:8089)
┌─────────────────────────────────────────────────────────┐
│              Renode Web Bridge (Python)                  │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐         │
│  │   Audio    │  │    MIDI    │  │  Control   │         │
│  │   Socket   │  │   Socket   │  │   Socket   │         │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘         │
│        └───────────────┼───────────────┘                 │
│                        ▼                                 │
│  ┌─────────────────────────────────────────────────┐    │
│  │                  Renode                          │    │
│  │  ┌────────────────────────────────────────┐     │    │
│  │  │ STM32F4 (Cortex-M4F @ 168MHz)          │     │    │
│  │  │  ├── Flash: synth_example.elf          │     │    │
│  │  │  ├── I2S2: Audio output                │     │    │
│  │  │  ├── USART2: Shell/Debug               │     │    │
│  │  │  └── ADC1: Control inputs              │     │    │
│  │  └────────────────────────────────────────┘     │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

### 使用方法

1. ARMバイナリをビルド:
```bash
xmake config --plat=cross --arch=cortex-m4
xmake build synth_example
```

2. Audio Bridgeを起動（ターミナル1）:
```bash
python3 renode/scripts/audio_bridge.py
```

3. Web Bridgeを起動（ターミナル2）:
```bash
python3 renode/scripts/web_bridge.py
```

4. Renodeを起動（ターミナル3）:
```bash
renode --console --disable-xwt renode/synth_audio.resc
```

5. ブラウザで開く:
```
http://localhost:8088/workbench/synth_sim.html?backend=renode
```

### Renodeの利点

- **サイクル精度**: 実際のCPUサイクルをシミュレート
- **GDBデバッグ**: ブレークポイント、ステップ実行、メモリ検査
- **ペリフェラル**: I2S、DMA、ADC、Timerを正確にシミュレート
- **マルチコア**: 複数のCPUを同時シミュレート可能

## 3. Cortex-M Backend（Web完結）

純粋なJavaScriptでCortex-Mを エミュレート。サーバー不要でブラウザのみで動作。

```
┌─────────────────────────────────────────────────────────┐
│                    Browser                               │
│  ┌─────────────────────────────────────────────────┐    │
│  │  synth_sim.html + cortexm_adapter.js            │    │
│  │  ┌─────────────────────────────────────────┐    │    │
│  │  │  rp2040js / thumbulator.ts              │    │    │
│  │  │  ┌─────────────────────────────────┐    │    │    │
│  │  │  │  synth_example.elf (ARM binary) │    │    │    │
│  │  │  └─────────────────────────────────┘    │    │    │
│  │  └─────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────┘    │
│                         │                                │
│                         ▼                                │
│                   WebAudio Output                        │
└─────────────────────────────────────────────────────────┘
```

### 実装予定

このバックエンドは将来的に以下のライブラリを使用して実装予定:

- **rp2040js**: Raspberry Pi Pico (RP2040/Cortex-M0+) エミュレータ
  - https://github.com/wokwi/rp2040js

- **thumbulator.ts**: Thumb命令セットエミュレータ
  - https://github.com/DirtyHairy/thumbulator.ts

## ファイル構成

```
tools/renode/
├── stm32f4.repl              # 基本プラットフォーム定義
├── stm32f4_audio.repl        # オーディオペリフェラル追加版
├── synth.resc                # 基本起動スクリプト
├── synth_audio.resc          # オーディオブリッジ版
├── peripherals/
│   └── i2s_audio.py          # I2Sペリフェラル（Python）
└── scripts/
    ├── audio_bridge.py       # オーディオ再生ブリッジ
    ├── adc_injector.py       # ADC値インジェクター
    └── web_bridge.py         # WebSocket-Renodeブリッジ

examples/synth/
├── synth_worklet.js          # WASMバックエンド用ワークレット
├── renode_adapter.js         # Renodeバックエンド用アダプター
├── backend_manager.js        # バックエンド管理・統一API
└── synth_sim.wasm            # クロスコンパイル済みWASM

examples/workbench/
└── synth_sim.html            # 統一WebUI
```

## API統一

すべてのバックエンドは`BackendInterface`を実装し、同一のAPIを提供:

```javascript
class BackendInterface {
    async start()                    // シミュレーション開始
    stop()                           // シミュレーション停止
    sendMidi(data)                   // MIDI送信
    noteOn(note, velocity)           // ノートオン
    noteOff(note)                    // ノートオフ
    setParam(id, value)              // パラメータ設定
    getState()                       // カーネル状態取得
    getAudioContext()                // AudioContext取得
    getAnalyzer()                    // アナライザーノード取得
    isPlaying()                      // 再生中か
}
```

## バックエンド切り替え

UIでバックエンドを切り替えるか、URLパラメータで指定:

```
# WASM（デフォルト）
http://localhost:8088/workbench/synth_sim.html

# Renode
http://localhost:8088/workbench/synth_sim.html?backend=renode

# Cortex-M（将来）
http://localhost:8088/workbench/synth_sim.html?backend=cortexm
```
