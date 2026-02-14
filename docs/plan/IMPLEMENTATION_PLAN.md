# UMI ライブラリ クリーンスレート実装計画書

**バージョン:** 1.2.0
**作成日:** 2026-02-14
**最終更新日:** 2026-02-14
**前提仕様:** [LIBRARY_SPEC.md](LIBRARY_SPEC.md) v1.4.0

---

## 1. 方針

### 1.1 基本原則

**互換性を考慮せず、LIBRARY_SPEC v1.3.0 に完全準拠した12ライブラリを新規構築する。**

- 現行の `lib/` と `lib/umi/` を**アーカイブ**し、参照元として保持する
- 新規 `lib/` にゼロから12ライブラリを構築する
- 現行コードが再利用できる場合は**コピーして必要な修正を加える**（git mv ではなく cp + edit）
- ビルドシステム (xmake.lua) も新規作成する

### 1.2 アーカイブ戦略

```
lib/
├── _archive/                    ← 現行ライブラリコード全体を退避
│   ├── standalone/              ← 現行 lib/ 直下の7ライブラリ
│   │   ├── umitest/
│   │   ├── umibench/
│   │   ├── umirtm/
│   │   ├── umimmio/
│   │   ├── umihal/
│   │   ├── umiport/
│   │   └── umidevice/
│   ├── umi/                     ← 現行 lib/umi/ 全体
│   └── docs/                    ← 現行 lib/docs/
├── umicore/                     ← 新規構築
├── umihal/                      ← 新規構築
│ ...

_archive/
└── examples/                    ← 現行 examples/ 全体を退避
    ├── stm32f4_kernel/          ← stm32f4_os として再構築
    └── headless_webhost/        ← 新規再構築

examples/
├── stm32f4_os/                  ← 新規構築 (旧 stm32f4_kernel)
└── headless_webhost/            ← 新規構築
```

**アーカイブの役割:**
- 実装の参照元（コピー＆修正のソース）
- 差分確認用（新旧の比較）
- 全フェーズ完了後に削除

### 1.3 完了基準

各ライブラリが以下を**全て**満たすこと:

- [ ] LIBRARY_SPEC v1.3.0 §8.1 UMI Strict Profile 準拠
- [ ] `xmake build <target>` 成功
- [ ] `xmake test` で当該ライブラリのテストが通過
- [ ] §4.2 依存マトリクスと `add_deps` が一致
- [ ] Doxygen 生成がエラーなし

---

## 2. 現行資産の評価

### 2.1 そのままコピーできるもの（高品質）

| ライブラリ | 現行パス | 品質 | 方針 |
|-----------|---------|:----:|------|
| **umimmio** | lib/umimmio/ | ★★★★★ | **コピー + 最小修正** — 全ドキュメント完備、テスト・例あり。依存なしのためそのまま動く |
| **umibench** | lib/umibench/ | ★★★★☆ | **コピー + INDEX.md, TESTING.md 追加** — Doxyfile, ja/ あり |
| **umirtm** | lib/umirtm/ | ★★★★☆ | **コピー + INDEX.md, TESTING.md 追加** — Doxyfile, ja/ あり |
| **umitest** | lib/umitest/ | ★★★★☆ | **コピー + INDEX.md, TESTING.md 追加** — Doxyfile, ja/ あり |

### 2.2 コピー＋大幅リファクタが必要なもの

| ライブラリ | 現行パス | 品質 | 方針 |
|-----------|---------|:----:|------|
| **umidsp** | lib/umi/dsp/ | — | **include/ をコピー + ドキュメント全新規** — 実装 13,440 行は良好だが include/ 以外の構造が仕様不適合 |
| **umidi** | lib/umi/midi/ | — | **include/ をコピー + ドキュメント全新規** — 実装 11,692 行は良好 |
| **umiusb** | lib/umi/usb/ | — | **include/ をコピー + ドキュメント全新規** — 実装 10,386 行。USB HAL 部分を分離する必要あり |

### 2.3 既存コード参照しつつ新規構築

| ライブラリ | 参照元 | 方針 |
|-----------|-------|------|
| **umicore** | lib/umi/core/ (2,111行), lib/umi/shell/ (516行), lib/umi/util/ (427行) | 新規ディレクトリ構造で構築。既存ファイルから型定義・Concept を選択的にコピー |
| **umihal** | lib/umihal/ (81行), lib/umi/port/ の Concept 定義部分 | 新規構築。現行は stub のため、lib/umi/port/ から Concept を抽出 |
| **umiport** | lib/umi/port/ (17,041行) | 新規 2 軸構造 (pal/driver + arch/core/mcu) で構築。MCU DB + PAL 生成パイプラインを含む。最大のリファクタ対象 |
| **umidevice** | lib/umidevice/ (41行), lib/umi/port/device/ | 新規構築。コーデックドライバをコピー |
| **umios** | lib/umi/kernel/ + runtime/ + service/ + adapter/ + app/ + boot/ + crypto/ + fs/ (~25,000行) | 新規内部5層構造で構築。最大規模 |

---

## 3. フェーズ構成

依存関係の下位から上位へ実装する。Phase 2 は 2 サブフェーズ + 延期マイルストーンに分割。

```
Phase 0 : アーカイブ + ビルド基盤
Phase 1 : L0 + L1 (依存なし: 6ライブラリ)
Phase 2a: umiport 骨格 + 手書き PAL (最小動作スライス)
Phase 2b: MCU データベース + ボード定義の Lua 化 + umidevice
Phase 3 : L3 (umidsp + umidi + umiusb)
Phase 4 : L4 + バンドル + 統合検証 (umios + umi.base 等)

延期マイルストーン（複数 MCU ファミリ対応時に実行）:
Phase 2c: PAL コード生成パイプライン (SVD → umimmio C++)
```

