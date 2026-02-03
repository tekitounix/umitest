# UMI ライブラリ構成リファクタリング計画

本文書は `lib/` 以下のライブラリ構成を理想的な統合構造へ移行する計画を定義する。

**更新履歴**:
- 2026-02-03: 理想形（`lib/umi/` 統合構造）を採用。全ライブラリを `umi::` namespace 下に統合
- 2026-02-03: System ServiceをRuntime層に配置、`umi::ref`を採用、coroを独立レイヤーに配置

---

## 背景と目的

### 現状の問題

1. **命名の不統一**: `umidsp::Osc` + `#include <umidsp/osc.hh>` — 「umi」プレフィックスが断片的
2. **namespace とパスの不一致**: `umios::core::AudioContext` だがパスは `umios/core/`
3. **アーキテクチャの不可視**: 3層モデル（Application/Runtime/Backend）がディレクトリ構造に反映されていない
4. **拡張時の迷い**: 新機能を `umixxx/` として独立させるか `umios/xxx/` に入れるか判断困難

### 目標

- **完全な命名統一**: `umi::xxx::Name` + `#include <umi/xxx/name.hh>`
- **namespace = path = 責務**: 3重の一貫性を達成
- **アーキテクチャの視覚化**: 3層モデルがディレクトリ構造に直接表現される
- **業界標準との整合**: Abseil, Folly, Zephyr と同じパターン

---

## 理想形: `lib/umi/` 統合構造

### 最終的なディレクトリ構成

```
lib/
└── umi/
    ├── core/               # umi::core — Application境界型（最下層）
    │   ├── audio_context.hh
    │   ├── processor.hh
    │   ├── event.hh
    │   ├── types.hh
    │   ├── error.hh
    │   ├── shared_state.hh
    │   ├── time.hh
    │   ├── syscall_nr.hh   # OS/App共有定義
    │   └── ui/             # UIフレームワーク（将来使用）
    │       ├── ui_controller.hh
    │       ├── ui_map.hh
    │       └── ui_view.hh
    │
    ├── runtime/            # umi::runtime — イベントルーティング（Runtime層）
    │   ├── event_router.hh
    │   ├── param_mapping.hh
    │   └── route_table.hh
    │
    ├── service/            # umi::service — System Service（トップレベル独立カテゴリ）
    │   ├── loader/         # アプリローダー（.umia検証・ロード）
    │   │   ├── loader.hh
    │   │   ├── loader.cc
    │   │   └── app_header.hh
    │   ├── shell/          # SysEx対話シェル
    │   │   ├── shell.hh
    │   │   └── shell_commands.hh
    │   ├── audio/          # オーディオサービス
    │   │   └── audio.hh
    │   ├── midi/           # MIDIサービス
    │   │   └── midi.hh
    │   └── storage/        # ストレージサービス
    │       └── storage.hh
    │
    ├── kernel/             # umi::kernel — RTOSカーネル（Backend層、組み込み専用）
    │   ├── scheduler.hh
    │   ├── syscall_handler.hh
    │   ├── mpu.hh
    │   ├── protection.hh
    │   ├── fault_handler.hh
    │   ├── fpu_policy.hh
    │   └── metrics.hh
    │
    ├── app/                # umi::app — アプリSDK（Application層）
    │   ├── syscall.hh
    │   ├── umi_app.hh
    │   ├── crt0.cc
    │   ├── app.ld
    │   └── app_sections.ld
    │
    ├── adapter/            # umi::adapter — バックエンドアダプタ
    │   ├── embedded.hh
    │   ├── embedded_adapter.hh
    │   ├── umim.hh         # WASMアダプタ
    │   └── web/
    │       ├── web_adapter.hh
    │       ├── web_sim.js
    │       ├── web_sim_worklet.js
    │       ├── umi.js
    │       ├── umi-worklet.js
    │       └── worklet-processor.js
    │
    ├── coro/               # umi::coro — コルーチン（ターゲット非依存、Controllerで使用）
    │   ├── coroutine.hh
    │   ├── task.hh
    │   ├── scheduler.hh
    │   └── awaitable.hh
    │
    ├── dsp/                # umi::dsp — DSP処理
    │   ├── oscillator/
    │   │   ├── sine.hh
    │   │   ├── saw.hh
    │   │   └── square.hh
    │   ├── filter/
    │   │   ├── biquad.hh
    │   │   └── svf.hh
    │   └── envelope/
    │       └── adsr.hh
    │
    ├── midi/               # umi::midi — MIDI処理
    │   ├── ump.hh
    │   ├── message.hh
    │   └── sysex.hh
    │
    ├── usb/                # umi::usb — USBスタック
    │   └── audio_class.hh
    │
    ├── port/               # umi::port — HAL/BSP
    │   ├── hal/
    │   └── platform/
    │       ├── stm32f4/
    │       ├── stm32h7/
    │       └── wasm/
    │
    ├── crypto/             # umi::crypto — 暗号
    │   ├── ed25519.hh
    │   ├── ed25519.cc
    │   ├── sha256.hh
    │   ├── sha256.cc
    │   ├── sha512.hh
    │   ├── sha512.cc
    │   └── public_key.hh
    │
    ├── synth/              # umi::synth — シンセ実装
    │   └── voice.hh
    │
    ├── fs/                 # umi::fs — ファイルシステム
    │   ├── fat/            # FatFs実装
    │   │   ├── ff.hh
    │   │   ├── ff_config.hh
    │   │   ├── ff_diskio.hh
    │   │   ├── ff_types.hh
    │   │   └── ff_unicode.hh
    │   └── slim/           # SLIM実装（独自軽量FS）
    │       ├── slim.hh
    │       ├── slim_config.hh
    │       └── slim_types.hh
    │
    ├── ui/                 # umi::ui — UI状態管理（旧 umiui）
    │   └── hid.hh
    │
    ├── gfx/                # umi::gfx — グラフィック描画（旧 umigui）
    │   ├── canvas.hh
    │   └── framebuffer.hh
    │
    ├── boot/               # umi::boot — ブートローダー（旧 umiboot）
    │   └── dfu.hh
    │
    ├── mmio/               # umi::mmio — MMIO抽象化（旧 umimmio）
    │   └── register.hh
    │
    ├── util/               # umi::util — 共通ユーティリティ
    │   ├── ring_buffer.hh
    │   ├── triple_buffer.hh
    │   ├── log.hh
    │   └── assert.hh
    │
    ├── shell/              # umi::shell — シェルプリミティブ（ホスト側ツール）
    │   └── primitives.hh
    │
    ├── test/               # umi::test — テストフレームワーク（旧 umitest）
    │   └── test_common.hh
    │
    └── ref/                # umi::ref — ライブラリ構造規約のリファレンス（旧 umimock）
        └── reference.hh
```

