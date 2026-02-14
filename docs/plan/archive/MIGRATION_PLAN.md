# UMI ライブラリ構成 移行計画書

**バージョン:** 1.0.0
**作成日:** 2026-02-14
**前提:** [INVESTIGATION.md](INVESTIGATION.md), [ANALYSIS.md](ANALYSIS.md), [PROPOSAL.md](PROPOSAL.md)
**理想構成:** [LIBRARY_SPEC.md](LIBRARY_SPEC.md)
**調査方法:** 9チーム並列調査 + 3チーム × 2ラウンド監査

本文書は現行のライブラリ構成の問題を診断し、[LIBRARY_SPEC.md](LIBRARY_SPEC.md) で定義された理想構成への移行手順を定義する。

---

## 1. 現状の診断

### 1.1 構造的な二重性

UMI のライブラリ構成には**2つのライブラリシステムが並存**している。

```
現状:
┌─────────────────────────────────────────────────────────┐
│  System A: スタンドアロン (lib/ 直下)                      │
│  umihal, umiport, umimmio, umirtm, umitest, umibench,   │
│  umidevice — 高品質だが一部機能が限定的                     │
├─────────────────────────────────────────────────────────┤
│  System B: モノリシック (lib/umi/ 配下)                     │
│  24 モジュール — 包括的だがドキュメント未整備                │
│  port/, mmio/, test/ が System A と重複                    │
└─────────────────────────────────────────────────────────┘
```

### 1.2 主要問題

| # | 問題 | 深刻度 |
|---|------|--------|
| P1 | lib/umimmio と lib/umi/mmio の完全重複 | CRITICAL |
| P2 | lib/umiport と lib/umi/port の機能分裂 | CRITICAL |
| P3 | lib/umitest と lib/umi/test の二重定義 | HIGH |
| P4 | umi.kernel → umi.service の依存関係逆転 | CRITICAL |
| P5 | umi.mmio, umi_util → umi.test のテスト依存混入 | HIGH |
| P6 | umi.service, umi.runtime 等が親 xmake.lua から未インクルード | HIGH |
| P7 | 名前空間の不一致 (rt, umitest, umidi) | MEDIUM |
| P8 | LIBRARY_SPEC 準拠レベルの格差 (5/5 ～ 2/5) | MEDIUM |
| P9 | プラットフォーム抽象化パターンの不統一 | MEDIUM |
| P10 | lib/umi/bench_old/ 等のレガシーコード残存 | LOW |

### 1.3 依存関係違反

| 違反 | 現状 | 解消方法 |
|------|------|----------|
| umi.kernel → umi.service | カーネルがサービスに依存 | umios 内で統合。依存関係を kernel ← service に反転 |
| umi.mmio → umi.test | MMIO がテストに依存 | テストヘルパーを tests/ 内に移動。本体から依存削除 |
| umi_util → umi.test | ユーティリティがテストに依存 | 同上 |
| umi.adapter → umi.kernel | アダプタがカーネルに依存 | umios 内で統合。adapter は kernel の一部として扱う |
| umi.core → umi.shell | core がシェルに依存 | shell を core に吸収 (shell は小さな基盤ユーティリティ) |

---

## 2. 移行対象

### 2.1 モジュール → ライブラリ対応表

