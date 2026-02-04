# XMake ガイド

UMI プロジェクトにおける xmake の基本的な使い方と全コマンドリファレンスです。

## 基本フロー

```bash
# 1) 設定
xmake f

# 2) ビルド
xmake build

# 3) 実行（対話型）
xmake run <target>

# 4) テスト
xmake test

# 5) クリーン
xmake clean

# 6) 情報表示
xmake show
```

## コード品質（xmake標準機能）

### フォーマット

```bash
xmake format                    # 全ターゲットをフォーマット
xmake format -n                 # dry-run（変更なし確認のみ）
xmake format -e                 # エラーとして報告（CI向け）
xmake format -t <target>        # 特定ターゲットのみ
xmake format -g test            # testグループのみ
xmake format -f "src/*.cc"      # ファイル指定
xmake format --create -s LLVM   # .clang-format生成
```

### 静的解析チェック

```bash
xmake check                     # デフォルトチェッカー
xmake check clang.tidy          # clang-tidy によるチェック
xmake check --list              # 利用可能なチェッカー一覧
xmake check --info=clang.tidy   # チェッカー詳細情報
```

### デバッグビルド

```bash
# デバッグモードでビルド
xmake f -m debug && xmake

# リリースモードに戻す
xmake f -m release && xmake
```

## 組み込み開発（arm-embedded パッケージ）

### Flash

```bash
# ターゲットを書き込み
xmake flash -t <target>
xmake flash -t stm32f4_kernel
xmake flash -t synth_app -a 0x08060000

# ツール状態/接続プローブ確認（非対話型）
xmake flash.status
xmake flash.probes
```

### デバッグ

```bash
# GDBデバッガー起動（対話型）
xmake debugger -t <target>

# GDBサーバー清理（非対話型）
xmake debugger.cleanup
```

### エミュレータ

```bash
# ヘルプ表示（非対話型）
xmake emulator

# Renode対話セッション（対話型）
xmake emulator.run

# 自動テスト（対話型）
xmake emulator.test
```

### デプロイ

```bash
# ビルド成果物をコピー（非対話型）
xmake deploy -t <target> [--dest <dir>]

# WASMホストデプロイ（非対話型）
xmake deploy.webhost

# デプロイしてサーバー起動（対話型 - 常駐）
xmake deploy.serve
```

### HTTPサーバー

```bash
# HTTPサーバー起動（対話型 - 常駐）
xmake serve

# RTTログビューアー（対話型 - 常駐）
xmake serve.rtt
```

## テスト

```bash
# 全ホストテスト実行
xmake test

# 特定グループのみ
xmake test -g tests/umidi
xmake test -g "tests/*"

# FS統合チェック
xmake fs-check
```

## プロジェクト生成

```bash
# compile_commands.json（Clangd用）
xmake project -k compile_commands

# CMakeLists.txt
xmake project -k cmake

# Visual Studioプロジェクト
xmake project -k vsxmake
```

---

## 全コマンドリファレンス

### xmake標準アクション

| コマンド | 説明 | タイプ |
|----------|------|--------|
| `xmake build` / `xmake b` | ターゲットをビルド | 非対話型 |
| `xmake clean` / `xmake c` | バイナリと一時ファイルを削除 | 非対話型 |
| `xmake config` / `xmake f` | プロジェクトを設定 | 非対話型 |
| `xmake create` | 新規プロジェクトを作成 | 非対話型 |
| `xmake global` / `xmake g` | xmakeのグローバルオプション設定 | 非対話型 |
| `xmake install` / `xmake i` | ターゲットバイナリをパッケージ化・インストール | 非対話型 |
| `xmake package` / `xmake p` | ターゲットをパッケージ化 | 非対話型 |
| `xmake require` / `xmake q` | 必要なパッケージをインストール・更新 | 非対話型 |
| `xmake run` / `xmake r` | プロジェクトターゲットを実行 | **対話型** |
| `xmake uninstall` / `xmake u` | プロジェクトバイナリをアンインストール | 非対話型 |
| `xmake update` | xmakeプログラムを更新・削除 | 非対話型 |

### xmake標準プラグイン

| コマンド | 説明 | タイプ |
|----------|------|--------|
| `xmake check` | プロジェクトソースコードと設定をチェック | 非対話型 |
| `xmake doxygen` | Doxygenドキュメントを生成 | 非対話型 |
| `xmake format` | 現在のプロジェクトをフォーマット | 非対話型 |
| `xmake lua` / `xmake l` | Luaスクリプトを実行 | 非対話型 |
| `xmake macro` / `xmake m` | 指定マクロを実行 | 非対話型 |
| `xmake pack` | バイナリインストールパッケージを作成 | 非対話型 |
| `xmake plugin` | xmakeプラグインを管理 | 非対話型 |
| `xmake project` | プロジェクトファイルを生成 | 非対話型 |
| `xmake repo` | パッケージリポジトリを管理 | 非対話型 |
| `xmake show` | プロジェクト情報を表示 | 非対話型 |
| `xmake watch` | プロジェクトディレクトリを監視してコマンド実行 | **対話型** |

### arm-embedded パッケージ: Flash プラグイン

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake flash` | ARM組み込みターゲントを書き込み | **対話型**※ | `-t <target>`, `-d <device>`, `-b <backend>`, `-a <address>`, `--dry-run` |
| `xmake flash.probes` | 接続されているデバッグプローブを一覧表示 | 非対話型 | - |
| `xmake flash.status` | フラッシュツールの状態を表示 | 非対話型 | - |

※`--dry-run`付与時は非対話型

### arm-embedded パッケージ: Debugger プラグイン

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake debugger` | GDBデバッガー起動 | **対話型** | `-t <target>`, `-b <backend>`, `-p <port>`, `--server-only`, `--vscode` |
| `xmake debugger.cleanup` | 孤立したGDBサーバープロセスを全て終了 | 非対話型 | - |