> **Phase 2c 延期の理由:** PAL コード生成パイプライン（SVD パーサー + Python + Jinja2 + パッチシステム）は、STM32F4 一つのみの段階では ROI が合わない。Phase 2a の手書き PAL で十分に動作し、ドライバ開発に支障はない。3つ以上の MCU ファミリ（STM32H7, RP2040 等）を対応する必要が生じた時点で着手する。gen/ のディレクトリ構造と設計は LIBRARY_SPEC §6.4, §7.3.2 に規定済みであり、延期しても設計意図は保持される。

---

## Phase 0: アーカイブとビルド基盤

**目的:** 現行コードを退避し、新規ビルドシステムの骨格を作る。

### P0-1: 現行コードのアーカイブ

```bash
mkdir -p lib/_archive
mv lib/umitest    lib/_archive/standalone/umitest
mv lib/umibench   lib/_archive/standalone/umibench
mv lib/umirtm     lib/_archive/standalone/umirtm
mv lib/umimmio    lib/_archive/standalone/umimmio
mv lib/umihal     lib/_archive/standalone/umihal
mv lib/umiport    lib/_archive/standalone/umiport
mv lib/umidevice  lib/_archive/standalone/umidevice
mv lib/umi        lib/_archive/umi
mv lib/docs       lib/_archive/docs
mv examples        _archive/examples
```

**注意:** `examples/` もアーカイブ対象とする。ライブラリ同様にクリーンスレートで再構築し、新ライブラリに合わせた構造で新規作成する。現行の `examples/stm32f4_kernel/` は `examples/stm32f4_os/` としてリネーム・再構築する。

### P0-2: ルート xmake.lua のリセット

```lua
-- xmake.lua (新規)
set_project("umi")
set_version("0.3.0")   -- メジャーリファクタ: 0.2.0 → 0.3.0
set_xmakever("2.8.0")
set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_warnings("all", "extra", "error")

-- Phase 1 で順次追加
-- includes("lib/umitest")
-- includes("lib/umibench")
-- ...
```

### P0-3: テストの一時退避

現行 `xmake test` の 8 テストが全て失敗するため、Phase 1 開始前に空のテスト状態にする。新ライブラリのテスト追加と同時に `xmake test` を復活させていく。

**成功基準:**
- [ ] `lib/_archive/` に現行ライブラリコード全体が退避されている
- [ ] `_archive/examples/` に現行 examples が退避されている
- [ ] `xmake build` がエラーなし（ターゲットなしで正常終了）
- [ ] `git status` でアーカイブの移動が追跡されている

---

## Phase 1: L0 + L1（依存なしの6ライブラリ）

**目的:** 依存ゼロのライブラリを構築し、基盤を確立する。

### P1-1: umitest（L0 — コピー）

**ソース:** `lib/_archive/standalone/umitest/`
**作業:**
1. `cp -r lib/_archive/standalone/umitest lib/umitest`
2. INDEX.md を新規作成（API リファレンスマップ）
3. TESTING.md を新規作成（テスト戦略）
4. compile-fail テスト追加（Concept 定義ライブラリのため: §8.2）
5. xmake.lua に `includes("lib/umitest")` を追加
6. `xmake test` で umitest テスト通過を確認

**検証:**
- [ ] `xmake build umitest` 成功
- [ ] テスト通過
- [ ] UMI Strict Profile チェックリスト全項目 OK

### P1-2: umibench（L0 — コピー）

**ソース:** `lib/_archive/standalone/umibench/`
**作業:**
1. `cp -r lib/_archive/standalone/umibench lib/umibench`
2. INDEX.md, TESTING.md を新規作成
3. xmake.lua に `includes("lib/umibench")` を追加

**検証:**
- [ ] `xmake build umibench*` 成功
- [ ] テスト通過

### P1-3: umirtm（L0 — コピー）

**ソース:** `lib/_archive/standalone/umirtm/`
**作業:** umibench と同パターン

### P1-4: umimmio（L1 — コピー）

**ソース:** `lib/_archive/standalone/umimmio/`
**作業:**
1. `cp -r lib/_archive/standalone/umimmio lib/umimmio`
2. 唯一の ★★★★★ — 修正不要の見込み
3. xmake.lua に `includes("lib/umimmio")` を追加

**検証:**
- [ ] `xmake build umimmio` 成功
- [ ] 全テスト通過
- [ ] Doxygen 生成成功

### P1-5: umicore（L1 — 新規構築）

**ソース（参照）:**
- `lib/_archive/umi/core/` — audio_context.hh, processor.hh, types.hh, error.hh, event.hh, irq.hh 等
- `lib/_archive/umi/shell/` — shell.hh, commands.hh
- `lib/_archive/umi/util/` — time.hh 等（一部のみ）

**ターゲット構造:**
```
lib/umicore/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
│       └── README.md
├── include/umicore/
│   ├── core.hh              # 統合ヘッダ
│   ├── types.hh             # 基本型 (Sample, SampleRate, etc.)
│   ├── error.hh             # Result<T>, ErrorCode
│   ├── event.hh             # Event, EventQueue
│   ├── audio_context.hh     # AudioContext
│   ├── processor.hh         # ProcessorLike concept
│   ├── shared_state.hh      # SharedParamState
│   ├── time.hh              # 時間変換
│   ├── irq.hh               # 割り込み抽象 (既存 lib/umi/core/irq.hh を移設)
│   └── shell.hh             # シェル基盤
├── tests/
│   ├── xmake.lua
│   └── test_core.cc
└── examples/
    └── minimal/
```

