# Web Audio UI 設計ガイド

## 1. ゴールと非ゴール

### ゴール

- VST/AU/CLAP っぽい操作感のタイル UI
- ノブ・スライダー中心、セクション分割、密度高め
- Web Audio と Web MIDI で動作
- モバイルからデスクトップまで同一設計で破綻しない
- 将来 Max/Pd 風ノード UI も同一基盤で共存

### 非ゴール

- 最初から DAW レベルの編集機能、タイムライン、Undo 完全実装
- 最初から全ブラウザで同一動作保証
- 最初から AudioWorklet WASM の最適解を決め打ち

---

## 2. アーキテクチャの柱

### 2.1 状態・ロジック・描画の分離

レイヤを明確に分ける。依存方向も固定する。

| レイヤ | 責務 |
|--------|------|
| `domain` | 変換、単位、スケール、範囲、量子化、MIDI マッピング規則、プリセットのスキーマ |
| `engine` | `AudioContext`、`AudioWorklet`、`Worker`、`AudioParam` への適用とスムージング |
| `ui_headless` | 入力ジェスチャ、フォーカス、キーバインド、a11y、値の更新頻度制御、ドラッグスケーリング |
| `ui_styled` | 見た目のみ、tokens のみ参照、SVG/Canvas/HTML の描画 |

> **重要**: UI は engine を直接触らない。`Param` を通す。

### 2.2 headless と styled の分離

同じノブを別スキンで差し替える前提。

**headless knob:**
- pointer 操作
- fine モード
- wheel
- double click reset
- keyboard 操作

**styled knob:**
- 表示のみ
- SVG か Canvas

> **重要**: headless が UI フレームワーク依存を持たないこと。

### 2.3 Design Tokens を単一ソース

token は設計の契約。コードの都合で増やさない。

**最低限のカテゴリ:**
- `color`
- `space`
- `radius`
- `border`
- `shadow`
- `typography`
- `motion`
- `z`

CSS Variables は出力形式。入力は tokens JSON を推奨。

---

## 3. UI 基盤の選択

### A. Web Components 中核

**向いている条件:**
- ノブやメータなど特殊 UI を複数プロジェクトで再利用したい
- フレームワークを変える可能性がある
- Shadow DOM でスタイル衝突を避けたい

**設計方針:**
- tokens は host から CSS Variables で注入
- 共有スタイルは `adoptedStyleSheets`

### B. フレームワーク中心

**向いている条件:**
- 画面の状態が複雑
- ノード UI を含めた編集機能が大きい
- DX を重視

**設計方針:**
- headless を純 TS として外出し
- UI は薄くする

### 結論

寿命を伸ばすなら Web Components か headless 純 TS を必ず入れる。

---

## 4. レスポンシブ戦略

### Container Queries を基本

viewport 基準はコンポーネント再利用を壊す。入れ物基準にする。

```css
/* tile を container として定義 */
.tile { container-type: inline-size; }

/* tile 内のレイアウトは container query で分岐 */
@container (min-width: 520px) {
  .kit { grid-template-columns: repeat(4, minmax(120px, 1fr)); }
}
```

### CSS 詳細度の破綻を避ける

Cascade Layers を使い、層で固定。

**推奨レイヤ:**
1. `reset`
2. `tokens` / `theme`
3. `components`
4. `utilities`

---

## 5. プラグイン風 UI の構成

### セクション Tile

**責務:**
- 見出し
- バイパス
- プリセット
- 折りたたみ

### パラメータ UI

**必須の部品:**
- `Knob`
- `Slider`
- `Toggle`
- `Value readout`
- `Unit formatter`

**必須の挙動:**
- double click → default
- shift → fine
- wheel
- keyboard
- value の量子化対応
- modified 状態表示

### 表示系

- `Scope`
- `Spectrum`
- `Meter`

> Waveform は要件が増えるので別枠。

---

## 6. グリッドシステム

### 基本単位

```
1 unit = 22px
パネル幅 = 16 units (352px) + padding (16px) = 368px
```

パネルは 16 カラムを基本とし、各コンポーネントは `cols × rows` でサイズを指定する。

### コンポーネントサイズ一覧