| 現行パス | 操作 | 移行先 |
|---------|------|--------|
| lib/umi/core/ | 昇格 | lib/umicore/ |
| lib/umi/shell/ | 吸収 | lib/umicore/ (シェル基盤ユーティリティ) |
| lib/umi/kernel/ | 統合 | lib/umios/include/umios/kernel/ |
| lib/umi/runtime/ | 統合 | lib/umios/include/umios/runtime/ |
| lib/umi/service/ | 統合 | lib/umios/include/umios/service/ |
| lib/umi/adapter/ | 統合 | lib/umios/include/umios/adapter/ |
| lib/umi/app/ | 統合 | lib/umios/include/umios/app/ |
| lib/umi/boot/ | 統合 | lib/umios/include/umios/ |
| lib/umi/crypto/ | 統合 | lib/umios/src/crypto/ |
| lib/umi/fs/ | 統合 | lib/umios/src/ (内部) |
| lib/umi/dsp/ | 昇格 | lib/umidsp/ |
| lib/umi/synth/ | 吸収 | lib/umidsp/include/umidsp/synth/ |
| lib/umi/midi/ | 昇格 | lib/umidi/ |
| lib/umi/usb/ | 昇格 | lib/umiusb/ |
| lib/umi/port/ | 統合 | lib/umiport/ (concepts/hal/ は umihal に一本化) |
| lib/umi/mmio/ | 削除 | umimmio が正本 |
| lib/umi/test/ | 削除 | umitest が正本 |
| lib/umi/bench_old/ | 削除 | レガシー |
| lib/umi/ref/ | 移動 | lib/umitest/ の例に移動 |

**注:** `wasm` / `embedded` 固有のコードは独立ディレクトリではなく `lib/umi/port/platform/` と `lib/umi/adapter/` に含まれており、それぞれ umiport / umios に統合される。

### 2.2 名前空間の移行

| 現行名前空間 | 移行先 |
|------------|--------|
| `rt` | `umi::rt` |
| `umitest::test` (テストコード内) | `umi::test` に統一 |
| `umidi` | `umi::midi` |

### 2.3 テスト配置の統一

| 現在のテスト | 移行先 |
|------------|--------|
| lib/umi/dsp/test/ | lib/umidsp/tests/ |
| lib/umi/midi/test/ | lib/umidi/tests/ |
| lib/umi/usb/test/ | lib/umiusb/tests/ |
| lib/umi/boot/test/ | lib/umios/tests/boot/ |
| lib/umi/fs/test/ | lib/umios/tests/fs/ |
| lib/umi/port/test/ | lib/umiport/tests/ |
| lib/umi/ref/ (umi::mock) | lib/umitest/examples/ |

### 2.4 未成熟モジュールの取り扱い

| モジュール | コード量 | 判断 |
|-----------|---------|------|
| lib/umi/coro/ | ~510行 (coro.hh 1ファイル) | 使用実績を確認。未使用ならアーカイブ |
| lib/umi/gfx/ | ~1,159行 (12ファイル) | 将来的に lib/umigfx/ として独立。当面は lib/umi/gfx/ に保持 |
| lib/umi/ui/ | ~200行 | gfx と統合して lib/umigfx/ の一部に |
| lib/umi/util/ | 4ファイル | ring_buffer.hh, triple_buffer.hh → umios/ipc/ に移動。assert.hh, log.hh → umicore に移動 |
| lib/umi/tools/ | ビルドヘルパー | lib/umi/tools/ として保持（ライブラリではない） |

### 2.5 lib/umi/ ディレクトリの最終形

移行完了後、lib/umi/ ディレクトリには **xmake.lua (便利バンドル定義のみ)** が残る:

```lua
-- lib/umi/xmake.lua (最終形)
target("umi.all")
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

---

## 3. 移行の原則

- **段階的移行** — 一度に全てを変更しない
- **既存テストの維持** — 各段階で `xmake test` が通ること
- **git ブランチ分離** — `refactor/library-architecture` ブランチで実施
- **フェーズ単位のタグ** — 各フェーズ開始前に `pre-mN` タグを打ち、ロールバック可能にする
- **フィーチャーブランチ** — 各フェーズで `refactor/m0-deps`, `refactor/m1-umicore` 等のブランチを切る

---

## 4. 移行フェーズ

### フェーズ間の依存関係

```
M0 (必須先行: 依存関係修正)
  ├─→ M1 (umicore) ──┐
  ├─→ M2 (umiport) ──┼─→ M4 (umios) ──→ M5 (クリーンアップ) ──→ M6 (品質統一)
  └─→ M3a/b/c ───────┘
       (M3 の 3 ライブラリは互いに独立、M1 完了後に並列可能)
