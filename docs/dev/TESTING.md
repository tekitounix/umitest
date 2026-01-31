# UMI Test Strategy

## 概要

UMIのテスト戦略は3つの環境をカバーします:

| 環境 | 目的 | ツール |
|------|------|--------|
| **Host** | ユニットテスト、高速イテレーション | xmake test |
| **WASM** | ブラウザ/Node.js での動作確認 | Node.js, Emscripten |
| **ARM** | 実機相当のエミュレーション | Renode, Robot Framework |

---

## 1. ユニットテスト (`tests/`)

### 現在のテスト

| ファイル | 対象 | テスト数 |
|---------|------|----------|
| `test_dsp.cc` | Oscillator, Filter, Envelope, Utility | 79 |
| `test_kernel.cc` | Task, Timer, Notification, SpscQueue | 101 |
| `test_audio.cc` | AudioContext, Lifecycle, DSP Load | 多数 |
| `test_midi.cc` | MIDI Message, EventQueue, EventReader | 152 |
| `renode_test.cc` | ARM実機エミュレーション（UART出力） | - |

**合計: 330+ テスト**

### テスト実行

```bash
# 全テスト実行
xmake test

# 個別実行
xmake run test_dsp
xmake run test_kernel
xmake run test_audio
xmake run test_midi
```

### テストフレームワーク

共通の軽量フレームワーク `test/test_common.hh` を使用（外部依存なし、組み込み向け）:

```cpp
#include "test_common.hh"
using umi::test::check;

SECTION("My Feature");
check(result == expected, "description");
CHECK_EQ(a, b, "values match");
CHECK_NEAR(actual, expected, "float comparison");

TEST_SUMMARY();  // 結果サマリー出力
```

### Kernel テストカバレッジ

| 機能 | テスト状況 |
|------|-----------|
| Task create/delete/suspend/resume | ✓ |
| Priority scheduling (Realtime > Server > User > Idle) | ✓ |
| User priority round-robin | ✓ |
| Core affinity | ✓ |
| Timer queue (ticked mode) | ✓ |
| Timer IRQ (tickless) | ✓ |
| Notification (notify/wait/wait_block) | ✓ |
| SpscQueue (SPSC lock-free queue) | ✓ |
| LoadMonitor / Stopwatch | ✓ |
| MPU configuration | ✓ |
| Shared memory | ✓ |
| for_each_task iteration | ✓ |

---

## 2. WASM テスト

### ヘッドレステスト (`test/test-headless.mjs`)

Node.js でUMIMモジュールをテスト:

```bash
# ビルド + テスト
xmake f -p wasm -a wasm32 --toolchain=emcc
xmake build umim_synth umim_delay umim_volume
node test/test-headless.mjs
```

### テスト内容 (22 tests)

- UMIMモジュールのロード確認 (synth, delay, volume)
- パラメータ introspection API
- パラメータ set/get roundtrip
- オーディオ処理 (umi_process)
- Note on/off (シンセのみ)
- モジュール間パッチング (synth → delay → volume チェイン)

---

## 3. ARM エミュレーション (Renode)

### セットアップ

```bash
# ビルド
xmake build renode_test

# 対話的実行
xmake renode

# 自動テスト
xmake renode-test

# Robot Framework テスト
xmake robot
```

### テスト項目

| 項目 | 内容 |
|------|------|
| 起動シーケンス | ベクタテーブル → main |
| Kernel動作 | タスク切り替え、タイマー |
| UART出力 | テスト結果のログ出力 |
| リアルタイム制約 | デッドライン検証 |

---

## 4. CI/CD パイプライン

### GitHub Actions (`.github/workflows/ci.yml`)

```
push/PR
   ↓
┌─────────────────────────────────────────────┐
│  GitHub Actions                             │
│  ├── host-tests (ubuntu, macos)             │
│  │   ├── xmake build (全テストターゲット)    │
│  │   ├── xmake test (330+ tests)            │
│  │   └── カバレッジ計測 (ubuntu, lcov)       │
│  ├── wasm-tests                              │
│  │   ├── Emscripten ビルド                  │
│  │   └── Node.js テスト (22 tests)          │
│  ├── arm-build                               │
│  │   └── クロスコンパイル確認               │
│  └── renode-tests (optional, disabled)       │
│       └── Robot Framework                   │
└─────────────────────────────────────────────┘
```

### 実行タイミング

- **push**: master, main, develop ブランチへのプッシュ時
- **PR**: 上記ブランチへのPR作成/更新時

---

## 5. テスト設計方針

### 仕様ベース vs 実装ベース

| アプローチ | 用途 | 例 |
|-----------|------|-----|
| **仕様ベース** | API契約のテスト | SpscQueue の FIFO順序保証 |
| **実装ベース** | 内部ロジック検証 | FPU save/restore の呼び出し回数 |

基本は仕様ベースでAPI契約を検証し、クリティカルな内部動作は実装ベースで補完します。

### 組み込み制約

全テストは組み込み制約下でビルド:

```
-fno-exceptions -fno-rtti
```

### カバレッジ目標

| モジュール | 状態 |
|-----------|------|
| `lib/core/` (AudioContext, Event, etc.) | ✓ 網羅 |
| `lib/core/umi_kernel.hh` (RTOS) | ✓ 網羅 |
| `lib/dsp/` | ✓ 網羅 |
| `lib/adapter/` | UMIM経由でテスト |

---

## 6. ローカル開発フロー

```bash
# 1. 開発中の高速テスト
xmake build test_dsp && xmake run test_dsp

# 2. 全テスト
xmake test

# 3. WASMビルド確認
xmake f -p wasm && xmake build umim_synth

# 4. ARM ビルド確認
xmake build firmware
```

---

## 参考

- [ARCHITECTURE.md](ARCHITECTURE.md) - 全体アーキテクチャ
- [API.md](API.md) - APIリファレンス