| コンポーネント | サイズ | 備考 |
|----------------|--------|------|
| **入力系** | | |
| Knob | 4×4 | 標準サイズ |
| H-Slider | 4×1, 8×1, 12×1, 16×1 | 横幅バリエーション |
| H-Slider (tall) | 4×2, 8×2, 12×2, 16×2 | ラベル付き |
| V-Slider | 2×4, 2×8 | short / tall |
| Toggle | 2×2, 4×2 | スイッチ |
| RadioGroup | 4×2, 8×2 | ボタングループ |
| Dropdown | 4×2, 8×2 | 選択メニュー |
| XYPad | 4×4, 8×8 | 2次元パッド |
| Encoder | 4×4 | 無限回転 |
| NumberInput | 4×2, 6×2, 8×2 | 数値入力 |
| **表示系** | | |
| Waveform | 4×4, 8×4, 16×4, 8×8, 16×8 | 波形表示 |
| Spectrum | 4×4, 8×4, 16×4, 8×8, 16×8 | スペクトラム |
| PeakMeter | 1×4, 1×8, 2×4, 2×8 | 縦メーター |
| StereoMeter | 2×4, 2×8, 4×4, 4×8 | ステレオメーター |
| Goniometer | 4×4, 8×8 | XYスコープ |
| PhaseMeter | 4×4 | 位相相関 |
| LEDBar | 1×4, 1×8 | LEDメーター |
| SegmentDisplay | 4×2 | 7セグ風表示 |
| **その他** | | |
| StepSequencer | 8×2, 16×2 | ステップシーケンサー |
| WaveSelector | 4×4 | 波形アイコン選択 |
| Keyboard | 12×2, 16×2 | ピアノ鍵盤 |
| Button | 2×2, 4×2, 8×2 | ボタン |
| Label | 4×1, 8×1, 16×1 | テキストラベル |
| ProgressBar | 8×1, 16×1, 8×2, 16×2 | 進捗バー |

### CSS グリッド実装

```css
.ctrl-grid {
  display: grid;
  grid-template-columns: repeat(var(--cols, 16), var(--unit));
  grid-auto-rows: var(--unit);
  gap: 0;
  justify-content: center;
}

/* コンポーネントはspan指定 */
.knob {
  grid-column: span 4;
  grid-row: span 4;
}
```

---

## 7. コンポーネント API

### 入力系

#### Knob

```typescript
Knob({
  label: string,
  param: Param,
  onGestureStart?: (param: Param) => void
}) => { el, render, setNormalized }
```

#### Slider

```typescript
Slider({
  label: string,
  param: Param,
  onGestureStart?: (param: Param) => void,
  orientation?: 'h' | 'v',  // default: 'h'
  size?: string             // '8x1', '16x2', 'short', 'tall' など
}) => { el, render, setNormalized }
```

#### Toggle

```typescript
Toggle({
  label?: string,
  size?: '2x2' | '4x2',
  defaultOn?: boolean,
  onChange?: (on: boolean) => void
}) => { el, getValue, setValue }
```

#### RadioGroup

```typescript
RadioGroup({
  options: string[],
  defaultIndex?: number,
  size?: '4x2' | '8x2',
  onChange?: (value: string, index: number) => void
}) => { el, getValue, getIndex, setIndex }
```

#### Dropdown

```typescript
Dropdown({
  label?: string,
  options: string[],
  defaultIndex?: number,
  size?: '4x2' | '8x2',
  onChange?: (value: string, index: number) => void
}) => { el, getValue, getIndex, setIndex }
```

#### XYPad

```typescript
XYPad({
  label?: string,
  size?: '4x4' | '8x8',
  onChange?: (x: number, y: number) => void  // 0..1
}) => { el, getValue, setValue }
```

#### Keyboard

```typescript
Keyboard({
  size?: '12x2' | '16x2',
  startNote?: number,  // default: 60 (C4)
  onNoteOn?: (note: number) => void,
  onNoteOff?: (note: number) => void
}) => { el, noteOn, noteOff, allNotesOff }
```

### 表示系

#### Waveform

