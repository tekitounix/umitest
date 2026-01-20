# UMI-OS リファクタリング計画

**作成日**: 2025年1月12日  
**最終更新**: 2026年1月12日  
**基準ドキュメント**: [DESIGN_COMPARISON_REPORT.md](DESIGN_COMPARISON_REPORT.md)

---

## 1. 概要

NEW_DESIGN.md を目標仕様として、既存設計の成熟した部分を維持しながら段階的にリファクタリングを実施する。

### 1.1 基本方針

| 方針 | 内容 |
|------|------|
| 新設計採用 | **コンセプトベースProcessor API**、時間管理、メモリ管理、保護レベル、ログ・アサート |
| 既存維持 | コルーチン(umi_coro.hh)、UI設計(ADAPTER.md)、設計決定記録 |
| アーカイブ | 現在の実装を完全に保存してから変更開始 |
| 進行方式 | 依存関係順に実行、各フェーズ完了条件で区切る |

### 1.2 設計決定

| 決定 | 理由 |
|------|------|
| C++20コンセプト | vtable不要、インライン化可能、-fno-rtti対応 |
| ダックタイピング | 継承不要、柔軟なAPI |
| 型消去ラッパー | 動的ディスパッチが必要な時のみ使用 |

---

## 2. アーカイブ構成

```
archive/
└── v0.1-legacy/                    # 現在の実装スナップショット
    ├── core/                       # 全ファイルコピー
    │   ├── umi_app_types.hh
    │   ├── umi_audio.hh
    │   ├── umi_coro.hh
    │   ├── umi_expected.hh
    │   ├── umi_kernel.hh
    │   ├── umi_midi.hh
    │   ├── umi_monitor.hh
    │   ├── umi_shell.hh
    │   └── umi_startup.hh
    ├── port/                       # 全ファイルコピー
    ├── examples/
    ├── test/
    ├── doc/
    │   ├── DESIGN_DETAIL.md       # 旧詳細設計
    │   └── README.md              # 旧README
    ├── renode/
    └── xmake.lua
```

**アーカイブしないファイル（維持・発展）**:
- `core/umi_coro.hh` — コルーチン実装（成熟）
- `doc/ADAPTER.md` — UI設計（詳細）
- `doc/COMPARISON_MOS_STM32.md` — 設計決定記録
- `core/umi_monitor.hh` — モニタリング機能
- `core/umi_shell.hh` — デバッグシェル

---

## 3. 新ディレクトリ構造

```
umi_os/
├── xmake.lua                       # 更新済
├── include/umi/                    # 公開ヘッダー
│   ├── types.hh                   # ✅ 型定義
│   ├── time.hh                    # ✅ 時間ユーティリティ
│   ├── event.hh                   # ✅ Event/EventQueue
│   ├── audio_context.hh           # ✅ AudioContext/ControlContext
│   ├── processor.hh               # ✅ コンセプトベースAPI
│   ├── coro.hh                    # ✅ core/umi_coro.hhから移動
│   ├── assert.hh                  # ✅ 層別アサート
│   ├── log.hh                     # ✅ 層別ログ
│   ├── triple_buffer.hh           # ✅ ロックフリー・トリプルバッファ
│   └── error.hh                   # ✅ Result<T>/Errorコード
├── core/                          # カーネル・内部実装（レガシー）
│   ├── umi_coro.hh               # コルーチン（成熟）
│   ├── umi_kernel.hh             # カーネル
│   ├── umi_audio.hh              # オーディオ
│   ├── umi_midi.hh               # MIDI
│   └── ...
├── dsp/                           # DSP部品（依存なし、予定）
├── adapter/                       # アダプタ層（予定）
│   └── embedded/
├── port/                          # PAL（既存構造維持）
├── doc/
│   ├── DESIGN_COMPARISON_REPORT.md # 比較レポート
│   ├── MIGRATION.md               # 移行ガイド
│   └── REFACTORING_PLAN.md        # 本ドキュメント
├── examples/
│   └── example_app.cc            # ✅ 新API対応済
├── test/
│   ├── test_processor.cc         # ✅ 新API、16テスト
│   ├── renode_test.cc            # ✅ レガシーAPI、33テスト
│   ├── test_kernel.cc            # 既存
│   ├── test_audio.cc             # 既存
│   └── test_midi.cc              # 既存
├── renode/
└── archive/
    └── v0.1-legacy/              # ✅ 旧実装アーカイブ
```

---

## 4. フェーズ計画

### Phase 0: 準備 ✅ 完了

**依存**: なし  
**完了条件**: アーカイブ完了、新ディレクトリ構造準備完了

| タスク | 状態 | 詳細 |
|--------|------|------|
| アーカイブ作成 | ✅ | `archive/v0.1-legacy/` に全ファイルコピー |
| Gitタグ作成 | ✅ | `v0.1-legacy` タグ作成済 |
| MIGRATION.md作成 | ✅ | 旧→新APIマッピング表 |
| ディレクトリ作成 | ✅ | `include/umi/` 作成済 |