---

## namespace とパスの対応

| 機能 | 旧namespace | 旧パス | 新namespace | 新パス |
|------|------------|--------|-------------|--------|
| AudioContext | `umios::core` | `<umios/core/audio_context.hh>` | `umi::core` | `<umi/core/audio_context.hh>` |
| EventRouter | `umios::core` | `<umios/core/event_router.hh>` | `umi::runtime` | `<umi/runtime/event_router.hh>` |
| Shell Service | `umios::kernel` | `<umios/kernel/umi_shell.hh>` | `umi::service::shell` | `<umi/service/shell/shell.hh>` |
| Scheduler | `umios::kernel` | `<umios/kernel/umi_kernel.hh>` | `umi::kernel` | `<umi/kernel/scheduler.hh>` |
| Task | `umios::kernel` | `<umios/kernel/coro.hh>` | `umi::coro` | `<umi/coro/task.hh>` |
| Sine | `umidsp` | `<umidsp/osc.hh>` | `umi::dsp` | `<umi/dsp/oscillator/sine.hh>` |
| UMP | `umidi` | `<umidi/ump.hh>` | `umi::midi` | `<umi/midi/ump.hh>` |
| Gpio | `umiport` | `<umiport/gpio.hh>` | `umi::port` | `<umi/port/gpio.hh>` |
| Ed25519 | `umios::crypto` | `<umios/crypto/ed25519.hh>` | `umi::crypto` | `<umi/crypto/ed25519.hh>` |
| UIController | `umios::core::ui` | `<umios/core/ui/ui_controller.hh>` | `umi::core::ui` | `<umi/core/ui/ui_controller.hh>` |
| FatFs | `umifs::fat` | `<umifs/fat/ff.hh>` | `umi::fs::fat` | `<umi/fs/fat/ff.hh>` |
| SLIM | `umifs::slim` | `<umifs/slim/slim.hh>` | `umi::fs::slim` | `<umi/fs/slim/slim.hh>` |

---

## アーキテクチャの視覚化