```typescript
Waveform({
  label?: string,
  size?: '8x4' | '16x4' | '8x8' | '16x8'
}) => { el, draw: (data: Float32Array) => void }
```

#### Spectrum

```typescript
Spectrum({
  label?: string,
  size?: '8x4' | '16x4' | '8x8' | '16x8'
}) => { el, draw: (data: Uint8Array) => void }
```

#### PeakMeter

```typescript
PeakMeter({
  label?: string,
  size?: 'thin-short' | 'thin-tall' | 'wide-short' | 'wide-tall'
}) => { el, setLevel, resetClip }
```

#### StereoMeter

```typescript
StereoMeter({
  label?: string,
  size?: '2x4' | '2x8' | '4x4' | '4x8'
}) => { el, setLevel: (l: number, r?: number) => void }
```

#### Goniometer

```typescript
Goniometer({
  label?: string,
  size?: '4x4' | '8x8'
}) => { el, draw: (leftData: Float32Array, rightData: Float32Array) => void }
```

### ユーティリティ

#### Button

```typescript
Button({
  label: string,
  onClick?: () => void,
  variant?: 'default' | 'primary' | 'warn',
  size?: '2x2' | '4x2' | '8x2'
}) => { el, setLabel, setEnabled, setActive }
```

#### ProgressBar

```typescript
ProgressBar({
  label: string,
  size?: '8x1' | '16x1' | '8x2' | '16x2'
}) => { el, set: (v: number) => void, setColor }
```

#### Panel

```typescript
Panel({
  title?: string,
  actions?: Element[],
  mode?: 'fixed' | 'expand'
}) => {
  el,
  header,
  body,
  append,
  appendGrid,
  appendSection
}
```

---

## 8. Param 設計

### 正規化を唯一の真実にする

`Param` 内部は `0..1` のみ。実値は変換で得る。

**Param ファクトリ関数:**

```typescript
function createParam({
  id: string,
  label: string,
  unit?: string,
  def01: number,           // デフォルト値 (0..1)
  scale: Scale,            // 変換スケール
  format: (value: number) => string
}): Param

interface Param {
  id: string;
  label: string;
  unit?: string;
  get01(): number;                           // 現在値取得
  set01(v: number, opts?: { silent?: boolean }): void;  // 値設定
  getValue(): number;                        // 実値取得
  reset(): void;                             // デフォルトに戻す
  display(): string;                         // 表示文字列
  on(fn: (v01: number) => void): () => void; // リスナー登録
  learnName(): string;                       // MIDI Learn用ラベル
}
```

**Scale ユーティリティ:**

```typescript
const Scale = {
  linear: (min, max) => ({ toValue, to01 }),
  log: (min, max) => ({ toValue, to01 }),     // 周波数など
  db: (minDb, maxDb) => ({ toValue, to01 }),  // デシベル
};
```

**Format ユーティリティ:**

```typescript
const Format = {
  hz: (x) => x >= 1000 ? (x / 1000).toFixed(2) + ' k' : x.toFixed(0),
  db: (g) => (20 * Math.log10(Math.max(1e-6, g))).toFixed(1),
  q: (x) => x.toFixed(2),
};
```

**Engine への接続:**

`apply` メソッドではなく、`on()` でリスナーを登録して engine に委譲する。

```typescript
cutoffParam.on(() => {
  if (engine) engine.setCutoff(cutoffParam.getValue());
});
```

`AudioParam` は `setTargetAtTime` でスムージングする。

### 更新頻度の規約

- UI の `pointermove` はそのまま apply しない
- UI は 60fps でも、engine にはレート制限する
- `AudioParam` は `setTargetAtTime` か `setValueAtTime` を規約化

---

## 9. ノブ UI の実装規約

> **ここが今回の破綻点。**

### 原則

**表示は単調でなければならない**

値に応じて SVG arc の解が切り替わる方式は禁止。

### 禁止例

```javascript
// ❌ 始点と終点だけで A コマンドを生成し、largeArc を value で切り替える
const largeArc = value > 0.5 ? 1 : 0;
arc.setAttribute('d', `M ${p0.x} ${p0.y} A ${r} ${r} 0 ${largeArc} ${sweep} ${p1.x} ${p1.y}`);
```