```

---

### Phase M0: 準備 (依存関係修正)

**目的:** 現行コードの依存関係違反を修正（構造変更なし）

**具体的手順:**
1. `lib/umi/mmio/xmake.lua` から `add_deps("umi.test")` を削除。テスト用コードが本体に含まれていれば `tests/` ディレクトリに分離
2. `lib/umi/util/xmake.lua` から `add_deps("umi.test")` を削除。同様にテスト用コードを `tests/` に分離
3. `lib/umi/kernel/xmake.lua` の `add_deps` から `"umi.service"` を除去
4. `lib/umi/kernel/shell_commands.hh` の `#include <umi/service/shell/shell_commands.hh>` を削除。shell_commands は service 側で定義し、kernel は参照しない構造に変更
5. `lib/umi/kernel/syscall_handler.hh` の `#include "storage_service.hh"` と `#include "loader.hh"` を、テンプレートパラメータまたは前方宣言で置換。SyscallHandler は既に `StorageType` をテンプレートパラメータとして受け取っており、この方向性を拡張する

**成功基準:**
- [ ] `xmake test` 全テスト通過
- [ ] `git grep 'add_deps.*umi.test' lib/umi/mmio lib/umi/util` がヒットゼロ
- [ ] `git grep 'add_deps.*umi.service' lib/umi/kernel` がヒットゼロ
- [ ] `lib/umi/kernel/` 内に `#include <umi/service/` が存在しない

**ロールバック:** `git revert` でフェーズ単位で巻き戻し

---

### Phase M1: L1 新設 (umicore)

**目的:** lib/umi/core/ + lib/umi/shell/ → lib/umicore/ の昇格

**具体的手順:**
1. lib/umicore/ ディレクトリ構造を作成 ([LIBRARY_SPEC.md §5](LIBRARY_SPEC.md) のツリーに従う)
2. lib/umi/core/ の全ヘッダを lib/umicore/include/umicore/ にコピー
3. lib/umi/shell/include/umishell/ の shell_core.hh, shell_auth.hh を lib/umicore/include/umicore/ に移動 (shell 基盤ユーティリティの吸収)
4. xmake.lua を作成、`umicore` ターゲットを定義 (headeronly, 依存なし)
5. lib/umi/xmake.lua の `umi.core` ターゲットに `add_deps("umicore")` を追加し互換性維持

   **注:** 現行の `umi.core` は kernel/ や adapter/ のインクルードパスも含むため、完全なエイリアス化は M4 (umios 完成後) まで延期する。M1 では umicore を並行して追加するのみ。

6. `xmake test` で全テスト通過を確認
7. DESIGN.md, README.md 等のドキュメントを作成

**成功基準:**
- [ ] `xmake build umicore` が依存ゼロで成功
- [ ] `xmake run test_umicore` が通過
- [ ] `xmake test` 全テスト通過 (既存テスト影響なし)
- [ ] lib/umicore/ が LIBRARY_SPEC v2.0.0 の最低要件を満たす

**ロールバック:** lib/umicore/ ディレクトリを削除し、xmake.lua の変更を revert

---

### Phase M2: L2 統合 (umiport) ⚠ 最高リスク

**目的:** lib/umi/port/ → lib/umiport/ への統合

**具体的手順:**
1. 現行 lib/umiport/ のファイルを退避 (既存の HAL Concept 定義を維持)
2. lib/umi/port/ の全ファイルを lib/umiport/ に統合

   **ディレクトリ正規化マッピング:**
   | 現行パス | 移行先 |
   |---------|--------|
   | lib/umi/port/arch/cm4/ | lib/umiport/include/umiport/arch/cm4/ |
   | lib/umi/port/arch/cm7/ | lib/umiport/include/umiport/arch/cm7/ |
   | lib/umi/port/mcu/stm32f4/ | lib/umiport/include/umiport/mcu/stm32f4/ |
   | lib/umi/port/mcu/stm32h7/ | lib/umiport/include/umiport/mcu/stm32h7/ |
   | lib/umi/port/board/ | lib/umiport/include/umiport/board/ |
   | lib/umi/port/platform/ | lib/umiport/include/umiport/platform/ |
   | lib/umi/port/common/ | lib/umiport/include/umiport/common/ |
   | lib/umi/port/concepts/hal/ | 削除 (umihal に一本化) |

