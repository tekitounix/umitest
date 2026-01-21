# UMI C++20 Concepts 設計

## 概要

UMI プロジェクトでは C++20 Concepts を活用し、vtable オーバーヘッドなしで型安全なインターフェイス制約を実現します。

本ドキュメントでは、既存の Concepts、Concept 化が必要な箇所、および追加すべき Concepts を定義します。

## 設計原則

1. **静的ディスパッチ優先** - 組込み環境でのパフォーマンスを最大化
2. **型消去は明示的に** - `AnyProcessor` のように必要な場合のみ動的ディスパッチ
3. **コンパイル時検証** - テンプレートエラーを早期に、分かりやすく
4. **ゼロコスト抽象化** - 実行時オーバーヘッドなし

---

## 既存 Concepts

### DSP Concepts (`lib/umios/core/processor.hh`)

```cpp
// オーディオ処理の基本インターフェイス
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};

// パラメータ制御可能
template<typename P>
concept Controllable = requires(P& p, ParamId id, float v) {
    { p.set_param(id, v) } -> std::same_as<void>;
};

// パラメータ情報を持つ
template<typename P>
concept HasParams = requires(P& p) {
    { p.params() } -> std::ranges::range;
};

// ポート情報を持つ
template<typename P>
concept HasPorts = requires(P& p) {
    { p.ports() } -> std::ranges::range;
};

// 状態のシリアライズ/デシリアライズ
template<typename P>
concept Stateful = requires(P& p, std::span<const std::byte> data) {
    { p.save_state() } -> std::same_as<std::vector<std::byte>>;
    { p.load_state(data) } -> std::same_as<void>;
};
```

### HAL Concepts (`lib/umiusb/include/hal.hh`)

```cpp
// HAL 抽象化
template<typename T>
concept Hal = requires(T hal) {
    { hal.init() } -> std::same_as<void>;
    // ... 詳細は umiusb/include/hal.hh 参照
};

// USB デバイスクラス
template<typename T>
concept Class = requires(T cls) {
    { cls.init() } -> std::same_as<void>;
    // ... 詳細は umiusb/include/hal.hh 参照
};
```

### 動的ディスパッチ (`lib/umios/core/processor.hh`)

```cpp
// 型消去による動的ディスパッチ（必要な場合のみ使用）
class AnyProcessor {
    // ProcessorLike を満たす任意の型を保持
    // ランタイムでの柔軟性が必要な場合に使用
};
```

---

## Concept 化が必要な箇所

以下の仮想クラスは C++20 Concepts への移行を推奨します。

### 1. GUIBackend Concept

**現状:** `lib/umigui/backend.hh` - `IBackend` 仮想クラス

**提案:**
```cpp
template<typename B>
concept GUIBackend = requires(B& b, const Rect& r, Color c) {
    { b.fill_rect(r, c) } -> std::same_as<void>;
    { b.draw_text(r, std::string_view{}) } -> std::same_as<void>;
    { b.begin_frame() } -> std::same_as<void>;
    { b.end_frame() } -> std::same_as<void>;
};
```

**詳細:** [ARCHITECTURE.md](ARCHITECTURE.md) の GUI セクション参照

### 2. ProcessorLike 統合

**現状:** `IAudioProcessor` が一部で使用されている

**提案:** 既存の `ProcessorLike` concept に統合し、仮想クラスを廃止

**詳細:** [ARCHITECTURE.md](ARCHITECTURE.md) の Processor モデル参照

### 3. Skin Concept

**現状:** `lib/umigui/skin/` - `ISkin` 仮想クラス

**提案:**
```cpp
template<typename S>
concept Skin = requires(S& s) {
    { s.button_style() } -> std::convertible_to<ButtonStyle>;
    { s.slider_style() } -> std::convertible_to<SliderStyle>;
    { s.colors() } -> std::convertible_to<ColorPalette>;
};
```

### 4. Transport Concept

**現状:** 複数のトランスポート実装で仮想クラス使用

**提案:**
```cpp
template<typename T>
concept Transport = requires(T& t, std::span<const std::byte> data) {
    { t.send(data) } -> std::same_as<bool>;
    { t.receive() } -> std::same_as<std::optional<std::vector<std::byte>>>;
    { t.is_connected() } -> std::same_as<bool>;
};
```