**作業:**
1. ディレクトリ構造を作成
2. `lib/_archive/umi/core/` から各 .hh をコピー
3. 名前空間を `umi::core` に統一（必要に応じてリネーム）
4. `lib/_archive/umi/shell/include/` から shell 関連をコピー
5. `lib/_archive/umi/core/irq.hh` を移設（既存ファイル、§5 確認済み）
6. 統合ヘッダ `core.hh` を作成
7. xmake.lua を作成（headeronly, 依存なし）
8. 全ドキュメント新規作成
9. ユニットテスト作成

**注意点:**
- 現行 `umi.core` は kernel/, adapter/ も含んでいるが、**新 umicore にはそれらを含めない**
- kernel 関連は umios (Phase 4) で構築する
- ProcessorLike concept の定義がここ、実装は各ライブラリ

**検証:**
- [ ] `xmake build umicore` 成功
- [ ] テスト通過
- [ ] 依存ゼロ（`add_deps` なし）

### P1-6: umihal（L1 — 新規構築）

**ソース（参照）:**
- `lib/_archive/standalone/umihal/` — 現行 stub (81行)
- `lib/_archive/umi/port/` — Concept 定義を抽出

**ターゲット構造:**
```
lib/umihal/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
│       └── README.md
├── include/umihal/
│   ├── hal.hh               # 統合ヘッダ
│   ├── arch.hh              # アーキテクチャ検出
│   ├── audio.hh             # AudioBackendLike concept
│   ├── board.hh             # BoardLike concept
│   ├── codec.hh             # CodecLike concept
│   ├── fault.hh             # FaultHandlerLike concept
│   ├── gpio.hh              # GpioLike concept
│   ├── i2c.hh               # I2cLike concept
│   ├── i2s.hh               # I2sLike concept
│   ├── interrupt.hh         # InterruptControllerLike concept
│   ├── result.hh            # HAL Result types
│   ├── timer.hh             # TimerLike concept
│   ├── uart.hh              # UartLike concept
│   └── concept/
│       ├── clock.hh         # ClockLike concept
│       ├── codec.hh         # AudioCodecLike concept
│       ├── platform.hh      # PlatformLike concept
│       ├── transport.hh     # TransportLike concept
│       ├── uart.hh          # UartTransportLike concept
│       └── usb.hh           # UsbDeviceLike concept
├── tests/
│   ├── xmake.lua
│   ├── test_hal_concepts.cc
│   └── compile_fail/        # Concept 検証: 不正な型がエラーになること
│       └── test_invalid_gpio.cc
└── examples/
    └── minimal/
```

**作業:**
1. `lib/_archive/umi/port/` から Concept 定義（`template <typename T> concept XxxLike`）を抽出
2. 各 Concept を個別ヘッダに分離して配置
3. USB Concept (`UsbDeviceLike`) を `concept/usb.hh` に追加
4. compile-fail テストを作成（§8.2 必須）
5. 全ドキュメント新規作成

**注意点:**
- **実装コードは一切含めない** — Concept 定義と型のみ
- 現行 umihal は stub のため、実質的に lib/umi/port/ から Concept を抽出する作業

**検証:**
- [ ] `xmake build umihal` 成功
- [ ] compile-fail テスト通過
- [ ] 依存ゼロ

### Phase 1 完了基準

- [ ] 6 ライブラリ全てが `xmake build` + `xmake test` 通過
- [ ] 6 ライブラリ全てが UMI Strict Profile 準拠
- [ ] 依存マトリクス: 全て依存ゼロ（L0, L1 は相互依存なし）

---

## Phase 2: L2（プラットフォーム抽象化）

**目的:** ハードウェア抽象化レイヤーを構築する。最大の構造変更を伴うフェーズ。

umiport は単なるドライバ集ではなく「ハードウェアポーティングキット」全体を包含する (LIBRARY_SPEC §5 参照)。構造の複雑さに対応するため、Phase 2 を **2 サブフェーズ + 延期マイルストーン** に分割する:

```
Phase 2a: umiport 骨格 + 手書き PAL (最小動作スライス)
Phase 2b: MCU データベース + ボード定義の Lua 化 + umidevice
[延期]  Phase 2c: PAL コード生成パイプライン（3+ MCU ファミリ対応時に着手）
```

### P2-1a: umiport 骨格 + 手書き PAL（Phase 2a）

**ソース（参照）:** `lib/_archive/umi/port/` (96ファイル, 17,041行)

**目的:** PAL 生成パイプラインなしで最小限のビルドを成立させる。

