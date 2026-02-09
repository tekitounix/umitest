# UMI ハードウェア抽象化アーキテクチャ

**ステータス:** 確定版  **策定日:** 2026-02-09
**根拠:** archive/ARCHITECTURE_FINAL.md + archive/BSP_ARCHITECTURE_PROPOSAL.md + 7 設計文書の統合

**関連文書:**
- [problem_statement.md](problem_statement.md) — 現行の問題点
- [comparative_analysis.md](comparative_analysis.md) — フレームワーク横断比較
- [../hal/concept_design.md](../hal/concept_design.md) — HAL Concept 設計
- [../board/architecture.md](../board/architecture.md) — ボード定義・継承・DB アーキテクチャ
- [../board/project_structure.md](../board/project_structure.md) — ユーザープロジェクト構成

---

## 1. 設計原則

以下の 5 原則は不変であり、全ての設計判断の土台となる。

| # | 原則 | 具体制約 |
|---|------|----------|
| 1 | **ソースにハード依存を漏らさない** | `#ifdef STM32F4` 禁止。MCU 差異は `constexpr` + `if constexpr`。フルパスインクルードはアプリ層禁止（`platform.hh` 内のみ例外） |
| 2 | **ライブラリは HW を知らない** | umirtm, umibench, umimmio, umitest は HW 依存ゼロ。出力はリンク時注入 |
| 3 | **統合は board 層のみで行う** | MCU × 外部デバイスの結合は `board/platform.hh` が唯一の統合点 |
| 4 | **ゼロオーバーヘッド** | vtable 不使用。C++ Concepts と静的ポリモーフィズムで全抽象をコンパイル時解消 |
| 5 | **パッケージは単独で意味を持つ単位** | 分離の判断軸:「独立して使えるか」+「知識の帰属先が異なるか」 |

---

## 2. パッケージ構成

7 パッケージで構成され、各パッケージが単独で意味を持つ。

| パッケージ | 役割 | HW 依存 |
|-----------|------|---------|
| umihal | HAL Concept 契約（実装なし） | なし（0 依存） |
| umimmio | 型安全 MMIO レジスタ抽象化 | なし |
| umiport | MCU/Arch/Board ポーティング（統合点） | **MCU 固有** |
| umidevice | 外部 IC ドライバ（CS43L22, WM8731 等） | なし（Transport 経由） |
| umirtm | 軽量 printf/monitor | なし |
| umibench | ベンチマーク基盤 | なし |
| umitest | テスト基盤 | なし |

### 依存関係グラフ

```
              umihal (0 deps)        umimmio (0 deps)
          ┌── Concept 定義 ──┐         │
     umiport (MCU固有)    umidevice (MCU非依存)
          └──────┬───────────┘
           platform.hh  ← board/ 内で MCU × Device を結合
                 │
           application / tests / examples
     umirtm, umibench, umitest ── 全て 0 HW 依存
```

umiport と umidevice は **同格の Hardware Driver Layer**。両者は互いを知らず、`board/platform.hh` で初めて出会う。umiport は MCU シリーズ固有、umidevice は全 MCU 共通（Mock Transport でホストテスト可能）。

---

## 3. umiport ディレクトリ構造

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/          # アーキテクチャ共通（DWT, CoreDebug, NVIC）
│   ├── mcu/stm32f4/           # MCU 固有レジスタ操作（rcc, gpio, uart, i2c）
│   └── board/                 # ボード定義
│       ├── stm32f4-disco/     # platform.hh + board.hh
│       ├── stm32f4-renode/
│       ├── host/
│       └── wasm/
├── database/                  # MCU データベース（Lua）
│   ├── index.lua              # MCU 名 → ファイルパス索引
│   ├── family/stm32f4.lua     # ファミリ共通定義
│   └── mcu/stm32f4/stm32f407vg.lua
├── boards/                    # ボード定義（Lua 側）
│   ├── stm32f4-disco/board.lua
│   └── stm32f4-renode/board.lua
├── src/
│   ├── stm32f4/               # startup.cc, syscalls.cc, sections.ld
│   ├── host/write.cc          # write_bytes() → stdout
│   └── wasm/write.cc          # write_bytes() → fwrite
├── rules/
│   ├── board.lua              # umiport.board xmake ルール
│   ├── board_loader.lua       # ボード読み込み・継承解決
│   └── memory_ld.lua          # memory.ld 生成
└── xmake.lua
```

- `arm/cortex-m/` -- 全 Cortex-M 共通。MCU/ボード追加は `mcu/` と `board/` にディレクトリを足すだけ。
- `src/<family>/` -- startup/syscalls/sections.ld は同一ファミリのボード間で共有。

---

## 4. 出力経路

ライブラリは HW を知らない。出力先の解決は **link-time 注入** で行う。

```cpp
// umirtm/include/umirtm/detail/write.hh -- umirtm はこのシンボルの存在のみを要求
namespace umi::rt::detail {
extern void write_bytes(std::span<const std::byte> data);
}
```

```
rt::println("Hello {}", 42)
  -> umi::rt::detail::write_bytes(data)     <-- link-time 注入点
       +-- Cortex-M: Platform::Output::putc(c)  (USART or RTT)
       +-- Host:     ::write(1, data, size)      (stdout)
       +-- WASM:     std::fwrite(data, 1, size, stdout)
```

Cortex-M 実装では `write_bytes()` + `_write()` syscall が共存。`_write()` は Cortex-M + newlib の一実装に格下げ。WASM/ESP-IDF では不要。

```cpp
// umiport/src/stm32f4/syscalls.cc -- Cortex-M 側の実装例
namespace umi::rt::detail {
void write_bytes(std::span<const std::byte> data) {
    for (auto byte : data) umi::port::Platform::Output::putc(static_cast<char>(byte));
}
}
```

---

## 参照文書

| 文書 | 内容 |
|------|------|
| archive/BSP_ANALYSIS.md | 5 アーキタイプ分類、3 ユースケース評価、共通問題パターン |
| archive/PLATFORM_COMPARISON.md | 13 システムの詳細調査・横断比較表 |
| archive/HAL_INTERFACE_ANALYSIS.md | 10 HAL のインターフェイス比較、根源的設計原理 |
| archive/review/compare.md | 7 レビュアーの合意分析、改善優先度 |
| archive/review/compare_device.md | デバイスドライバ配置分析、パッケージ構成決定 |
