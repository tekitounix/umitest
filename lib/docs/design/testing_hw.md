# ハードウェア層テスト戦略

**ステータス:** 設計中  **策定日:** 2026-02-10
**根拠:** [hal/concept_design.md](hal/concept_design.md), [foundations/architecture.md](foundations/architecture.md)

**関連文書:**
- [hal/concept_design.md](hal/concept_design.md) — HAL Concept 定義 (テスト対象の契約)
- [foundations/architecture.md](foundations/architecture.md) — パッケージ構成
- [board/architecture.md](board/architecture.md) — ボード定義 (テスト環境の構成)
- [../../guides/TESTING_GUIDE.md](../../guides/TESTING_GUIDE.md) — 既存テストガイド (ホストテスト中心)
- [init_sequence.md](init_sequence.md) — 初期化シーケンス (テスト前提条件)

---

## 1. 目的

既存の [TESTING_GUIDE.md](../../guides/TESTING_GUIDE.md) はホスト上のユニットテストを扱う。
本文書は **HAL/PAL/Board 固有のテスト戦略** — ハードウェア抽象化層の品質保証を設計する。

---

## 2. テストピラミッド

```
                    ▲
                   ╱ ╲
                  ╱   ╲          実機テスト
                 ╱ HW  ╲         少数・高コスト・最終検証
                ╱───────╲
               ╱         ╲       シミュレータテスト
              ╱  Renode /  ╲      中間層・CI 実行可能
             ╱   QEMU      ╲
            ╱───────────────╲
           ╱                 ╲    ホストテスト
          ╱  Host Mock Tests  ╲    多数・低コスト・高速
         ╱─────────────────────╲
        ╱                       ╲  静的検証
       ╱  Compile-time Checks    ╲  static_assert・concept チェック
      ╱───────────────────────────╲
```

---

## 3. テスト層の定義

### 3.1 Layer 0: 静的検証 (コンパイル時)

**コスト:** ゼロ (コンパイルの一部)
**対象:** HAL Concept 準拠、型安全性、constexpr 正当性

```cpp
// concept_compliance_test.cc — コンパイルが通れば合格
#include <umiport/board/stm32f4-disco/platform.hh>

// HAL Concept 準拠チェック
static_assert(umi::hal::Platform<umi::port::Platform>);
static_assert(umi::hal::PlatformWithTimer<umi::port::Platform>);
static_assert(umi::hal::OutputDevice<umi::port::Platform::Output>);
static_assert(umi::hal::I2cTransport<umi::port::Platform::I2C>);

// constexpr 定数の妥当性チェック
static_assert(umi::board::Stm32f4Disco::system_clock > 0);
static_assert(umi::board::Stm32f4Disco::Memory::flash_size >= 64 * 1024);
static_assert(umi::board::Stm32f4Disco::Memory::ram_size >= 16 * 1024);
```

**適用先:**
- 全ボードの platform.hh に対して concept 準拠を検証
- PAL 生成物の constexpr 値の妥当性
- umimmio レジスタ定義のアドレス・サイズ

### 3.2 Layer 1: ホストテスト (Mock/Stub)

**コスト:** 低 (ホストで即時実行)
**対象:** ドライバロジック、状態遷移、エラーハンドリング

#### スタブ/モック戦略

HAL Concept がインターフェースとして機能するため、テスト用のモック実装を Concept に対して作成する。

```cpp
// mock_i2c.hh — I2cTransport concept を満たすモック
namespace umi::test {
struct MockI2C {
    // 記録用バッファ
    std::vector<std::tuple<uint8_t, uint8_t, std::vector<uint8_t>>> write_log;
    std::vector<uint8_t> read_data;
    hal::ErrorCode next_error = hal::ErrorCode::OK;

    hal::Result<void> write(uint8_t addr, uint8_t reg, std::span<const uint8_t> data) {
        if (next_error != hal::ErrorCode::OK) return std::unexpected(next_error);
        write_log.emplace_back(addr, reg, std::vector(data.begin(), data.end()));
        return {};
    }

    hal::Result<void> read(uint8_t addr, uint8_t reg, std::span<uint8_t> buf) {
        if (next_error != hal::ErrorCode::OK) return std::unexpected(next_error);
        std::copy_n(read_data.begin(), std::min(buf.size(), read_data.size()), buf.begin());
        return {};
    }
};
static_assert(umi::hal::I2cTransport<MockI2C>);
} // namespace umi::test
```