### arm-embedded パッケージ: Emulator プラグイン

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake emulator` | エミュレータのヘルプと状態を表示 | 非対話型 | - |
| `xmake emulator.run` | Renodeエミュレータを起動 | **対話型** | `-t <target>`, `-s <file>`, `--headless`, `--gdb` |
| `xmake emulator.test` | Renode Robot Frameworkテストを実行 | **対話型** | `-r <file>`, `-o <dir>` |

### arm-embedded パッケージ: Deploy プラグイン

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake deploy` | ビルド成果物を指定ディレクトリにコピー | 非対話型 | `-t <target>`, `-d <dir>` |
| `xmake deploy.webhost` | WASMヘッドレスホストをデプロイ | 非対話型 | - |
| `xmake deploy.serve` | デプロイしてローカルサーバーを起動 | **対話型** | - |

### arm-embedded パッケージ: Serve プラグイン

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake serve` | Web/WASMコンテンツ用HTTPサーバーを起動 | **対話型** | `-p <port>`, `-d <dir>`, `--open`, `--build` |
| `xmake serve.rtt` | RTTログビューアーWebインターフェースを起動 | **対話型** | `-p <port>`, `--rtt-port` |

### プロジェクト定義タスク

| コマンド | 説明 | タイプ |
|----------|------|--------|
| `xmake test` | プロジェクトテストを実行 | 非対話型 |
| `xmake fs-check` | FSテスト、Renodeベンチマーク、ARMサイズ比較 | **対話型**※ |

※`fs-check`はRenodeテストを含むため対話型

### 非推奨タスク

| コマンド | 代替方法 |
|----------|----------|
| `xmake flash-h7-app` | `xmake flash -t daisy_pod_synth_h7 -a 0x90000000` |
| `xmake flash-h7-kernel` | `xmake flash -t daisy_pod_kernel` |
| `xmake flash-kernel` | `xmake flash -t stm32f4_kernel` |
| `xmake flash-synth-app` | `xmake flash -t synth_app -a 0x08060000` |
| `xmake flash-synth-h7` | `xmake flash -t daisy_pod_synth_h7 -a 0x90000000` |

---

## 調査結果サマリー

### コマンド分類一覧

| 分類 | 数 | コマンド例 |
|------|-----|-----------|
| **非対話型（バッチ実行可能）** | 30+ | build, clean, test, check, format, pack, project, flash.probes, flash.status, debugger.cleanup |
| **対話型（ユーザー入力/常駐）** | 9 | run, debugger, emulator.run, emulator.test, serve, serve.rtt, deploy.serve, watch |
| **無効/未対応** | 0 | - |

### 非対話型コマンド実行確認結果

#### ✅ 実行成功（17コマンド）

| コマンド | 結果 | 備考 |
|----------|------|------|
| `xmake test` | ✅ | 13テスト完了 |
| `xmake build` | ✅ | 成功 |
| `xmake build -g firmware` | ✅ | 成功 |
| `xmake build -g tests/*` | ✅ | success |
| `xmake build -g wasm` | ✅ | 成功 |
| `xmake clean` | ✅ | 完了 |
| `xmake show` | ✅ | 完了 |
| `xmake deploy.webhost` | ✅ | 成功 |
| `xmake check` | ✅ | 2 warnings (headerfiles not found) |
| `xmake format -n` | ✅ | format ok |
| `xmake debugger.cleanup` | ✅ | 0 orphaned processes |
| `xmake project -k compile_commands` | ✅ | compile_commands.json生成 |
| `xmake project -k cmake` | ✅ | CMakeLists.txt生成 |
| `xmake flash.probes` | ✅ | STLINK-V3検出済み |
| `xmake flash.status` | ✅ | PyOCD/OpenOCD検出済み |
| `xmake pack` | ✅ | pack ok |

#### ⚠️ ツールエラー（1コマンド）

| コマンド | 結果 | 備考 |
|----------|------|------|
| `xmake check clang.tidy` | ⚠️ | clang-arm 21.1.1 の multilib.yaml に `IncludeDirs` キーが含まれているが、clang-tidy が認識できない（Arm Toolchain のバグ） |

**解決策**: `~/.xmake/packages/c/clang-arm/21.1.1/*/lib/clang-runtimes/multilib.yaml` を削除またはリネームすると clang-tidy が正常に動作します

#### ❌ 対話型のためスキップ（9コマンド）

| コマンド | 理由 |
|----------|------|
| `xmake run <target>` | プログラムが対話型になる可能性あり |
| `xmake debugger` | GDB対話セッション |
| `xmake emulator.run` | Renode GUI/コンソール待機 |
| `xmake emulator.test` | Robot Framework + Renode対話 |
| `xmake deploy.serve` | Python HTTPサーバー常駐 |
| `xmake serve` | HTTPサーバー常駐 |
| `xmake serve.rtt` | WebSocket + HTTPサーバー常駐 |
| `xmake fs-check` | Renodeテストを含む |
| `xmake watch` | ファイル監視常駐 |

### 実行統計

```
総コマンド数: 40+
├─ 非対話型実行成功: 16
├─ ツールエラー: 1
├─ 対話型スキップ: 9
└─ 無効: 1
```

---

*最終更新: 2026-02-05*
*対象リポジトリ: `.refs/arm-embedded-xmake-repo` (packages/a/arm-embedded/plugins/)*