3. **.cc ファイルの移行:**
   | 現行パス | 移行先 |
   |---------|--------|
   | lib/umi/port/arch/cm4/arch/handlers.cc | lib/umiport/src/arch/cm4/handlers.cc |
   | lib/umi/port/arch/cm7/arch/handlers.cc | lib/umiport/src/arch/cm7/handlers.cc |
   | lib/umi/port/common/common/irq.cc | lib/umiport/src/common/irq.cc |
   | lib/umi/port/mcu/stm32f4/syscalls.cc | lib/umiport/src/stm32f4/syscalls.cc |

4. xmake.lua を更新。board.lua ルールで動的インクルードパス選択
5. examples/stm32f4_kernel/xmake.lua のハードコードパスを更新 (§6 参照)
6. lib/umi/port/ を削除
7. `xmake test` + `xmake build stm32f4_kernel` で確認

**成功基準:**
- [ ] `xmake test` 全テスト通過
- [ ] `xmake build stm32f4_kernel` 成功
- [ ] `git grep 'lib/umi/port' examples/ lib/umi/xmake.lua` がヒットゼロ
- [ ] lib/umi/port/ ディレクトリが存在しない

**ロールバック:** `pre-m2` タグに戻る。後方互換ラッパーにより段階的切り替えも可能

---

### Phase M3: L3 昇格 (umidsp, umidi, umiusb)

**目的:** DSP, MIDI, USB ライブラリの独立。3 ライブラリは互いに独立であり、並列実行可能。

**M3a: umidsp**
1. lib/umidsp/ を作成
2. lib/umi/dsp/ + lib/umi/synth/ の全ヘッダを移動
3. xmake.lua を作成 (`add_deps("umicore")`)
4. `umi.dsp`, `umi.synth` をエイリアスに変更

**M3b: umidi**
1. lib/umidi/ を作成
2. lib/umi/midi/ の全ヘッダを移動
3. xmake.lua を作成 (`add_deps("umicore")`)
4. 名前空間 `umidi` → `umi::midi` に変更 (エイリアス維持)
5. `umi.midi` をエイリアスに変更

**M3c: umiusb**
1. lib/umiusb/ を作成
2. lib/umi/usb/ の全ヘッダを移動
3. xmake.lua を作成 (`add_deps("umicore", "umidsp")`)
4. `umi.usb` をエイリアスに変更

**成功基準 (各サブフェーズ共通):**
- [ ] 各ライブラリが `xmake build <libname>` で独立ビルド成功
- [ ] `xmake test` 全テスト通過
- [ ] ドキュメント最低要件 (README.md, DESIGN.md) を満たす

**ロールバック:** ライブラリ単位で revert 可能

---

### Phase M4: L4 統合 (umios) ⚠ 最大規模

**目的:** カーネル関連モジュールの統合。規模が大きいため M4a/M4b に分割。

**M4a: kernel + ipc + runtime**
1. lib/umios/ を作成
2. lib/umi/kernel/ → lib/umios/include/umios/kernel/ に移動
3. lib/umi/runtime/ → lib/umios/include/umios/runtime/ に移動
4. lib/umi/util/ の ring_buffer.hh, triple_buffer.hh → lib/umios/include/umios/ipc/ に移動
5. xmake.lua を作成 (`add_deps("umicore", "umiport")`)

**M4b: service + adapter + app + boot + crypto + fs**
1. lib/umi/service/ → lib/umios/include/umios/service/ に移動
2. lib/umi/adapter/ → lib/umios/include/umios/adapter/ に移動
3. lib/umi/app/ → lib/umios/include/umios/app/ に移動
4. lib/umi/boot/ → lib/umios/include/umios/ に移動
5. lib/umi/crypto/ → lib/umios/src/crypto/ に移動 (sha256.cc, sha512.cc, ed25519.cc)
6. lib/umi/fs/ → lib/umios/src/ 内部に移動
7. `umi.kernel`, `umi.runtime`, `umi.service`, `umi.boot` をエイリアスに変更

