# UMI Web UI 再設計提案書

**バージョン:** Draft 1.0
**作成日:** 2025-01-25

---

## 1. 現状分析

### 1.1 現在の構成

```
index.html (1600+ 行)
├── インラインCSS (~1000行)
├── HTML構造 (~500行)
└── インラインJavaScript (~数百行)

lib/umi_web/
├── core/        - バックエンド管理
├── components/  - UIコンポーネント
└── theme/       - テーマ管理

js/
├── memory-display.js
├── kernel-status.js
├── hw-settings.js
└── auto-play.js
```

### 1.2 問題点

#### A. 機能の重複

| 重複項目 | 場所1 | 場所2 | 問題 |
|---------|-------|-------|------|
| MIDIカウンタ | I/O Bar (`midiRxIO/TxIO`) | MIDI Monitor Panel (`midiRx/Tx`) | 同じ情報が2箇所に表示 |
| MIDIアクティビティLED | I/O Bar (`midiRxActivityIO`) | Monitor Panel (`midiRxActivity`) | 4つのLEDが同時点滅 |
| Audio状態表示 | I/O Bar (`audioIOIndicator`) | System Panel (`audioIndicator`) | 状態の二重表示 |
| デバイス選択 | I/O Bar (select) | コンポーネント内 | 責務分散 |
| **バックエンド/アプリ選択** | Backend Selector | App Select | 概念的に重複（後述） |

#### B. 情報過多・階層の深さ

- System パネル内に5つのdetails要素（Kernel/Memory/Audio/Power/Processor）
- 3カラムレイアウトで情報が散在
- HW Configuration の設定項目が多すぎる（通常ユーザーには不要）

#### C. 技術的問題

- **巨大な単一HTMLファイル**: 1600行超、メンテナンス困難
- **インラインCSS**: 1000行超、再利用性なし
- **モダン機能未活用**: CSS Container Queries, `<dialog>`, `:has()` など
- **レスポンシブ対応不十分**: 固定px値多用、モバイル考慮不足

#### D. UX問題

- 起動時に「Loading...」だけで何が起きているか不明
- エラー発生時のフィードバック不足
- 重要な操作（Start/Stop）と副次的設定の視覚的区別なし

#### E. バックエンド/アプリ選択の概念的重複

現在の実装では以下の2つの選択UIが存在:

1. **Backend Selector**: UMIM / UMIOS / Renode / Hardware
2. **App Select**: synth_sim など（apps.json から読み込み）

**問題点:**
- アプリには対応するバックエンドが決まっている（apps.json の `backend` フィールド）
- ユーザーは「何を動かすか」を選びたいだけで、バックエンドの種類は実装詳細
- Hardware の場合はアプリ選択不要（実機のファームウェアが決定）

**解決策: ターゲット選択に統合**

```
現在:  [Backend: UMIOS ▾] [App: Synth ▾]  → 2つの選択が必要

改善:  [Target ▾]
       ├─ 🖥️ Synth Simulator (WASM)     ← アプリ + バックエンド自動選択
       ├─ 🖥️ Drum Machine (WASM)
       ├─ 🔧 Renode Emulator             ← 特殊バックエンド
       └─ 🔌 USB Device: UMI-Synth       ← 実機（検出時のみ表示）
```

**統合の利点:**
- ユーザーは1クリックで目的のターゲットを選択
- バックエンドは内部で自動決定
- 実機接続時は自動検出してリストに追加

---

## 2. 設計方針

### 2.1 ユーザーペルソナと機能優先度

| ペルソナ | 主な目的 | 必要機能 |
|---------|---------|----------|
| **シンセ奏者** | 音を出して演奏 | キーボード、パラメータ、波形 |
| **開発者** | デバッグ・動作確認 | Shell、ログ、メモリ状態 |
| **評価者** | 性能評価・比較 | DSP負荷、バッファ状態、CPU使用率 |

### 2.2 機能の優先度マトリクス

```
重要度 高 ─────────────────────────────────────────> 低

使    ┌─────────────────┬─────────────────┬─────────────────┐
用    │ 🔴 Must Have    │ 🟡 Should Have  │ 🟢 Nice to Have │
頻    ├─────────────────┼─────────────────┼─────────────────┤
度    │ • Start/Stop    │ • パラメータ    │ • Sequencer     │
高    │ • ターゲット選択│ • 波形表示      │ • MIDI Learn    │
      │ • キーボード    │ • DSP負荷       │ • Auto Play     │
      ├─────────────────┼─────────────────┼─────────────────┤
      │ • Audio設定     │ • Shell         │ • HW Config     │
中    │ • MIDI入力      │ • メモリ状態    │ • 詳細ログ      │
      │                 │ • MIDI出力      │                 │
      ├─────────────────┼─────────────────┼─────────────────┤
低    │                 │ • Kernel詳細    │ • Power状態     │
      │                 │                 │ • Processor情報 │
      └─────────────────┴─────────────────┴─────────────────┘
```

---

## 3. 新設計

### 3.1 レイアウト案