**ターゲット構造:**
```
lib/umiport/
├── README.md
├── Doxyfile
├── xmake.lua                              # board.lua ルール + プラットフォーム選択
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
│       └── README.md
│
├── include/umiport/
│   ├── port.hh                            # 統合ヘッダ
│   │
│   ├── pal/                               # PAL 生成物ルート (LIBRARY_SPEC §6.4)
│   │   ├── arch/arm/cortex_m/             #   L1: アーキテクチャ共通
│   │   │   ├── nvic.hh                    #     手書き (仕様固定)
│   │   │   ├── scb.hh
│   │   │   └── systick.hh
│   │   ├── core/cortex_m4f/               #   L2: コアプロファイル固有
│   │   │   ├── dwt.hh
│   │   │   └── fpu.hh
│   │   └── mcu/stm32f4/                  #   L3: MCU ファミリ固有
│   │       └── periph/                    #     Phase 2a では手書き、Phase 2c で生成物に置換
│   │           ├── gpio.hh
│   │           ├── rcc.hh
│   │           ├── i2s.hh
│   │           └── usart.hh
│   │
│   ├── driver/                            # 手書きドライバ (PAL を #include)
│   │   ├── arch/arm/cortex_m/             #   アーキテクチャ共通ドライバ
│   │   │   └── dwt.hh                     #     サイクルカウンタ
│   │   └── mcu/stm32f4/                  #   MCU 固有ドライバ
│   │       ├── gpio_driver.hh             #     GpioLike concept を充足
│   │       ├── uart_driver.hh             #     UartLike concept を充足
│   │       ├── i2s_driver.hh
│   │       ├── system_init.hh             #     クロック初期化 (PLL パラメータは定数)
│   │       └── usb_otg.hh                #     USB OTG FS (UsbDeviceLike を充足)
│   │
│   ├── board/                             # ボード定義 (Phase 2a では手書き)
│   │   ├── host/
│   │   │   └── platform.hh
│   │   ├── wasm/
│   │   │   └── platform.hh
│   │   └── stm32f4_disco/
│   │       ├── platform.hh                #     手書き (Phase 2b で生成に置換)
│   │       ├── board.hh                   #     手書き (Phase 2b で生成に置換)
│   │       └── clock_config.hh            #     手書き (Phase 2b で生成に置換)
│   │
│   ├── platform/                          # 実行環境固有コード
│   │   ├── embedded/
│   │   └── wasm/
│   │
│   └── common/                            # 共通ユーティリティ
│       └── irq.hh                         #   MCU 固有割り込みディスパッチ
│
├── src/
│   ├── arch/cm4/handlers.cc
│   ├── common/irq.cc
│   ├── mcu/stm32f4/syscalls.cc
│   ├── host/write.cc
│   └── wasm/write.cc
│
├── tests/
│   ├── xmake.lua
│   ├── test_port.cc
│   └── compile_fail/
└── examples/
    └── minimal/
```

**作業:**
1. 2軸構造 (スコープ軸: arch/core/mcu + 種別軸: pal/driver/board/platform) を新規作成 (LIBRARY_SPEC §6.3)
2. `lib/_archive/umi/port/` から各階層にファイルをコピー・分類:
   - レジスタ定義 → `pal/`
   - HAL Concept 実装 → `driver/`
   - ボード固有定義 → `board/`
   - プラットフォーム固有コード → `platform/`
3. Concept **実装** を `driver/` に配置（umihal の Concept に対応する具象型）
4. `usb_otg.hh` (USB OTG FS) を `driver/mcu/stm32f4/` に配置（L3 umiusb ではなく L2 umiport）
5. xmake.lua に手動 MCU 選択ロジックを設定
6. `add_deps("umihal", "umimmio")` を設定
7. 全ドキュメント新規作成

**注意点:**
- これが最大の構造変更。現行は lib/umi/port/ に全てフラットに入っている
- **pal/ には手書きのレジスタ定義を配置する。** Phase 2c で自動生成物に段階的に置換する
- pal/ と driver/ の分離が最重要 — 生成物 (レジスタ定義) と手書きドライバを混在させない
- `irq.cc` は umiport に残す（umicore の irq.hh が抽象、ここが実装）

**検証:**
- [ ] `xmake build umiport` 成功（ホスト + 組込み両方）
- [ ] umihal Concept に対する具象実装がコンパイル通過
- [ ] PAL (pal/) とドライバ (driver/) が明確に分離されている
- [ ] 統合テスト: umiport 単体でのプラットフォーム選択動作

### P2-1b: MCU データベース + ボード定義の Lua 化（Phase 2b）

**目的:** board.lua → C++ 生成チェーンを確立する (LIBRARY_SPEC §6.5, §7.3)。

**追加構造:**
```
lib/umiport/
├── database/                              # MCU/ボード定義データベース (LIBRARY_SPEC §7.3)
│   ├── mcu/
│   │   └── stm32f4/
│   │       └── stm32f407vg.lua            #   MCU スペック (メモリ, クロック, ペリフェラル)
│   └── boards/
│       └── stm32f4_disco/
│           └── board.lua                  #   ボード設定 (HSE, ピン, 使用ペリフェラル)
│
└── rules/                                 # xmake ビルドルール (LIBRARY_SPEC §7.3)
    └── board.lua                          #   ボード選択 → MCU 特定 → ツールチェイン決定
```

**作業:**
1. `database/mcu/stm32f4/stm32f407vg.lua` を作成 — MCU スペック定義 (LIBRARY_SPEC §7.3.1 スキーマ準拠)
2. `database/boards/stm32f4_disco/board.lua` を作成 — ボード設定 (HSE 周波数, ピン割り当て等)
3. `rules/board.lua` を作成 — `xmake-repo/synthernet` の board.lua を参照して構築
4. 生成チェーンの実装:
   - `board.lua` → `board.hh` (constexpr 定数)
   - `board.lua` → `platform.hh` (HAL Concept 充足型)
   - `board.lua` + `mcu/*.lua` → `clock_config.hh` (PLL M/N/P/Q 自動探索)
   - `board.lua` + `mcu/*.lua` → `memory.ld` (リンカスクリプト)
5. Phase 2a の手書き `board.hh`, `platform.hh`, `clock_config.hh` を生成物に置換

**検証:**
- [ ] `xmake build stm32f4_os --board=stm32f4_disco` でボード定義から自動解決
- [ ] MCU DB スキーマが LIBRARY_SPEC §7.3.1 に準拠
- [ ] `--board=<board>` 指定でビルドが完結する（MCU → arch → ツールチェイン自動解決）
- [ ] board.lua → C++ 生成チェーン（board.hh, clock_config.hh, memory.ld）が正常動作