**成功基準:**
- [ ] `xmake build umios` 成功
- [ ] `xmake build stm32f4_kernel` 成功
- [ ] `xmake test` 全テスト通過
- [ ] `git grep 'lib/umi/kernel\|lib/umi/runtime\|lib/umi/service' examples/` がヒットゼロ

**ロールバック:** M4a, M4b 各段階で `pre-m4a`, `pre-m4b` タグに戻る

---

### Phase M5: 重複削除・クリーンアップ

**目的:** 旧構造の完全削除

1. lib/umi/mmio/ を削除 (umimmio が正本)
2. lib/umi/test/ を削除 (umitest が正本)
3. lib/umi/bench_old/ を削除
4. lib/umi/ref/ を umitest に移動
5. lib/umi/coro/, lib/umi/gfx/, lib/umi/ui/, lib/umi/util/ を整理 (§2.4 参照)
6. lib/umi/xmake.lua をバンドル定義のみに縮小
7. lib/umi/xmake.lua 内の dead reference (bench/ への参照等) を修正

**成功基準:**
- [ ] `xmake test` 全テスト通過
- [ ] lib/umi/ 配下にバンドル定義以外のソースコードが残っていない (gfx/ui/tools は例外)
- [ ] `xmake build` で warning ゼロ

**ロールバック:** `pre-m5` タグに戻る

---

### Phase M6: 品質統一

**目的:** 全ライブラリを LIBRARY_SPEC v2.0.0 準拠に

1. 各ライブラリの README.md, DESIGN.md, INDEX.md, TESTING.md, docs/ja/ を整備
2. Doxyfile を全ライブラリに配置
3. 名前空間の移行エイリアスを削除

---

## 5. 後方互換性戦略

### 5.1 インクルードパスの互換性

移行中、旧インクルードパスは**ラッパーヘッダ**で維持する:

```cpp
// lib/umi/core/include/umi/core/audio_context.hh (ラッパー)
#pragma once
#pragma message("Deprecated: use <umicore/audio_context.hh> instead")
#include <umicore/audio_context.hh>
```

**スケジュール:**
- Phase M1-M4: ラッパー設置、新旧両方のパスが機能する
- Phase M5: ラッパーに deprecation warning を追加
- Phase M6: ラッパーを削除

### 5.2 xmake ターゲット名の互換性

旧ターゲット名は **wrapper target** で維持する。全エイリアスの一覧:

```lua
-- lib/umi/xmake.lua (移行期間中)
-- L1: Foundation
target("umi.core")
    set_kind("headeronly")
    add_deps("umicore")
    add_includedirs("$(projectdir)/lib/umicore/include", { public = true })
target_end()

-- L3: Domain
target("umi.dsp")
    set_kind("headeronly")
    add_deps("umidsp")
    add_includedirs("$(projectdir)/lib/umidsp/include", { public = true })
target_end()

target("umi.synth")  -- synth は dsp に吸収
    set_kind("headeronly")
    add_deps("umidsp")
target_end()

target("umi.midi")
    set_kind("headeronly")
    add_deps("umidi")
    add_includedirs("$(projectdir)/lib/umidi/include", { public = true })
target_end()

target("umi.usb")
    set_kind("headeronly")
    add_deps("umiusb")
    add_includedirs("$(projectdir)/lib/umiusb/include", { public = true })
target_end()

-- L4: System (umi.port は既にスタンドアロン、エイリアス不要)
target("umi.kernel")
    set_kind("headeronly")
    add_deps("umios")
    add_includedirs("$(projectdir)/lib/umios/include", { public = true })
target_end()

target("umi.runtime")
    set_kind("headeronly")
    add_deps("umios")
target_end()

target("umi.service")
    set_kind("headeronly")
    add_deps("umios")
target_end()

target("umi.boot")
    set_kind("headeronly")
    add_deps("umios")
target_end()
```

---