```
┌──────────────────────────────────────────────────────────────┐
│ [Target ▾]  ▶Start  ■Stop             [🔊 Audio] [🎹 MIDI]  │ Header
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────┐            │
│  │              Waveform / Scope               │            │
│  └─────────────────────────────────────────────┘            │
│                                                              │
│  ┌─────────────────────────────────────────────┐            │
│  │              Virtual Keyboard               │            │
│  └─────────────────────────────────────────────┘            │
│                                                              │ Main
│  ┌─────────────────────────────────────────────┐            │
│  │ Freq ═══════════●═══      50%               │            │
│  │ Resonance ═══●══════      25%               │            │
│  │ Attack ═●═══════════       5%               │            │
│  │ Release ═════════●══      80%               │            │
│  └─────────────────────────────────────────────┘            │
│                                                              │
├───────────────────────────┬──────────────────────────────────┤
│ Status   DSP: 12%  Buf: 3 │  [⚙ Settings] [🐚 Shell] [📊 Debug] │ Footer
└───────────────────────────┴──────────────────────────────────┘
```

### 3.2 情報の階層化

#### Tier 1: 常時表示（ヘッダー/フッター）
- ターゲット選択（統合セレクタ）
- Start/Stop
- 接続状態インジケータ（Audio/MIDI）
- DSP負荷、バッファ状態

#### Tier 2: メイン領域
- 波形表示
- キーボード
- パラメータ（コンパクト表示）

#### Tier 3: モーダル/パネル（オンデマンド）
- 設定（Audio/MIDI詳細設定）
- Shell（開発者向け）
- Debug（メモリ、カーネル、ログ）

### 3.3 ファイル構成案

```
web/
├── index.html              # 最小限のHTML (<100行)
├── styles/
│   ├── main.css            # 変数・リセット・レイアウト
│   ├── components.css      # コンポーネントスタイル
│   └── themes/
│       ├── dark.css
│       └── light.css
├── app.js                  # エントリーポイント (ES modules)
└── lib/umi_web/            # 既存ライブラリ (リファクタ)
```

---

## 4. 技術仕様

### 4.1 モダンCSS活用

```css
/* Container Queries - コンポーネント自律レスポンシブ */
.param-grid {
  container-type: inline-size;
}

@container (width < 300px) {
  .param-grid {
    grid-template-columns: 1fr;
  }
}

/* :has() - 状態連動スタイル */
.panel:has(.indicator.error) {
  border-color: var(--color-error);
}

/* CSS Nesting (Native) */
.button {
  background: var(--btn-bg);

  &:hover {
    background: var(--btn-bg-hover);
  }

  &.primary {
    background: var(--color-accent);
  }
}

/* Logical Properties */
.panel {
  padding-inline: 1rem;
  margin-block-end: 1rem;
}
```

### 4.2 モダンHTML活用

```html
<!-- Dialog for Settings -->
<dialog id="settings-dialog">
  <form method="dialog">
    <h2>Audio Settings</h2>
    <!-- content -->
    <menu>
      <button value="cancel">Cancel</button>
      <button value="apply">Apply</button>
    </menu>
  </form>
</dialog>

<!-- Popover for Quick Info -->
<button popovertarget="midi-info">ℹ</button>
<div id="midi-info" popover>
  Connected: 2 inputs, 1 output
</div>

<!-- Details for Progressive Disclosure -->
<details name="debug-panels">
  <summary>Memory Details</summary>
  <!-- content -->
</details>
```

### 4.3 JavaScript構成

```javascript
// app.js - シンプルなエントリーポイント
import { UmiApp } from './lib/umi_web/app.js';

const app = new UmiApp({
  container: '#app',
  defaultBackend: 'umios',
  theme: 'dark'
});

app.mount();

// イベント駆動で疎結合
app.on('backend:change', ({ type }) => console.log(`Backend: ${type}`));
app.on('audio:start', () => console.log('Audio started'));
```

---

## 5. コンポーネント統合案

### 5.1 重複解消

| 現状 | 改善案 |
|------|--------|
| Backend Selector + App Select | **TargetSelector** に統合 |
| Audio I/O Bar + System/Audio | **StatusBar** 1つに統合 |
| MIDI I/O Bar + MIDI Monitor | **MidiPanel** に統合、詳細はexpand |
| 複数のインジケータ | **ConnectionStatus** コンポーネント |

### 5.2 新コンポーネント構成

```
components/
├── layout/
│   ├── Header.js          # ターゲット選択、コントロール
│   ├── Main.js             # 波形、キーボード、パラメータ
│   └── Footer.js           # ステータス、ナビゲーション
├── target/
│   └── TargetSelector.js   # 統合ターゲット選択（下記詳細）
├── audio/
│   ├── Waveform.js         # 既存を改善
│   ├── ParamSlider.js      # 単一スライダー
│   └── ParamGrid.js        # スライダーグリッド
├── midi/
│   ├── Keyboard.js         # 既存
│   ├── MidiStatus.js       # 接続状態 + カウンタ
│   └── MidiLog.js          # ログ表示（フィルタ付き）
├── system/
│   ├── ConnectionStatus.js # Audio/MIDI/Backend状態
│   ├── DspMeter.js         # DSP負荷表示
│   └── MemoryMap.js        # メモリマップ（開発者向け）
└── dialogs/
    ├── SettingsDialog.js   # Audio/MIDI設定
    ├── ShellDialog.js      # Shell（モーダル化）
    └── DebugDialog.js      # デバッグ情報
```