### 推奨例

```javascript
// ✅ リングは固定描画、インジケータだけ回転させる
indicator.style.transform = `rotate(${angle}deg)`;

// ✅ もしくは stroke-dasharray / dashoffset
arc.setAttribute('stroke-dashoffset', String(circumference * (1 - value)));
```

この方式にすると「途中で膨らむ」問題は構造的に発生しない。

---

## 10. Web Audio と UI の接続

### スレッド設計

- UI は軽くする
- 重い処理は `Worker`
- 実時間処理は `AudioWorklet`

### Audio 入出力デバイス

```javascript
// デバイス一覧取得
const devices = await navigator.mediaDevices.enumerateDevices();
const inputs = devices.filter(d => d.kind === 'audioinput');

// デバイス指定で取得
const stream = await navigator.mediaDevices.getUserMedia({
  audio: { deviceId: { exact: deviceId } }
});
```

> **注意**: `AudioContext` の起動はユーザー操作イベントから。

### AudioContext の GC 防止と状態管理

ブラウザによっては、参照がなくなった `AudioContext` がガベージコレクションされる場合がある。
また、以下の状況で AudioContext が予期せず停止することがある：

| 状況 | 原因 | 対策 |
|------|------|------|
| 長時間アイドル | 一部ブラウザでGC対象になる | グローバル参照を保持 |
| `interrupted` 状態 | iOS/Safari で電話着信時などに発生 | `onstatechange` で復帰試行 |
| バックグラウンドタブ | ブラウザがリソース節約のため suspend | `visibilitychange` で復帰試行 |

```javascript
const ctx = new AudioContext();

// 1. グローバル配列に参照を保持（GC防止、実務上の保険）
if (!window.__audioContexts) window.__audioContexts = [];
window.__audioContexts.push(ctx);

// 2. 状態管理フラグ
let wasRunning = false;  // start()で明示的に開始された場合のみtrue

// 3. resume試行（wasRunning かつ suspended なら復帰を試みる）
async function tryResume() {
  if (!wasRunning) return;
  if (ctx.state !== 'suspended') return;
  try { await ctx.resume(); } catch {}
}

// 4. state変更の監視
// interrupted (iOS Safari) やその他の suspended 状態からの復帰を試みる
ctx.onstatechange = () => {
  if (ctx.state === 'suspended' && wasRunning) {
    // suspended になったが wasRunning なら復帰を試みる
    // ただしポリシーで拒否される場合もあるので、visibilitychange やUI操作時にも再試行
    tryResume();
  }
};

// 5. バックグラウンドタブからの復帰時
// wasRunning かつ suspended なら resume を試みる（ユーザー停止後は再開しない）
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') {
    tryResume();
  }
});

// 6. 明示的な開始/停止
async function start() {
  wasRunning = true;
  await tryResume();
}

async function stop() {
  wasRunning = false;  // ユーザーによる停止
  try { await ctx.suspend(); } catch {}
}

// 7. 完全終了時はクリーンアップ
async function destroy() {
  ctx.onstatechange = null;
  try { await ctx.close(); } catch {}
  const idx = window.__audioContexts?.indexOf(ctx) ?? -1;
  if (idx !== -1) window.__audioContexts.splice(idx, 1);
}
```

**重要な設計判断:**
- フラグは `wasRunning` だけで十分（`wasSuspendedBySystem` は不要）
- `wasRunning` はユーザーが `start()` を呼んだ場合のみ `true`
- `stop()` でユーザーが明示的に停止した場合、タブ復帰時に自動再開しない
- `suspended` を検知したら復帰を試みる（`interrupted` だけに依存しない）
- iOS Safari では状況によりユーザー操作なしで `resume()` が拒否される場合があるため、UI側で「タップして再開」導線を用意しておくとより堅牢

---

## 11. MIDI

### MIDI Learn

1. Learn on
2. 次の入力で binding
3. CC 相対方式も想定した抽象を用意

### 保存対象

- binding の一覧
- Param の `0..1` 値

---

## 12. ノード UI との共存設計

### 重要な分離