---

## 追加すべき Concepts

### DSP カテゴリ別 Concepts

```cpp
// 信号生成器（オシレータ、ノイズ等）
template<typename G>
concept DspGenerator = ProcessorLike<G> && requires(G& g) {
    { g.set_frequency(float{}) } -> std::same_as<void>;
};

// フィルタ
template<typename F>
concept DspFilter = ProcessorLike<F> && requires(F& f) {
    { f.set_cutoff(float{}) } -> std::same_as<void>;
    { f.set_resonance(float{}) } -> std::same_as<void>;
};

// エンベロープ
template<typename E>
concept DspEnvelope = requires(E& e) {
    { e.trigger() } -> std::same_as<void>;
    { e.release() } -> std::same_as<void>;
    { e.get_value() } -> std::same_as<float>;
    { e.is_active() } -> std::same_as<bool>;
};

// エフェクト
template<typename E>
concept DspEffect = ProcessorLike<E> && requires(E& e) {
    { e.set_mix(float{}) } -> std::same_as<void>;
};
```

### HAL Concepts

```cpp
// GPIO ポート
template<typename P>
concept GpioPort = requires(P& p, bool state) {
    { p.set(state) } -> std::same_as<void>;
    { p.get() } -> std::same_as<bool>;
    { p.toggle() } -> std::same_as<void>;
};

// I2S ドライバ
template<typename D>
concept I2sDriver = requires(D& d, std::span<const int16_t> buf) {
    { d.transmit(buf) } -> std::same_as<bool>;
    { d.receive(std::span<int16_t>{}) } -> std::same_as<bool>;
    { d.set_sample_rate(uint32_t{}) } -> std::same_as<void>;
};

// I2C ドライバ
template<typename D>
concept I2cDriver = requires(D& d, uint8_t addr, std::span<const uint8_t> data) {
    { d.write(addr, data) } -> std::same_as<bool>;
    { d.read(addr, std::span<uint8_t>{}) } -> std::same_as<bool>;
};

// SPI ドライバ
template<typename D>
concept SpiDriver = requires(D& d, std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { d.transfer(tx, rx) } -> std::same_as<bool>;
};
```

### Kernel Concepts

```cpp
// タスク
template<typename T>
concept KernelTask = requires(T& t) {
    { t.run() } -> std::same_as<void>;
    { t.get_priority() } -> std::convertible_to<int>;
};

// イベントソース
template<typename E>
concept EventSource = requires(E& e) {
    { e.poll() } -> std::same_as<std::optional<Event>>;
    { e.has_pending() } -> std::same_as<bool>;
};
```

---

## 実装ガイドライン

### Concept の命名規則

- **〜Like**: 基本的な振る舞いを定義（例: `ProcessorLike`）
- **〜able**: 特定の能力を持つ（例: `Controllable`）
- **Has〜**: 特定のプロパティを持つ（例: `HasParams`）
- **Dsp〜**: DSP 処理に関連（例: `DspFilter`）

### 使用パターン

```cpp
// テンプレート関数での使用
template<ProcessorLike P>
void run_processor(P& proc, AudioContext& ctx) {
    proc.process(ctx);
}

// 複合制約
template<typename P>
    requires ProcessorLike<P> && Controllable<P>
void setup_processor(P& proc) {
    proc.set_param(VOLUME, 0.8f);
}

// 型消去が必要な場合のみ AnyProcessor を使用
std::vector<AnyProcessor> dynamic_chain;
```

### コンパイルエラーの改善

```cpp
// static_assert で分かりやすいエラーメッセージ
template<typename P>
void process_audio(P& proc) {
    static_assert(ProcessorLike<P>,
        "P must satisfy ProcessorLike concept (requires process(AudioContext&))");
    // ...
}
```

---

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - システムアーキテクチャ
- [CODING_STYLE.md](../development/CODING_STYLE.md) - コーディングスタイル
- [IMPLEMENTATION_PLAN.md](../development/IMPLEMENTATION_PLAN.md) - 実装計画
