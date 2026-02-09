# HAL Concept 設計

**ステータス:** 確定版  **策定日:** 2026-02-09
**分割元:** 旧 03_ARCHITECTURE.md §4, §10.2, §10.3

**関連文書:**
- [../foundations/architecture.md](../foundations/architecture.md) — 全体アーキテクチャ (設計原則、パッケージ構成)
- [../foundations/problem_statement.md](../foundations/problem_statement.md) §3 — HAL Concept の課題
- [../foundations/comparative_analysis.md](../foundations/comparative_analysis.md) §2 — HAL 設計の比較

---

## 1. 設計方針

- Concept は **Basic -> Extended -> Full** に階層化。`NOT_SUPPORTED` 逃げ道は禁止。
- sync/async/DMA は別インターフェイスとして分離（embedded-hal 1.0 準拠）。
- GPIO は InputPin/OutputPin に分離（embedded-hal + Mbed OS が独立に到達した結論）。
- Transaction が基本単位。エラーモデル: `Result<T> = std::expected<T, ErrorCode>`。

---

## 2. エラー型と代表的 Concept

```cpp
namespace umi::hal {
// --- エラー型 ---
enum class ErrorCode : uint8_t {
    OK = 0, TIMEOUT, NACK, BUS_ERROR, OVERRUN, NOT_READY, INVALID_CONFIG,
};
template <typename T> using Result = std::expected<T, ErrorCode>;

// --- Codec: 階層化の模範例 ---
template <typename T>
concept CodecBasic = requires(T& c) { { c.init() } -> std::convertible_to<bool>; };
template <typename T>
concept CodecWithVolume = CodecBasic<T> && requires(T& c, int db) {
    { c.set_volume(db) } -> std::same_as<void>;
};
template <typename T>
concept AudioCodec = CodecWithVolume<T> && requires(T& c, bool m) {
    { c.power_on() } -> std::same_as<void>;
    { c.mute(m) } -> std::same_as<void>;
};

// --- GPIO: 入出力分離 ---
template <typename T>
concept GpioInput = requires(const T& pin) { { pin.is_high() } -> std::convertible_to<bool>; };
template <typename T>
concept GpioOutput = requires(T& pin) {
    { pin.set_high() } -> std::same_as<void>; { pin.set_low() } -> std::same_as<void>;
};
template <typename T>
concept GpioStatefulOutput = GpioOutput<T> && requires(T& pin) {
    { pin.is_set_high() } -> std::convertible_to<bool>; { pin.toggle() } -> std::same_as<void>;
};

// --- UART: 実行モデル分離 ---
template <typename T>
concept UartBasic = requires(T& u, const uart::Config& cfg, uint8_t byte) {
    { u.init(cfg) } -> std::same_as<Result<void>>;
    { u.write_byte(byte) } -> std::same_as<Result<void>>;
    { u.read_byte() } -> std::same_as<Result<uint8_t>>;
};
template <typename T>
concept UartAsync = UartBasic<T> && requires(T& u, std::span<const uint8_t> data) {
    { u.write_async(data) } -> std::same_as<Result<void>>;
};

// --- Transport: umidevice が使用 ---
template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

// --- Platform ---
template <typename T>
concept OutputDevice = requires(char c) {
    { T::init() } -> std::same_as<void>; { T::putc(c) } -> std::same_as<void>;
};
template <typename T>
concept Platform = requires {
    requires OutputDevice<typename T::Output>; { T::init() } -> std::same_as<void>;
};
template <typename T>
concept PlatformWithTimer = Platform<T> && requires { typename T::Timer; };
} // namespace umi::hal
```

---

## 3. Platform Concept 完全仕様

**問題:** 現在の `Platform` concept は `Output` + `init()` のみ。ClockTree 等の検証がコンパイル時に行われない。

**解決策:** 既に `umihal/concept/clock.hh` に `ClockSource` / `ClockTree` concept が定義済み。これを Platform に段階的に統合する。

```cpp
// 現状維持: Platform は最小限のまま
template <typename T>
concept Platform = requires {
    requires OutputDevice<typename T::Output>;
    { T::init() } -> std::same_as<void>;
};

// 段階的拡張: 既存 concept を組み合わせた上位 concept を追加
template <typename T>
concept PlatformWithClock = Platform<T> && requires {
    requires ClockTree<typename T::Clock>;
};

template <typename T>
concept PlatformFull = PlatformWithClock<T> && requires {
    typename T::Timer;
    { T::name() } -> std::convertible_to<const char*>;
};
```