### Phase 1: Processor API改訂 ✅ 完了

**依存**: Phase 0  
**完了条件**: 新Processor APIで最小限のシンセサイザーがネイティブテストで動作

| タスク | 状態 | 詳細 |
|--------|------|------|
| types.hpp | ✅ | sample_t、定数定義 |
| time.hpp | ✅ | ms_to_samples()、samples_to_ms()、bpm計算 |
| event.hpp | ✅ | EventType、Event、EventQueue（SPSC） |
| audio_context.hpp | ✅ | AudioContext、ControlContext、StreamConfig |
| processor.hpp | ✅ | **コンセプトベース設計**（継承不要） |
| ネイティブテスト | ✅ | test_processor.cc: 16テストパス |
| example更新 | ✅ | example_app.cc を新API対応 |

**設計変更**:
- 当初の6段階ライフサイクル → コンセプトベース設計に変更
- `ProcessorLike`: `process(AudioContext&)`があればOK
- `Controllable`: `control(ControlContext&)`も持つ
- `AnyProcessor`: 動的ディスパッチ用型消去ラッパー

### Phase 2: コア分割・整理 ✅ 完了

**依存**: Phase 1  
**完了条件**: トリプルバッファリング動作、既存テスト全パス、Renodeシミュレーション動作

| タスク | 状態 | 詳細 |
|--------|------|------|
| coro.hh移動 | ✅ | umi_coro.hh → include/umi/coro.hh（API変更なし） |
| assert.hh | ✅ | 層別アサート（UMI_ASSERT, UMI_REQUIRE等） |
| log.hh | ✅ | 層別ログ（LogLevel, UMI_LOG_*マクロ） |
| triple_buffer.hh | ✅ | ロックフリー・トリプルバッファリング |
| error.hh | ✅ | core/umi_expected.hhからコピー、Error列挙型 |
| 拡張子変更 | ✅ | 全ヘッダーを.hpp→.hhに変更 |
| Renodeテスト | ✅ | 33/33テストパス（レガシーAPI使用） |

**注意**: `umi::Event`名前空間の衝突により、renode_test.ccでは新APIを使用できず。
- umi_kernel.hh: `umi::Event`はnamespace（ビットフラグ）
- event.hh: `umi::Event`はstruct（オーディオイベント）
- **解決済み**: `umi::Event` → `umi::KernelEvent`に変更

### Phase 3: アダプタ層実装 ✅ 完了

**依存**: Phase 2  
**完了条件**: ネイティブアダプタ動作、Renodeで音声出力確認

| タスク | 状態 | 詳細 |
|--------|------|------|
| 名前空間衝突解決 | ✅ | umi::Event → umi::KernelEvent |
| adapter/embedded/ | ✅ | Adapter<Proc,Hw,Config>テンプレート |
| dsp/分離 | ✅ | oscillator, filter, envelope, 依存なし |
| Renodeテスト | ✅ | 64/64テストパス |

**注**: 
- VST3/CLAP/WASMプラグインビルドは後回し
- DSPテストはネイティブ環境推奨（Renodeではfloat演算が遅い）
- UI Serverは未実装

### Phase 4: 拡張機能

**依存**: Phase 3  
**完了条件**: ISPで診断情報取得可能、Renodeで検証

| タスク | 詳細 |
|--------|------|
| ISP | SysEx経由の診断・統計取得 |
| 保護レベル | L1/L2/L3の明確化 |
| 外部同期 | WordClock/MIDI Clock |

---

## 5. テスト戦略

### 5.1 テスト環境

| 環境 | 用途 | ツール |
|------|------|--------|
| **ネイティブ** | 単体テスト、ロジック検証 | xmake test (host) |
| **Renode** | RTOS動作、割り込み、タイミング | Robot Framework |
| **実ハードウェア** | 最終検証（後回し） | PyOCD + STM32F4 |

### 5.2 xmake構成

arm-embedded-xmake-repo を使用し、gcc-arm/clang-arm両対応:

```lua
-- リポジトリ追加
add_repositories("arm-embedded-xmake-repo https://github.com/tekitounix/arm-embedded-xmake-repo.git main")

-- パッケージ
add_requires("arm-embedded")
add_requires("coding-rules")

-- 組み込みターゲット
target("firmware")
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.toolchain", "clang-arm")  -- or "gcc-arm"
target_end()
```
| adapter/vst3/ | VST3アダプタ（ADAPTER.md §4.2準拠） |
| dsp/分離 | 依存なしDSP部品 |
| UI Server | Input Server / Display Server実装 |

### Phase 4: 拡張機能

**依存**: Phase 3  
**完了条件**: CLAP/WASM動作、ISPで診断情報取得可能

| タスク | 詳細 |
|--------|------|
| ISP | SysEx経由の診断・統計取得 |
| 保護レベル | L1/L2/L3の明確化 |
| adapter/clap/ | CLAPアダプタ |
| adapter/wasm/ | WebAssemblyアダプタ |
| 外部同期 | WordClock/MIDI Clock |