#### テスト対象例

| テスト対象 | モック | 検証内容 |
|-----------|--------|---------|
| CS43L22 ドライバ | MockI2C | I2C コマンドシーケンス、エラー時の挙動 |
| UartOutput | MockUart | 出力バッファリング、改行処理 |
| GPIO ドライバ | MockGPIO | ピン状態遷移、AF 設定 |
| クロック初期化 | MockRCC | PLL パラメータ計算、バスプリスケーラ |

### 3.3 Layer 2: シミュレータテスト (Renode / QEMU)

**コスト:** 中 (CI 実行可能、セットアップ必要)
**対象:** 割り込み動作、DMA 転送、タイミング依存処理

#### Renode 統合

```
テスト実行フロー:
1. xmake build stm32f4_renode     # Renode 用バイナリビルド
2. renode --disable-xwt            # ヘッドレス起動
   - .resc スクリプトで MCU 構成ロード
   - ELF バイナリをメモリにロード
3. Robot Framework テスト実行
   - UART 出力をキャプチャ
   - レジスタ値を検証
   - タイムアウトで hang 検出
```

#### Renode テストシナリオ例

```robot
*** Test Cases ***
UART Output Test
    Create Machine    stm32f4-disco.resc
    Execute Command   sysbus LoadELF @build/stm32f4_renode.elf
    Create Terminal Tester    sysbus.uart2
    Start Emulation
    Wait For Line On Uart    "UMI initialized"    timeout=5
    Wait For Line On Uart    "Self-test: PASS"    timeout=10

GPIO Toggle Test
    Create Machine    stm32f4-disco.resc
    Execute Command   sysbus LoadELF @build/gpio_test.elf
    Start Emulation
    Execute Command   sysbus.gpioPortD.LED_GREEN AssertState true
    Execute Command   emulation RunFor "0.5"
    Execute Command   sysbus.gpioPortD.LED_GREEN AssertState false
```

#### QEMU の位置づけ

| | Renode | QEMU |
|---|--------|------|
| ペリフェラル精度 | 高 (レジスタレベル) | 低 (機能レベル) |
| STM32 対応 | 公式サポート | 限定的 |
| CI 実行 | 容易 | 容易 |
| デバッガ連携 | GDB 対応 | GDB 対応 |

**決定**: Renode を主軸とし、QEMU はフォールバックのみ。

### 3.4 Layer 3: 実機テスト

**コスト:** 高 (HW 必要、手動/半自動)
**対象:** 電気的特性、実タイミング、アナログ動作

#### 自動化フロー

```
1. xmake flash-kernel              # pyOCD でフラッシュ
2. pyocd gdbserver &                # GDB サーバー起動
3. arm-none-eabi-gdb -batch         # バッチモード GDB
   -ex "target remote :3333"
   -ex "monitor reset halt"
   -ex "load"
   -ex "monitor reset run"
   -ex "monitor wait_halt 10000"    # テスト完了待ち
4. RTT 出力パース                    # テスト結果の判定
   - "PASS" / "FAIL" + 詳細ログ
```

#### RTT ベースのテスト結果伝送

```cpp
// 実機テストの結果を RTT 経由で報告
void test_i2c_codec_init() {
    auto result = codec.init();
    UMI_TEST_ASSERT(result.has_value(), "Codec init failed");
    UMI_TEST_ASSERT(codec.is_ready(), "Codec not ready after init");
    umi::rtm::println("TEST:i2c_codec_init:PASS");
}
```

---

## 4. HAL Concept コンプライアンステスト

### 4.1 テスト生成パターン