platform.hh での検証:

```cpp
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Clock  = stm32f4::ClockTreeImpl;  // ClockTree concept を充足
    // ...
};

static_assert(umi::hal::Platform<Platform>);           // 最小
static_assert(umi::hal::PlatformWithClock<Platform>);  // クロック検証
static_assert(umi::hal::PlatformFull<Platform>);       // フル検証
```

ホスト/WASM は `Platform` のみ充足し、ARM ボードは `PlatformFull` を充足する。Concept 階層化により既存コードを壊さずに段階的に要件を追加できる。

---

## 4. Transport Concept 詳細設計

**問題:** I2C 10-bit アドレス、SPI `flush()`、DMA 転送の扱いが未決定。

**解決策:** 既存の umimmio I2cTransport / SpiTransport 実装を分析すると、AddressType テンプレートパラメータで 8/16-bit アドレスを既にサポートしている。HAL concept もこのパターンに合わせる。

```cpp
// I2C: 7-bit は基本、10-bit は拡張 concept
template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

template <typename T>
concept I2cTransport10Bit = I2cTransport<T> && requires(T& t, uint16_t addr10) {
    { t.set_10bit_addressing(true) } -> std::same_as<void>;
};

// SPI: flush は SpiBus concept に追加（embedded-hal 1.0 準拠）
template <typename T>
concept SpiTransport = requires(T& t, std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { t.transfer(tx, rx) } -> std::same_as<Result<void>>;
    { t.select() } -> std::same_as<void>;
    { t.deselect() } -> std::same_as<void>;
};

template <typename T>
concept SpiBus = SpiTransport<T> && requires(T& t) {
    { t.flush() } -> std::same_as<Result<void>>;
};

// DMA: 別 concept（実行モデル分離の原則に従う）
template <typename T>
concept I2cTransportDma = I2cTransport<T> && requires(T& t, uint8_t addr, uint8_t reg,
                                                       std::span<const uint8_t> tx,
                                                       DmaCallback cb) {
    { t.write_dma(addr, reg, tx, cb) } -> std::same_as<Result<void>>;
};
```

10-bit アドレスは `I2cTransport10Bit` として分離することで、対応しない MCU で不要なメソッドの実装を強制しない。DMA は全く異なる実行コンテキスト（ISR コールバック）であるため別 concept とする。

### HAL Transport と mmio Transport の関係

`umi::hal::I2cTransport`（concept）と `umi::mmio::I2cTransport`（class）は名前が同じだが、異なる抽象レベルに位置する:

| 名前 | 名前空間 | 種別 | 抽象レベル |
|------|---------|------|-----------|
| `I2cTransport` | `umi::hal` | concept | バスレベル: バイト列の送受信能力 |
| `I2cTransport` | `umi::mmio` | class | レジスタレベル: アドレスフレーミング + エンディアン変換 |

mmio Transport は HAL concept を **消費する** 側であり、HAL concept 準拠の型をテンプレートパラメータとして受け取る:

```
umi::hal::I2cTransport (concept)
        ↑ satisfies
umiport::Stm32I2c (実装)
        ↓ template inject
umi::mmio::I2cTransport<Stm32I2c> (レジスタR/Wブリッジ)
        ↓ template inject
umi::device::CS43L22<mmio::I2cTransport<Stm32I2c>> (デバイスドライバ)
```

**未解決の課題:**

1. **名前衝突**: 同名の `I2cTransport` が 2 つの名前空間に存在する。mmio 側を `I2cRegisterBus` 等にリネームすることで解消可能。
2. **concept 制約の欠如**: 現在の `mmio::I2cTransport<I2C>` はテンプレートパラメータ `I2C` に対して `umi::hal::I2cTransport` concept を要求していない（duck typing）。明示的な `requires` 節の追加が望ましい。
3. **シグネチャの不一致**: HAL concept は `write(addr, reg, tx)` を要求するが、mmio Transport が実際に呼ぶのは `write(addr, payload)`（reg 引数なし）。concept のシグネチャ設計を整理する必要がある。

詳細は `umimmio/docs/DESIGN.md` セクション 10 を参照。