---

## 5. ファイル対応表

### 維持・リファクタするファイル

| 現在 | 変更後 | 変更内容 |
|------|--------|----------|
| `core/umi_coro.hh` | `include/umi/coro.hpp` | 移動のみ（API維持） |
| `core/umi_kernel.hh` | `core/kernel/*.hpp` | 分割 |
| `core/umi_audio.hh` | `core/audio/*.hpp` | 分割 |
| `core/umi_midi.hh` | `core/midi/*.hpp` | 分割 |
| `core/umi_monitor.hh` | `core/monitor/` | 維持 |
| `core/umi_shell.hh` | `core/monitor/` | 維持 |
| `core/umi_expected.hh` | `include/umi/error.hpp` | 拡張 |
| `port/*` | `port/*` | 維持・拡張 |
| `doc/ADAPTER.md` | `doc/ADAPTER.md` | 正式仕様として維持 |
| `doc/COMPARISON_MOS_STM32.md` | `doc/COMPARISON_MOS_STM32.md` | 維持 |

### 新規作成ファイル

| ファイル | 内容 |
|----------|------|
| `include/umi/processor.hpp` | Processor API（6段階ライフサイクル） |
| `include/umi/audio_context.hpp` | AudioContext、EventQueue |
| `include/umi/time.hpp` | 時間ユーティリティ |
| `include/umi/assert.hpp` | 層別アサート |
| `include/umi/log.hpp` | 層別ログ |
| `core/audio/triple_buffer.hpp` | トリプルバッファリング |
| `adapter/embedded/adapter.hpp` | 組み込み用アダプタ |
| `adapter/vst3/*.hpp` | VST3アダプタ |
| `dsp/*.hpp` | 依存なしDSP部品 |
| `doc/MIGRATION.md` | 移行ガイド |

---

## 6. リスクと対策

| リスク | 影響度 | 対策 |
|--------|--------|------|
| 既存API破壊 | 高 | MIGRATION.mdで旧→新マッピング明記、deprecated警告経由 |
| トリプルバッファでレイテンシ増加 | 中 | バッファサイズ見直し、Renodeでレイテンシ計測 |
| コルーチン互換性 | 低 | API変更せずリネームのみ |
| 工数膨張 | 高 | 各フェーズ完了時に動作プロトタイプ維持、タグ作成 |
| ドキュメント整合性 | 中 | NEW_DESIGN.mdにADAPTER.md参照を追記 |

---

## 7. 完了条件チェックリスト

### Phase 0完了時
- [ ] archive/v0.1-legacy/ にすべてのファイルがコピーされている
- [ ] `git tag v0.1-legacy` が作成されている
- [ ] include/umi/ ディレクトリが存在する

### Phase 1完了時
- [ ] 新Processor APIでexample_app.ccがビルド・動作する
- [ ] AudioContextを通じてオーディオ処理が可能
- [ ] sample_positionベースの時間管理が機能する

### Phase 2完了時
- [ ] トリプルバッファリングが動作する
- [ ] 既存テストがすべてパスする
- [x] 層別ログ・アサートが機能する

### Phase 3完了時
- [x] 名前空間衝突解決（umi::Event → umi::KernelEvent）
- [x] adapter/embedded/ 実装
- [x] dsp/ 分離（oscillator, filter, envelope）
- [x] 64/64 Renodeテストパス
- [ ] VST3プラグインとしてビルド可能（後回し）
- [ ] Input/Display Serverが機能する（未実装）

### Phase WASM完了時
- [x] xmake.luaにEmscriptenビルド設定
- [x] adapter/wasm/（test_wasm.cc, synth_wasm.cc）
- [x] web/（umi.js, index.html, test.html, test-headless.mjs）
- [x] Node.jsヘッドレステスト（4/4パス）
- [x] WASMモジュール生成成功（umi_test.wasm, umi_synth.wasm）

### Phase 4完了時
- [ ] CLAPプラグインとしてビルド可能
- [ ] ISPで診断情報が取得できる
- [ ] 外部同期が機能する

---

## 8. 次のアクション

**次のフェーズ候補**:
1. ブラウザでの実際の音声出力テスト（xmake wasm-serve）
2. Playwright/Puppeteerでの自動化テスト
3. UI Server（Input Server / Display Server）の設計
4. ISP（In-System Programming）診断機能
5. プラグインアダプタ（VST3/CLAP）の実装（優先度低）

---

*本計画は段階的に更新されます。*

**更新履歴**:
- 2025-01-12: 初版作成（Phase 0〜4計画）
- 2025-01-12: Phase 0完了、Phase 1完了（コンセプトベース設計）
- 2025-01-12: Phase 2完了（コア整理、.hh拡張子統一、Renode 33テストパス）
- 2025-01-12: Phase 3完了（アダプタ層、DSP分離、64テストパス）
- 2026-01-12: Phase WASM完了（Emscripten統合、Node.jsテスト4/4パス）