各 HAL Concept に対して、準拠テストテンプレートを提供する。
新しいボード/MCU ドライバを追加したとき、テンプレートに型を流し込むだけでテストが得られる。

```cpp
// hal_compliance_test.hh — テンプレート
namespace umi::test {

template <umi::hal::I2cTransport T>
void test_i2c_compliance(T& transport) {
    // write → read ラウンドトリップ
    uint8_t tx[] = {0x42};
    auto wr = transport.write(0x50, 0x00, tx);
    assert(wr.has_value());

    uint8_t rx[1] = {};
    auto rd = transport.read(0x50, 0x00, rx);
    assert(rd.has_value());

    // エラーケース: 無効アドレス
    auto err = transport.write(0xFF, 0x00, tx);
    assert(!err.has_value());
    assert(err.error() == hal::ErrorCode::NACK);
}

} // namespace umi::test
```

### 4.2 テストマトリクス

| Concept | Layer 0 (static) | Layer 1 (mock) | Layer 2 (Renode) | Layer 3 (実機) |
|---------|------------------|----------------|------------------|---------------|
| Platform | static_assert | — | ブート確認 | ブート確認 |
| OutputDevice | static_assert | バッファ検証 | UART 出力 | UART 出力 |
| GpioInput/Output | static_assert | 状態遷移 | LED 制御 | LED + オシロ |
| I2cTransport | static_assert | シーケンス検証 | — | I2C デバイス通信 |
| UartBasic/Async | static_assert | バッファ検証 | ループバック | ループバック |
| AudioCodec | static_assert | コマンド検証 | — | 音声出力確認 |

---

## 5. CI パイプライン統合

### 5.1 テスト実行順序

```
CI Pipeline:
┌─────────────────────────────────────────────────┐
│ Stage 1: Static (全プラットフォーム)              │
│   xmake build --check-only                      │
│   → static_assert, concept 準拠                  │
│   → 所要時間: ~30秒                              │
├─────────────────────────────────────────────────┤
│ Stage 2: Host Tests                              │
│   xmake test                                    │
│   → Mock ベースのユニットテスト                   │
│   → 所要時間: ~1分                               │
├─────────────────────────────────────────────────┤
│ Stage 3: Renode Tests                            │
│   xmake build stm32f4_renode                    │
│   renode --disable-xwt test.resc                │
│   → シミュレータ上の統合テスト                    │
│   → 所要時間: ~3分                               │
├─────────────────────────────────────────────────┤
│ Stage 4: Real HW Tests (オプション / 手動)        │
│   xmake flash-kernel && xmake test-hw           │
│   → 実機テスト (Self-hosted runner 必要)          │
│   → 所要時間: ~5分                               │
└─────────────────────────────────────────────────┘
```

### 5.2 テスト失敗時の対応

| 失敗レイヤー | ブロック範囲 | 対応 |
|-------------|------------|------|
| Layer 0 (static) | PR マージ不可 | Concept 準拠を修正 |
| Layer 1 (mock) | PR マージ不可 | ドライバロジック修正 |
| Layer 2 (Renode) | PR マージ不可 | 統合問題の調査 |
| Layer 3 (実機) | 警告のみ | シミュレータと実機の差異を記録 |

---

## 6. 未解決の設計課題

| # | 課題 | 選択肢 | 備考 |
|---|------|--------|------|
| 1 | モック自動生成 | Concept から自動生成 vs 手書き | C++ リフレクション不足で自動生成は困難 |
| 2 | Renode の STM32F4 ペリフェラルカバレッジ | 標準モデル vs カスタムモデル追加 | I2S/SAI が未サポートの可能性 |
| 3 | 実機テスト用 Self-hosted runner | GitHub Actions vs 専用サーバー | HW 接続の安定性 |
| 4 | テストカバレッジ計測 | gcov + lcov (ホスト) / Renode カバレッジ | 組込みバイナリのカバレッジ取得は困難 |
| 5 | WASM テスト環境 | Node.js vs ブラウザ vs Deno | オーディオ API のモック方法 |