### ~~P2-1c: PAL コード生成パイプライン~~ → 延期マイルストーン

> **延期:** Phase 2c は複数 MCU ファミリ対応が必要になった時点で着手する。設計仕様は LIBRARY_SPEC §6.4, §7.3.2, §11.2 に規定済み。Phase 2a の手書き PAL で STM32F4 は十分に動作するため、本フェーズ計画のクリティカルパスから除外する。
>
> **着手条件:** 3つ以上の MCU ファミリ（STM32H7, RP2040 等）への対応が決定した時点
>
> **スコープ（着手時）:** SVD パーサー、Unified Device Model、Jinja2 テンプレート、xmake pal-gen タスク、手書き PAL の生成物への段階的置換。詳細は LIBRARY_SPEC §7.3.2 を参照。

### P2-2: umidevice（L2 — 新規構築）

**ソース（参照）:**
- `lib/_archive/standalone/umidevice/` (41行, stub)
- `lib/_archive/umi/port/device/` — コーデックドライバ等

**ターゲット構造:**
```
lib/umidevice/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/ ...
├── include/umidevice/
│   ├── device.hh             # 統合ヘッダ
│   ├── codec/
│   │   ├── wm8731.hh         # WM8731 Audio CODEC
│   │   └── cs43l22.hh        # CS43L22 Audio CODEC
│   └── peripheral/
│       └── ...               # その他デバイスドライバ
├── tests/
│   ├── xmake.lua
│   └── test_device.cc
└── examples/
```

**作業:**
1. `lib/_archive/umi/port/device/` からコーデックドライバをコピー
2. `add_deps("umihal", "umimmio")` を設定
3. 全ドキュメント新規作成

**検証:**
- [ ] `xmake build umidevice` 成功
- [ ] 統合テスト: コーデック初期化シーケンス（umiport + umidevice）

### Phase 2 完了基準

- [ ] umiport, umidevice が `xmake build` + `xmake test` 通過
- [ ] 依存: umiport → {umihal, umimmio}、umidevice → {umihal, umimmio}
- [ ] 組込みビルド (`is_plat("cross")`) とホストビルドの両方が成功
- [ ] 手書き PAL (pal/) とドライバ (driver/) が明確に分離されている
- [ ] MCU DB スキーマバリデーション (LIBRARY_SPEC §7.3.1) が rules/board.lua に実装されている
- [ ] `--board=<board>` 指定でビルドが完結する（MCU → arch → ツールチェイン自動解決）
- [ ] board.lua → C++ 生成チェーン（board.hh, clock_config.hh, memory.ld）が正常動作する

---

## Phase 3: L3（ドメインライブラリ）

**目的:** DSP、MIDI、USB のドメインライブラリを構築する。

### P3-1: umidsp（L3 — コピー + リファクタ）

**ソース:** `lib/_archive/umi/dsp/include/` (13,440行)

**ターゲット構造:**
```
lib/umidsp/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/ ...
├── include/umidsp/
│   ├── dsp.hh               # 統合ヘッダ
│   ├── constants.hh
│   ├── core/
│   │   ├── phase.hh
│   │   └── interpolate.hh
│   ├── filter/
│   │   ├── biquad.hh
│   │   ├── svf.hh
│   │   ├── moog.hh
│   │   ├── k35.hh
│   │   └── moving_average.hh
│   ├── synth/                # synth モジュールを吸収 (§10)
│   │   ├── oscillator.hh
│   │   └── envelope.hh
│   └── audio/
│       └── rate/
│           ├── asrc.hh
│           └── pi_controller.hh
├── tests/
│   ├── xmake.lua
│   └── test_dsp.cc
└── examples/
    └── minimal/
```

**作業:**
1. `lib/_archive/umi/dsp/include/` をコピー
2. `lib/_archive/umi/synth/include/` からシンセ関連をコピーし `synth/` に配置（§10: 吸収）
3. 名前空間を `umi::dsp` に統一（filter → `umi::dsp::filter` 等）
4. 統合ヘッダ `dsp.hh` を作成
5. `add_deps("umicore")` を設定
6. 全ドキュメント新規作成

**検証:**
- [ ] `xmake build umidsp` 成功
- [ ] フィルタ・オシレータのユニットテスト通過
- [ ] synth 吸収が完了（`umi.synth` ターゲット不要）

### P3-2: umidi（L3 — コピー + リファクタ）

**ソース:** `lib/_archive/umi/midi/include/` (11,692行)

**ターゲット構造:**
```
lib/umidi/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/ ...
├── include/umidi/
│   ├── midi.hh              # 統合ヘッダ
│   ├── core/
│   │   ├── ump.hh
│   │   ├── parser.hh
│   │   ├── sysex_assembler.hh
│   │   └── timestamp.hh
│   ├── messages/
│   ├── cc/
│   ├── codec/
│   ├── protocol/
│   │   ├── umi_sysex.hh
│   │   ├── umi_auth.hh
│   │   ├── umi_bootloader.hh
│   │   ├── umi_firmware.hh
│   │   ├── umi_session.hh
│   │   ├── umi_state.hh
│   │   ├── umi_object.hh
│   │   └── umi_transport.hh
│   └── util/
├── tests/
│   ├── xmake.lua
│   └── test_midi.cc
└── examples/
    └── minimal/
```

**作業:**
1. `lib/_archive/umi/midi/include/` をコピー
2. 名前空間を `umi::midi` に統一
3. `add_deps("umicore")` を設定
4. 全ドキュメント新規作成