```
lib/umi/
├── core/           ← Application層（型定義、依存なし）
├── app/            ← Application層（coreに依存）
├── coro/           ← 協調的マルチタスク（coreに依存）
│
├── runtime/        ← Runtime層（coreに依存）
│
├── service/        ← System Service（トップレベル独立カテゴリ）
│   ├── shell/      # SysEx対話シェル
│   ├── audio/      # オーディオサービス
│   ├── midi/       # MIDIサービス
│   └── storage/    # ストレージサービス
│
├── kernel/         ← Backend層（runtimeに依存、組み込み専用）
├── adapter/        ← Backend Adapter（kernelと対話）
│
├── port/           ← HAL層（アーキテクチャ外、低レベル）
├── mmio/           ← レジスタ抽象（最下位）
│
├── dsp/            ← ドメイン: オーディオ処理
├── midi/           ← ドメイン: MIDIプロトコル
├── synth/          ← ドメイン: シンセ実装（dspに依存）
├── usb/            ← ドメイン: USBスタック
├── fs/             ← ドメイン: ファイルシステム（fat, slim）
├── crypto/         ← ドメイン: 暗号（独立）
├── ui/             ← ドメイン: UI入力
├── gfx/            ← ドメイン: UI描画
├── boot/           ← ドメイン: ブートローダー
├── shell/          ← ドメイン: ホスト側シェルツール
├── util/           ← ドメイン: 共通ユーティリティ
├── test/           ← ドメイン: テストフレームワーク
└── ref/            ← ドメイン: リファレンス実装
```

**3層モデル（Application/Runtime/Backend）+ ドメイン層 + インフラ層**

---

## 依存グラフ（理想形）

```
                    ┌─────────┐
                    │ umi::mmio│ ← 最下位（レジスタ抽象）
                    └────┬────┘
                         │
                         ▼
                    ┌─────────┐
                    │umi::port│ ← HAL（mmioに依存）
                    └────┬────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
        ▼                ▼                ▼
   ┌─────────┐      ┌─────────┐      ┌─────────┐
   │umi::core│      │umi::util│      │umi::kernel
   │(Application)   │(共通)     │      │(Backend)
   └────┬────┘      └─────────┘      └────┬────┘
        │                                  │
   ┌────┴────┐                             │
   │         │                             │
   ▼         ▼                             │
┌──────┐ ┌──────┐ ┌─────────────┐         │
│umi:: │ │umi:: │ │umi::runtime │◀─────────┘
│app   │ │coro  │ │(Runtime)    │
└──────┘ └──────┘ └─────────────┘

              ┌─────────────────────┐
              │ umi::service        │  ← System Service（トップレベル独立）
              │ - shell             │
              │ - audio             │
              │ - midi              │
              │ - storage           │
              └─────────────────────┘
                         │
                         ▼
                    ┌──────────┐
                    │umi::adapter
                    └──────────┘

ドメイン層（独立またはcoreに依存）:
  umi::crypto, umi::fs, umi::mmio,
  umi::ui, umi::gfx, umi::boot, umi::shell, umi::test, umi::ref,
  umi::service

ドメイン層（core + 他ドメインに依存）:
  umi::dsp ← なし
  umi::midi（プロトコル）← なし
  umi::synth ← umi::dsp
  umi::usb ← umi::dsp
  
System Service（独立カテゴリ）:
  umi::service::shell, umi::service::audio, umi::service::midi, umi::service::storage
```

---

## 移行計画

### Phase 1: 準備（1日）

1. **重複・未使用コード削除**（STRUCTURE_ANALYSIS.md 参照）
   - `lib/umios/core/app.hh`（`app/umi_app.hh` と重複）
   - `lib/umios/kernel/syscall/syscall_numbers.hh`（`core/syscall_nr.hh` と重複）
   - `lib/umios/kernel/modules/`（`umiusb` と重複・未使用）
   - `lib/umios/backend/cm/`（空）
   - `lib/umios/platform/`（READMEのみ）

2. **移行順序の決定**（依存の少ない順）
   - Step 1: `umi/mmio/`, `umi/util/`, `umi/crypto/`（依存なし）
   - Step 2: `umi/core/`（基本型）
   - Step 3: `umi/coro/`（coreに依存）
   - Step 4: `umi/runtime/`（coreに依存）
   - Step 5: `umi/service/` 移動（System Service、トップレベル独立カテゴリ）
   - Step 6: `umi/port/`（mmioに依存）
   - Step 7: `umi/dsp/`, `umi/midi/`, `umi/fs/`（coreまたは独立）
   - Step 8: `umi/kernel/`（runtime, portに依存）
   - Step 9: `umi/adapter/`（core, kernelに依存）
   - Step 10: `umi/app/`（coreに依存）
   - Step 11: `umi/synth/`, `umi/usb/`（dspに依存）
   - Step 12: `umi/ui/`, `umi/gfx/`, `umi/boot/`, `umi/shell/`, `umi/test/`, `umi/ref/`