### 5.3 TargetSelector 詳細設計

**データ構造:**
```javascript
// apps.json を拡張、または動的に構築
const targets = [
  // シミュレータ（WASMアプリ）
  {
    id: 'synth-sim',
    name: 'Synth Simulator',
    icon: '🖥️',
    type: 'simulator',
    backend: 'umios',           // 自動選択
    wasmUrl: './apps/synth.wasm',
    description: 'Polyphonic synthesizer'
  },
  {
    id: 'drum-machine',
    name: 'Drum Machine',
    icon: '🥁',
    type: 'simulator',
    backend: 'umim',
    wasmUrl: './apps/drum.wasm'
  },
  // エミュレータ
  {
    id: 'renode',
    name: 'Renode Emulator',
    icon: '🔧',
    type: 'emulator',
    backend: 'renode',
    description: 'Cycle-accurate STM32F4 emulation'
  },
  // ハードウェア（動的追加）
  {
    id: 'hw-umi-synth',
    name: 'USB: UMI-Synth',
    icon: '🔌',
    type: 'hardware',
    backend: 'hardware',
    deviceName: 'UMI-Synth',    // Web MIDI から検出
    connected: true
  }
];
```

**UI表示:**
```
┌─────────────────────────────────┐
│ Target ▾                        │
├─────────────────────────────────┤
│ 🖥️ Synth Simulator        [●]  │  ← 選択中
│ 🥁 Drum Machine                 │
│ ─────────────────────────────── │
│ 🔧 Renode Emulator        [!]  │  ← 要サーバー
│ ─────────────────────────────── │
│ 🔌 USB: UMI-Synth         [✓]  │  ← 接続済み
└─────────────────────────────────┘
```

**動作:**
1. ページロード時に `apps.json` からシミュレータ一覧を読み込み
2. Web MIDI API で接続中のUMIデバイスを検出→リストに追加
3. ターゲット選択時、対応するバックエンドを自動でインスタンス化
4. Hardware選択時はデバイス名で `HardwareBackend.connectDevice()` を呼び出し

---

## 6. 段階的移行計画

### Phase 1: CSS分離 (低リスク)
1. インラインCSSを `styles/main.css` に抽出
2. CSS変数を整理・統一
3. テーマファイル分離

### Phase 2: レイアウト簡素化
1. 3カラム → シングルカラム（モバイルファースト）
2. I/O Bar廃止 → Header統合
3. 詳細パネルをモーダル/サイドパネル化

### Phase 3: コンポーネントリファクタ
1. 重複コンポーネント統合
2. Web Components化検討
3. イベント駆動アーキテクチャ導入

---

## 7. 参考: 最小実装例

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>UMI Synth</title>
  <link rel="stylesheet" href="styles/main.css">
</head>
<body>
  <div id="app">
    <!-- Header -->
    <header class="app-header">
      <div class="app-controls">
        <select id="target-select">
          <!-- 動的に構築 -->
        </select>
        <button id="start-btn" class="btn primary">Start</button>
        <button id="stop-btn" class="btn danger" hidden>Stop</button>
      </div>
      <div class="app-status">
        <span class="status-indicator" data-audio></span>
        <span class="status-indicator" data-midi></span>
      </div>
    </header>

    <!-- Main -->
    <main class="app-main">
      <section class="waveform-section">
        <canvas id="waveform"></canvas>
      </section>

      <section class="keyboard-section">
        <div id="keyboard"></div>
      </section>

      <section class="params-section">
        <div id="params" class="param-grid"></div>
      </section>
    </main>

    <!-- Footer -->
    <footer class="app-footer">
      <div class="footer-status">
        <span>DSP: <output id="dsp-load">0%</output></span>
        <span>Buf: <output id="buffer-count">0</output></span>
      </div>
      <nav class="footer-nav">
        <button popovertarget="settings-popover">Settings</button>
        <button popovertarget="shell-popover">Shell</button>
        <button popovertarget="debug-popover">Debug</button>
      </nav>
    </footer>
  </div>

  <!-- Popovers / Dialogs -->
  <div id="settings-popover" popover><!-- settings content --></div>
  <div id="shell-popover" popover><!-- shell content --></div>
  <div id="debug-popover" popover><!-- debug content --></div>

  <script type="module" src="app.js"></script>
</body>
</html>
```

---

## 8. 成功指標

| 指標 | 現状 | 目標 |
|------|------|------|
| index.html 行数 | 1600+ | < 100 |
| インラインCSS | 1000行 | 0 |
| 初期ロード時間 | 未計測 | < 1s (3G) |
| Lighthouse Performance | 未計測 | > 90 |
| モバイル対応 | 部分的 | 完全対応 |

---

*Document Version: Draft 1.0*
*Author: Claude*
