# UMI-OS Test Strategy

## 概要

UMI-OSのテスト戦略は3つの環境をカバーします:

| 環境 | 目的 | ツール |
|------|------|--------|
| **Host** | ユニットテスト、高速イテレーション | xmake test |
| **WASM** | ブラウザ/Node.js での動作確認 | Node.js, Emscripten |
| **ARM** | 実機相当のエミュレーション | Renode, Robot Framework |

---

## 1. ユニットテスト (`test/`)

### 現在のテスト

| ファイル | 対象 | 状態 |
|---------|------|------|
| `test_dsp.cc` | Oscillator, Filter, Envelope, Utility | ✓ 53 tests |
| `renode_test.cc` | ARM実機エミュレーション（UART出力） | ✓ |

### テスト実行

```bash
# 全テスト実行
xmake test

# 個別実行
xmake run test_dsp
```

### テストフレームワーク

独自の軽量フレームワークを使用（外部依存なし、組み込み向け）:

```cpp
void check(bool cond, const char* msg) {
    if (!cond) {
        std::printf("FAIL: %s\n", msg);
        std::exit(1);
    }
}
```

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
│  │   ├── xmake build test_dsp               │
│  │   ├── xmake test                          │
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

## 5. テスト方針

### 組み込み制約

全テストは組み込み制約下でビルド:

```
-fno-exceptions -fno-rtti
```

### カバレッジ目標

| モジュール | 目標 |
|-----------|------|
| `lib/core/` | 主要API全て |
| `lib/dsp/` | 全DSPコンポーネント |
| `lib/adapter/` | UMIM エクスポート関数 |

### 追加予定

| テスト | 対象 | 優先度 |
|--------|------|--------|
| `test_umim.cc` | UMIMモジュール統合 | 中 |
| `test_patching.cc` | モジュール間接続 | 低 (将来) |
| カバレッジ計測 | llvm-cov / gcov | 低 |

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