## 6. examples/ の参照パス移行ガイド

### stm32f4_kernel (全ハードコードパス)

```lua
-- 現在:
add_files("$(projectdir)/lib/umi/port/mcu/stm32f4/syscalls.cc")
add_files("$(projectdir)/lib/umi/service/loader/loader.cc")
add_files("$(projectdir)/lib/umi/port/common/common/irq.cc")
add_files("$(projectdir)/lib/umi/crypto/sha512.cc")
add_files("$(projectdir)/lib/umi/crypto/ed25519.cc")
add_includedirs("$(projectdir)/lib/umi/kernel")
add_includedirs("$(projectdir)/lib/umi/mmio")
add_includedirs("$(projectdir)/lib/umi/port/device")
add_includedirs("$(projectdir)/lib/umi/service/loader")
add_includedirs("$(projectdir)/lib/umi/usb/include")

-- Phase M2 後:
add_files("$(projectdir)/lib/umiport/src/stm32f4/syscalls.cc")
add_files("$(projectdir)/lib/umiport/src/common/irq.cc")
add_deps("umiport")  -- port 関連の includedirs は自動解決

-- Phase M3c 後:
add_deps("umiusb")   -- usb includedirs は自動解決

-- Phase M4 後:
add_files("$(projectdir)/lib/umios/src/service/loader.cc")
add_files("$(projectdir)/lib/umios/src/crypto/sha512.cc")
add_files("$(projectdir)/lib/umios/src/crypto/ed25519.cc")
add_deps("umios")    -- kernel/service includedirs は自動解決
-- mmio の includedirs は既に umimmio への deps で解決済
```

### headless_webhost (WASM)

```lua
-- 現在:
add_includedirs(path.join(project_root, "lib/umi/synth/include"))
add_includedirs(path.join(project_root, "lib/umi/port/platform/wasm"))

-- 移行後:
add_deps("umidsp")   -- synth は dsp に吸収
add_deps("umiport")  -- platform/wasm は umiport に統合
```

---

## 7. kernel → service 依存逆転の解消

### 現行の問題箇所 (3か所)

1. `kernel/shell_commands.hh`: `#include <umi/service/shell/shell_commands.hh>`
   → **解消:** shell_commands のリエクスポートを削除。shell_commands は service/shell/ に属し、adapter 層で kernel と結合する

2. `kernel/syscall_handler.hh`: `#include "storage_service.hh"`, `#include "loader.hh"`
   → **解消:** SyscallHandler は既にテンプレートパラメータ `StorageType` を受け取る設計。この方向性を拡張し、LoaderType もテンプレートパラメータとして注入する:
   ```cpp
   template <typename HW, typename StorageType, typename LoaderType>
   struct SyscallHandler {
       // kernel/ 内に定義。service/ への #include は不要
   };
   ```

3. `kernel/mpu_config.hh`: `#include "loader.hh"`
   → **解消:** AppHeader の型定義 (app_header.hh) は kernel/ 内で自己完結。loader.hh への依存は AppLoader の概念型 (Concept) を kernel/ に定義し、実装は service/ に配置:
   ```cpp
   // kernel/concepts.hh
   template <typename L>
   concept AppLoaderLike = requires(L loader, const AppHeader& header) {
       { loader.validate(header) } -> std::same_as<bool>;
   };
   ```

---

## 8. リスク管理

| リスク | 影響 | 緩和策 |
|--------|------|--------|
| **R1: 旧パス参照の残存** | ビルド失敗 | 各 Phase 前に `git grep 'lib/umi/core'` で全参照をスキャン |
| **R2: 組込みビルドの破損** | リリース遅延 | Phase M2 は慎重に。STM32F4 ビルド + フラッシュで動作確認 |
| **R3: xmake エイリアスの不完全性** | 外部プロジェクトの破損 | wrapper target で includedirs を明示的に設定 |
| **R4: テスト依存の暗黙的結合** | テスト不能 | L0 ライブラリのテスト依存は tests/xmake.lua 内に限定 |
| **R5: 名前空間移行の破壊的変更** | API 互換性崩壊 | namespace エイリアスを 2 リリース期間維持 |
| **R6: CONSOLIDATION_PLAN との競合** | 作業の重複 | ドキュメント整理 (Phase 1-6) を先行、ライブラリ移行 (M0-M6) は後続 |