**検証:**
- [ ] `xmake build umidi` 成功
- [ ] UMP パーサ、SysEx アセンブラのテスト通過

### P3-3: umiusb（L3 — コピー + HAL 分離）

**ソース:** `lib/_archive/umi/usb/include/` (10,386行)

**ターゲット構造:**
```
lib/umiusb/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/ ...
├── include/umiusb/
│   ├── usb.hh               # 統合ヘッダ
│   ├── core/
│   │   ├── types.hh
│   │   ├── device.hh
│   │   └── descriptor.hh
│   ├── audio/
│   │   ├── audio_types.hh
│   │   ├── audio_interface.hh
│   │   └── audio_device.hh
│   └── midi/
│       ├── usb_midi_class.hh
│       └── umidi_adapter.hh
├── tests/
│   ├── xmake.lua
│   └── test_usb.cc
└── examples/
    └── minimal/
```

**作業:**
1. `lib/_archive/umi/usb/include/` をコピー
2. **HAL 実装コード（stm32_otg 等）を除外** — これらは Phase 2 で umiport に配置済み
3. プロトコル層のみを残す（プラットフォーム非依存）
4. `add_deps("umicore", "umidsp")` を設定（ASRC のため umidsp に依存）
5. 全ドキュメント新規作成

**注意点:**
- 現行 `lib/umi/usb/` には `hal/stm32_otg.hh` がある → **これは umiport に移動済み**
- umiusb はプロトコルスタックのみ（プラットフォーム非依存の L3 ライブラリ）

**検証:**
- [ ] `xmake build umiusb` 成功
- [ ] USB ディスクリプタ生成のテスト通過
- [ ] HAL 実装コードが含まれていないこと

### Phase 3 完了基準

- [ ] umidsp, umidi, umiusb が `xmake build` + `xmake test` 通過
- [ ] 依存: umidsp → {umicore}、umidi → {umicore}、umiusb → {umicore, umidsp}
- [ ] L3 ライブラリに L2 (umiport, umidevice) への依存がないこと

---

## Phase 4: L4 + バンドル + 統合検証

**目的:** OS/カーネルを構築し、全体を統合する。

### P4-1: umios（L4 — 新規構築、最大規模）

**ソース（参照）:**
- `lib/_archive/umi/kernel/` (3,138行) — スケジューラ、MPU、障害処理
- `lib/_archive/umi/runtime/` (580行) — EventRouter, RouteTable
- `lib/_archive/umi/service/` (3,666行) — Audio, MIDI, Shell, Loader, Storage
- `lib/_archive/umi/adapter/` (987行) — embedded, wasm, umim
- `lib/_archive/umi/app/` (633行) — syscall, CRT0
- `lib/_archive/umi/boot/` (3,146行) — ブートローダ
- `lib/_archive/umi/crypto/` (1,002行) — SHA256/512, Ed25519
- `lib/_archive/umi/fs/` (12,734行) — ファイルシステム

**ターゲット構造:**
```
lib/umios/
├── README.md
├── Doxyfile
├── xmake.lua                     # umios (headeronly) + umios.service (static) + umios.crypto (static)
├── docs/ ...
├── include/umios/
│   ├── os.hh                     # 統合ヘッダ
│   ├── kernel/
│   │   ├── kernel.hh
│   │   ├── startup.hh
│   │   ├── monitor.hh
│   │   ├── metrics.hh
│   │   ├── fault.hh
│   │   ├── driver.hh
│   │   ├── app_header.hh
│   │   ├── syscall.hh
│   │   └── concepts.hh          # AppLoaderLike 等 (依存方向の逆転用)
│   ├── runtime/
│   │   ├── event_router.hh
│   │   ├── route_table.hh
│   │   └── param_mapping.hh
│   ├── service/
│   │   ├── audio.hh
│   │   ├── midi.hh
│   │   ├── shell.hh
│   │   ├── loader.hh
│   │   └── storage.hh
│   ├── ipc/
│   │   ├── spsc_queue.hh
│   │   ├── triple_buffer.hh
│   │   └── notification.hh
│   ├── app/
│   │   ├── syscall.hh
│   │   └── crt0.hh
│   └── adapter/
│       ├── embedded.hh
│       ├── wasm.hh
│       └── umim.hh
├── src/
│   ├── service/
│   │   └── loader.cc
│   └── crypto/
│       ├── sha256.cc
│       ├── sha512.cc
│       └── ed25519.cc
├── tests/
│   ├── xmake.lua
│   └── test_os.cc
└── examples/
    └── minimal/
```

**xmake.lua（内部ターゲット分割: LIBRARY_SPEC §5 L4, §10.1 ADR）:**
```lua
-- カーネルコア: umicore のみに依存（umiport にも依存しない）
-- kernel/ と ipc/ のみを公開 — service/ への逆依存をビルドエラーにする
target("umios.kernel")
    set_kind("headeronly")
    add_deps("umicore")
    add_headerfiles("include/(umios/kernel/**.hh)", "include/(umios/ipc/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- サービス層: kernel + umicore に依存
-- runtime/, service/, app/ を公開
target("umios.runtime")
    set_kind("headeronly")
    add_deps("umios.kernel", "umicore")
    add_headerfiles("include/(umios/runtime/**.hh)",
                    "include/(umios/service/**.hh)",
                    "include/(umios/app/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- アダプタ層: 全層を結合（umiport が必要）
target("umios.adapter")
    set_kind("headeronly")
    add_deps("umios.runtime", "umiport")
    add_headerfiles("include/(umios/adapter/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- 統合ターゲット（従来の umios と同等）
target("umios")
    set_kind("headeronly")
    add_deps("umios.kernel", "umios.runtime", "umios.adapter")
target_end()

-- サービスローダー (static)
target("umios.loader")
    set_kind("static")
    add_deps("umios.runtime")
    add_files("src/service/loader.cc")
target_end()

-- 暗号ライブラリ (static)
target("umios.crypto")
    set_kind("static")
    add_deps("umios.kernel")
    add_files("src/crypto/sha256.cc", "src/crypto/sha512.cc", "src/crypto/ed25519.cc")
target_end()
```