- **UI graph** と **audio graph** は別物
- UI graph は保存と編集のためのデータ
- audio graph は実行時にコンパイル生成される

### 実装方針

1. graph JSON を正規化して保存
2. compile で Web Audio ノードへ変換
3. 将来は AudioWorklet 内に本体を移す

---

## 13. ディレクトリ構成

```
docs/web/
├── ui.html                    # エントリーポイント（HTML + CSS + import）
├── styles/
│   ├── tokens.css             # Design Tokens
│   ├── themes.css             # テーマ（Midnight, Charcoal, Forest等）
│   ├── reset.css              # CSS Reset
│   ├── layout.css             # Panel, Grid, Topbar, Footer
│   └── components.css         # UIコンポーネントのスタイル
├── lib/
│   └── utils.js               # clamp01, $, esc等のユーティリティ
├── domain/
│   ├── index.js               # re-export
│   ├── scale.js               # Scale (linear, log, db)
│   ├── format.js              # Format (hz, db, q)
│   └── param.js               # createParam
├── engine/
│   ├── index.js               # re-export
│   ├── engine.js              # createEngine
│   └── worklet/
│       └── filter-processor.js
├── ui_headless/
│   ├── index.js               # re-export
│   └── knob_logic.js          # createKnobLogic
├── ui_components/
│   └── index.js               # 全コンポーネント（barrel export）
└── app/
    ├── index.js               # Tab/Theme切り替え、MIDI utilities
    └── state/
        ├── index.js           # re-export
        ├── midi_bindings.js   # MIDI binding永続化
        └── presets.js         # プリセット管理
```

### 実装済みコンポーネント一覧

`ui_components/index.js` で export されているコンポーネント:

**基本コントロール**
- `Knob` - ロータリーノブ（SVG描画）
- `RotarySelector` - ロータリーセレクター
- `Slider` - 水平/垂直スライダー
- `Toggle` - トグルスイッチ
- `Button` - ボタン

**入力**
- `TextInput` - テキスト入力（単行/複数行）
- `NumberInput` - 数値入力（+/-ボタン付き）
- `Dropdown` - ドロップダウンメニュー
- `RadioGroup` - ラジオボタングループ
- `SegmentedControl` - セグメントコントロール
- `MultiSelect` - 複数選択リスト

**表示**
- `Label` - テキストラベル
- `Value` - 値表示
- `Lamp` - インジケーターランプ
- `SegmentDisplay` - 7セグメント風表示

**メーター・可視化**
- `Waveform` - 波形表示
- `Spectrum` - スペクトラム表示
- `StereoMeter` - ステレオメーター
- `PeakMeter` - ピークメーター
- `VUMeter` - VUメーター
- `LEDBar` - LEDバーメーター
- `Goniometer` - ゴニオメーター（位相スコープ）
- `PhaseMeter` - 位相相関メーター
- `Scope` - オシロスコープ/スペクトラム表示
- `Meter` - 汎用メーター
- `Histogram` - ヒストグラム

**エディター**
- `EnvelopeEditor` - ADSRエンベロープエディター
- `FilterGraph` - フィルター周波数応答表示
- `StepSequencer` - ステップシーケンサー
- `XYPad` - XYパッド

**特殊**
- `Keyboard` - 鍵盤
- `WaveSelector` - 波形セレクター（アイコン付き）
- `Encoder` - エンコーダー（無限回転）
- `Shell` - コンソール/ログ表示
- `FileDropZone` - ファイルドロップゾーン
- `ProgressBar` - プログレスバー

**レイアウト**
- `Panel` - パネルコンテナ

Web Components を使うなら `ui_components` が custom elements になる。

---

## 14. 実装チェックリスト

- [x] Param は `0..1` が唯一
- [x] UI は Param の `set01` しか呼ばない
- [x] AudioParam は smoothing 規約に従う（`setTargetAtTime` 使用）
- [x] ノブ表示は rotate 方式か dashoffset 方式（dashoffset 採用）
- [x] レイアウトは container query 優先
- [ ] tokens 以外の固定値をコンポーネント内に持たない（一部残存）
- [x] MIDI Learn は binding を永続化できる
- [ ] ノード UI は JSON を正規化できる（未実装）