---

## 9. バージョン戦略

### 9.1 セマンティックバージョニング

移行は破壊的変更を含むため、バージョンを v0.3.0 系に上げる。v0.x.y の間は API 安定性保証の対象外。

| タイミング | バージョン / タグ |
|-----------|------------------|
| 各フェーズ開始前 | `pre-m0`, `pre-m1`, ..., `pre-m6` タグ |
| M0-M2 完了 | v0.3.0-alpha |
| M3-M4 完了 | v0.3.0-beta |
| M5 完了 | v0.3.0-rc1 |
| M6 完了 | v0.3.0 |

### 9.2 エイリアスの寿命

**名前空間エイリアス:**

| エイリアス | 導入 | 削除予定 |
|-----------|------|---------|
| `namespace rt = umi::rt` | M1 | v0.4.0 |
| `namespace umitest = umi::test` | M1 | v0.4.0 |
| `namespace umidi = umi::midi` | M3b | v0.4.0 |

**xmake ターゲットエイリアス:**

| エイリアス | 導入 | 削除予定 |
|-----------|------|---------|
| `umi.core` → `umicore` | M1 | v0.4.0 |
| `umi.dsp` → `umidsp` | M3a | v0.4.0 |
| `umi.synth` → `umidsp` | M3a | v0.4.0 |
| `umi.midi` → `umidi` | M3b | v0.4.0 |
| `umi.usb` → `umiusb` | M3c | v0.4.0 |
| `umi.kernel` → `umios` | M4 | v0.4.0 |
| `umi.runtime` → `umios` | M4 | v0.4.0 |
| `umi.service` → `umios` | M4 | v0.4.0 |
| `umi.boot` → `umios` | M4 | v0.4.0 |

---

## 10. 関連文書との整合性

| 文書 | 関係 | 実行順序 |
|------|------|---------|
| [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | ドキュメント整理 → 先行実施 | Phase 0 ～ Phase 1 |
| [PROPOSAL.md](PROPOSAL.md) | Phase 0-2 → CONSOLIDATION_PLAN 実行 + 設計統合 | Phase 0 ～ Phase 2 |
| **MIGRATION_PLAN.md (本文書)** | ライブラリ構成改編 → CONSOLIDATION_PLAN 完了後 | Phase M0 ～ Phase M6 |
| [LIBRARY_SPEC.md](LIBRARY_SPEC.md) | 理想構成の定義 → 本文書の移行ゴール | 参照 |
| [ANALYSIS.md](ANALYSIS.md) | 3つの「飛躍点」 → Phase M2 (umiport統合) で一部実現 | 参照のみ |

**実行順序の推奨:**

```
CONSOLIDATION_PLAN Phase 1-6 (ドキュメント整理)
    ↓ (完了後)
MIGRATION_PLAN Phase M0 (依存関係修正)
    ↓
Phase M1 (umicore 新設)
    ↓
Phase M2 (umiport 統合)  ← 最もリスク高い。慎重に
    ↓
Phase M3 (umidsp, umidi, umiusb 昇格)
    ↓
Phase M4 (umios 統合)    ← 最大規模。M4a/M4b に分割
    ↓
Phase M5 (クリーンアップ)
    ↓
Phase M6 (品質統一)
    ↓
PROPOSAL Phase 2 (MCU DB-PAL-BSP パイプライン)
```

---

## 用語集

| 用語 | 定義 |
|------|------|
| **昇格** | モジュールをスタンドアロンライブラリに変換する操作 |
| **統合** | 複数のモジュールを1つのライブラリにまとめる操作 |
| **吸収** | 小規模モジュールを別のライブラリの内部サブモジュールにする操作 |
| **エイリアス** | 後方互換性のための旧名称ラッパー (xmake target / namespace) |