**ターゲット依存グラフ:**
```
umios.kernel  ← umicore のみ（umiport 非依存を保証）
umios.runtime ← umios.kernel + umicore
umios.adapter ← umios.runtime + umiport（umiport はここで初めて登場）
umios         ← kernel + runtime + adapter（統合ターゲット）
umios.loader  ← umios.runtime（static ライブラリ）
umios.crypto  ← umios.kernel（static ライブラリ）
```

**作業:**
1. 内部 5 層構造 (kernel → ipc → runtime → service → adapter) を構築
2. 各モジュールから選択的にコピー
3. **kernel → service の依存逆転をビルドシステムで強制:** `umios.kernel` は `umicore` のみに依存し、`service/` ヘッダを一切公開しない。SyscallHandler はテンプレートパラメータで注入
4. boot/ の内容を適切に配置（include/umios/ 直下、または adapter/ の一部として）
5. fs/ を src/ 内部に配置（StorageService と密結合: §10）
6. crypto/ を src/crypto/ に配置（カーネル署名検証専用: §10。`umios.crypto` → `umios.kernel` のみに依存）
7. 6ターゲット分割: `umios.kernel`, `umios.runtime`, `umios.adapter`, `umios`（統合）, `umios.loader`, `umios.crypto`（LIBRARY_SPEC §5 L4, §10.1 ADR）
8. `umios.adapter` のみが `umiport` に依存する構造を維持
9. 全ドキュメント新規作成

**注意点:**
- **最大規模のフェーズ**（参照元合計 ~25,000 行）
- 内部の依存方向（kernel が service に依存しない）を**ビルドシステムのターゲット分割で強制**（LIBRARY_SPEC §10.1 ADR: コードレビューでなくコンパイルエラーで検出）
- fs/ は WASM で非カーネル利用が必要になった場合、再分離を検討（§10）

**検証:**
- [ ] `xmake build umios.kernel` 成功（`umicore` のみに依存）
- [ ] `xmake build umios.runtime` 成功
- [ ] `xmake build umios.adapter` 成功
- [ ] `xmake build umios` 成功（統合ターゲット）
- [ ] `xmake build umios.loader` 成功
- [ ] `xmake build umios.crypto` 成功
- [ ] 統合テスト: カーネル起動 → SysTick → タスクスケジュール
- [ ] `umios.kernel` が `umiport` に依存していないこと（xmake.lua の `add_deps` で検証）
- [ ] `umios.kernel` の公開ヘッダに `service/` が含まれていないこと

### P4-2: バンドルターゲット

**場所:** `lib/umi/xmake.lua`（バンドル定義のみ）

```lua
-- lib/umi/xmake.lua (新規 — バンドル定義のみ)
target("umi.base")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi")
target_end()

target("umi.wasm.full")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi")
target_end()

target("umi.embedded.full")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi", "umiusb", "umios")
target_end()
```

**検証:**
- [ ] `xmake build umi.base` 成功
- [ ] `xmake build umi.embedded.full` 成功
- [ ] `xmake build umi.wasm.full` 成功

### P4-3: examples の再構築

**方針:** `_archive/examples/` を参照しつつ、新ライブラリ構造に合わせて新規構築する。`stm32f4_kernel` は `stm32f4_os` にリネームし、OS/App アーキテクチャの実装例とする。

**ターゲット構造:**
```
examples/
├── stm32f4_os/                # 組込み OS アプリケーション (旧 stm32f4_kernel)
│   ├── xmake.lua
│   ├── src/
│   │   ├── main.cc            # エントリポイント
│   │   └── app/               # Processor 実装 (L5 Application)
│   └── config/
│       └── app_config.hh      # アプリケーション設定
│
└── headless_webhost/          # WASM ビルド
    ├── xmake.lua
    └── src/
        └── main.cc
```

**作業:**
1. `examples/stm32f4_os/` を新規作成:
   - `xmake.lua` を作成: `add_deps("umi.embedded.full", "umios.loader", "umios.crypto")` のみで依存解決
   - `_archive/examples/stm32f4_kernel/` から Processor 実装をコピー・修正
   - 手動 `add_includedirs` や `add_files` は一切使わない（全て `add_deps` で解決）
2. `examples/headless_webhost/` を新規作成:
   - `xmake.lua` を作成: `add_deps("umi.wasm.full")` のみで依存解決
   - `_archive/examples/headless_webhost/` から参照してコピー・修正
3. ルート `xmake.lua` に `includes("examples/stm32f4_os")`, `includes("examples/headless_webhost")` を追加

**検証:**
- [ ] `xmake build stm32f4_os` 成功（組込みビルド）
- [ ] `xmake build headless_webhost` 成功（WASM ビルド）
- [ ] examples 内に手動 `add_includedirs` が存在しないこと（全て `add_deps` で解決）

### P4-4: 全体統合検証

**作業:**
1. `xmake test` で全テスト通過を確認
2. LIBRARY_SPEC §9 の全検証基準をチェック:
   - §9.1 構造的正当性
   - §9.2 ドキュメント完成度
   - §9.3 テスト完成度
   - §9.4 レイヤー規則検証
   - §9.5 PAL / コード生成検証