### Phase 2: 統合移行（3-4日）

各ステップで以下を実行：
1. 新ディレクトリ `lib/umi/xxx/` 作成
2. ファイル移動・namespace変更
3. xmake.lua 更新
4. ビルド・テスト検証

| ステップ | 内容 | 工数 | 検証コマンド |
|---------|------|------|-------------|
| 2.1 | `umi/mmio/`, `umi/util/`, `umi/crypto/` 移動 | 4h | `xmake test` |
| 2.2 | `umi/core/` 移動 | 4h | `xmake build test_kernel test_audio` |
| 2.3 | `umi/coro/` 移動 | 2h | `xmake build synth_app` |
| 2.4 | `umi/runtime/` 移動 | 3h | `xmake build stm32f4_kernel` |
| 2.5 | `umi/service/` 移動（System Service、トップレベル独立） | 3h | `xmake build stm32f4_kernel` |
| 2.6 | `umi/port/` 移動 | 3h | `xmake build stm32f4_kernel` |
| 2.7 | `umi/dsp/`, `umi/midi/`, `umi/fs/` 移動 | 4h | `xmake test` |
| 2.8 | `umi/kernel/` 移動 | 6h | `xmake build stm32f4_kernel` + flash |
| 2.9 | `umi/adapter/` 移動 | 4h | `xmake build headless_webhost` |
| 2.10 | `umi/app/` 移動 | 2h | `xmake build synth_app` |
| 2.11 | `umi/synth/`, `umi/usb/` 移動 | 2h | `xmake build synth_app` |
| 2.12 | その他（ui, gfx, boot, shell, test, ref）移動 | 4h | `xmake test` |
| 2.13 | 旧ディレクトリ削除・最終検証 | 4h | 全ターゲットビルド |

**並列作業可能**: Step 2.1, 2.2, 2.3 は独立して実行可能

### Phase 3: ドキュメント更新（1日）

- CLAUDE.md — 全パス参照を更新
- README.md — ディレクトリ構造を更新
- 各ライブラリREADME.md — インクルードパスを更新
- docs/umios-architecture/ — 必要に応じて更新

### Phase 4: 互換性ヘッダ削除（将来）

移行期間中は互換性ヘッダを提供：

```cpp
// lib/umidsp/osc.hh（互換性用、移行期間中のみ）
#pragma once
#warning "umidsp/osc.hh is deprecated, use umi/dsp/oscillator/sine.hh"
#include <umi/dsp/oscillator/sine.hh>
```

**互換性期間**: 3ヶ月または次のマイルストーンリリースまで

---

## 検証基準

### Phase 1完了時
- [ ] 重複コード削除完了
- [ ] 全ターゲットビルド成功

### Phase 2完了時（各ステップで）
- [ ] ビルド成功: `xmake build <target>`
- [ ] テスト通過: `xmake test`
- [ ] 循環依存なし: 依存グラフがDAG

### Phase 3完了時
- [ ] ドキュメント更新完了
- [ ] CLAUDE.md のパス参照が新構造に対応

### Phase 4完了時
- [ ] 互換性ヘッダ削除
- [ ] 全コードベースが新パスのみを使用

---

## 利点（理想形達成時）

1. **命名の一貫性**: `umi::dsp::Sine` + `#include <umi/dsp/oscillator/sine.hh>` — 3重の一致
2. **アーキテクチャの視覚化**: 3層モデルがディレクトリ構造に反映
   - Application層: `core/`, `app/`, `coro/`
   - Runtime層: `runtime/`（System Service含む）
   - Backend層: `kernel/`, `adapter/`
3. **業界標準との整合**: Abseil, Folly, Zephyr と同じパターン
4. **拡張時の明確性**: 新機能は常に `lib/umi/xxx/` として追加
5. **シンプルなメンタルモデル**: 「すべては `umi::` の下にある」

---

## 参考資料

- [lib/umios/docs/STRUCTURE_ANALYSIS.md](../umios/docs/STRUCTURE_ANALYSIS.md) — 現状のコードベース分析
- [lib/docs/LIBRARY_STRUCTURE.md](LIBRARY_STRUCTURE.md) — ライブラリ構造規約
- [docs/umios-architecture/00-overview.md](../../docs/umios-architecture/00-overview.md) — 3層モデル定義