3. 依存マトリクス (LIBRARY_SPEC §4.2) との照合スクリプトを実行

**検証:**
- [ ] `xmake test` 全テスト通過
- [ ] 12 ライブラリ全てが UMI Strict Profile 準拠
- [ ] 依存グラフが DAG（循環なし）
- [ ] Doxygen 全ライブラリ生成成功

### P4-5: アーカイブ削除

全検証通過後:
```bash
rm -rf lib/_archive/
rm -rf _archive/examples/
```

---

## 4. フェーズ間の依存関係

```
Phase 0 ─→ Phase 1 ─→ Phase 2a ─→ Phase 2b ─→ Phase 3 ─→ Phase 4
(基盤)     (L0+L1)    (umiport    (MCU DB       (L3)       (L4+統合)
           6 libs      骨格+手書)  +board.lua    3 libs      1 lib+bundles
                                   +umidevice)

                                   [延期] Phase 2c: PAL コード生成
                                   （3+ MCU ファミリ対応時に着手）
```

**注:** Phase 3 は Phase 2a 完了のみを前提とする（MCU DB やボード定義の Lua 化とは独立）。Phase 2c は延期マイルストーンであり、クリティカルパスに含まれない。

**Phase 1 内の並列性:**
- P1-1〜P1-4（コピー系）は並列実行可能
- P1-5 (umicore)、P1-6 (umihal) は独立しており並列実行可能

**Phase 2 内の並列性と順序:**
- P2-1a (umiport 骨格 + 手書き PAL) → P2-1b (MCU DB + Lua 化 + umidevice) は順次実行
- P2-2 (umidevice) は P2-1a 完了後に着手可能（P2-1b と並列実行可能）
- umiport が最大規模のため、P2-1a を最優先で先行着手する

**Phase 3 内の並列性:**
- P3-1 (umidsp) と P3-2 (umidi) は並列実行可能
- P3-3 (umiusb) は umidsp に依存するため、P3-1 完了後に着手

---

## 5. リスクと対策

| リスク | 影響 | 対策 |
|--------|------|------|
| umiport の 2 軸構造化 (pal/driver + arch/core/mcu) が想定以上に複雑 | Phase 2 遅延 | Phase 2a/2b/2c のサブフェーズ分割で段階的に構築。Phase 2a で手書き PAL により最小動作を先行確立 |
| umios の内部依存方向が崩れる | 設計品質低下 | `umios.kernel` ターゲットが `umicore` のみに依存する分割構造により、ビルドシステムで強制（LIBRARY_SPEC §10.1 ADR） |
| 現行 examples のビルドが通らない | Phase 4 遅延 | P4-3 で手動 include パスを段階的に `add_deps` に置換 |
| fs の WASM 対応で再分離が必要 | Phase 4 スコープ拡大 | 本計画では umios 内部に配置。再分離は別計画として分離 |
| テストカバレッジの低下 | 品質リスク | 各フェーズで最低限のテストを必須化。アーカイブ内のテストを参照 |

---

## 6. 成果物サマリ

| フェーズ | 成果物 | ライブラリ数 |
|---------|--------|:----------:|
| Phase 0 | アーカイブ + ビルド基盤 | 0 |
| Phase 1 | umitest, umibench, umirtm, umimmio, umicore, umihal | 6 |
| Phase 2a | umiport (骨格 + 手書き PAL) | 1 (部分) |
| Phase 2b | umiport (MCU DB + board.lua 生成チェーン) + umidevice | 2 |
| Phase 3 | umidsp, umidi, umiusb | 3 |
| Phase 4 | umios (6ターゲット分割) + バンドル + examples 再接続 + 全体検証 | 1 (+3 bundles) |
| **合計** | | **12 ライブラリ** |
| *延期* | *Phase 2c: umiport PAL コード生成パイプライン（3+ MCU ファミリ対応時）* | *—* |

---

## 付録 A: 依存マトリクス（検証用）

Phase 完了時に各ライブラリの `add_deps` がこの表と一致することを検証する。

```
              core  hal  mmio  port  device  dsp  midi  usb  os
umicore        —    ×     ×     ×      ×      ×    ×     ×    ×
umihal         ×    —     ×     ×      ×      ×    ×     ×    ×
umimmio        ×    ×     —     ×      ×      ×    ×     ×    ×
umiport        ×    ○     ○     —      ×      ×    ×     ×    ×
umidevice      ×    ○     ○     ×      —      ×    ×     ×    ×
umidsp         ○    ×     ×     ×      ×      —    ×     ×    ×
umidi          ○    ×     ×     ×      ×      ×    —     ×    ×
umiusb         ○    ×     ×     ×      ×      ○    ×     —    ×
umios          ○    ×     ×     ○      ×      ×    ×     ×    —
```

**umios 内部ターゲットの依存（LIBRARY_SPEC §5 L4, §10.1 ADR）:**
```
umios.kernel   → umicore                 （umiport 非依存を保証）
umios.runtime  → umios.kernel, umicore
umios.adapter  → umios.runtime, umiport  （umiport はここで初めて登場）
umios          → umios.kernel, umios.runtime, umios.adapter（統合）
umios.loader   → umios.runtime           （static）
umios.crypto   → umios.kernel            （static）
```

> 上記マトリクスの `umios → {core ○, port ○}` は統合ターゲット `umios` の推移的依存を示す。実際のビルドでは `umios.kernel` が `umicore` のみに依存し、`umiport` への依存は `umios.adapter` に限定される。
