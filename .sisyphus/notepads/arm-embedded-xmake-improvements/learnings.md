## 2026-02-04 Task: task-5-flash-unification

### 実施内容

Flashタスクを統合版 `xmake flash -t <target>` に移行。旧タスクは後方互換性のためエイリアスとして残し、非推奨メッセージを表示。

### 変更ファイル

| ファイル | 変更内容 |
|---------|---------|
| `examples/stm32f4_kernel/xmake.lua` | `flash-kernel` タスクを統合版に移行 |
| `examples/synth_app/xmake.lua` | `flash-synth-app` タスクを統合版に移行 |
| `examples/daisy_pod_kernel/xmake.lua` | `flash-h7-kernel`, `flash-h7-app` タスクを統合版に移行 |
| `examples/daisy_pod_synth_h7/xmake.lua` | `flash-synth-h7` タスクを統合版に移行 |

### 移行パターン

```lua
-- 旧タスク（直接pyocd呼び出し）
task("flash-xxx")
    on_run(function ()
        os.execv("pyocd", {...})
    end)
task_end()

-- 新タスク（統合flashに委譲）
task("flash-xxx")
    on_run(function ()
        print("[DEPRECATED] Use: xmake flash -t <target>")
        os.exec("xmake flash -t <target>")
    end)
    set_menu {..., description = "... (deprecated, use 'xmake flash -t <target>')"}
task_end()
```

### 注意点

- Daisy Pod H7のQSPIフラッシュはpyOCDではなくSTM32CubeProgrammerが必要だが、統合版で `-a 0x90000000` を指定することで対応可能
- 後方互換性を維持するため、旧コマンドは引き続き使用可能

## 2026-02-04 Task: task-6-tool-registry-wire

- `plugins/flash/xmake.lua` で `tool_registry.find_pyocd()` を使用するように移行。
- `plugins/debugger/xmake.lua` で `tool_registry.find_gdb()` / `find_debugger()` を使用するように移行。

## 2026-02-04 Task: task-7-emulator-plugin

- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/emulator/xmake.lua` を追加。
- `xmake emulator`/`emulator.run`/`emulator.test`/`emulator.robot` を追加し、既存 `renode`/`renode-test`/`robot` に委譲。

## 2026-02-04 Task: task-7-emulator-install

- `arm-embedded/xmake.lua` の `on_load` に emulator プラグインのインストール処理を追加。

## 2026-02-04 Task: task-8-size-plugin

- `plugins/size/xmake.lua` を追加し、`xmake size` で `xmake fs-check` を呼ぶラッパーを実装。

## 2026-02-04 Task: task-6-tool-registry

### 実施内容

ツール検出の共通化（レジストリ導入）。

### 現状分析

重複しているツール検出:
- `flash/xmake.lua`: pyocd検出 (lines 159-201)
- `debugger/xmake.lua`: gdb/lldb検出 (lines 72, 181-202)

### 設計案

```lua
-- utils/tool_registry.lua
function find_tool(name, options)
    -- キャッシュ機能付きツール検出
    -- 複数パス対応
    -- バージョンチェック
end

function find_pyocd()
    -- パッケージ優先、フォールバックでシステム
end

function find_gdb(toolchain)
    -- gcc-arm → arm-none-eabi-gdb
    -- clang-arm → lldb
end
```

### 実装方針

1. 新規ファイル: `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/utils/tool_registry.lua`
2. 既存プラグインをリファクタリングして共通関数を使用
3. キャッシュ機能でパフォーマンス向上

### ステータス

設計完了、実装待ち。

### 実装完了 (Part 1)

`utils/tool_registry.lua` を作成:

| 関数 | 説明 |
|------|------|
| `find_pyocd()` | xmakeパッケージ優先、フォールバックでシステムPATH。キャッシュ付き |
| `find_gdb(toolchain)` | toolchain別GDB検出。gcc-arm→arm-none-eabi-gdb、clang-arm→lldb/gdb-multiarch |
| `find_lldb()` | システムLLDB検出。キャッシュ付き |
| `find_debugger(toolchain)` | プラットフォーム最適デバッガ選択 |
| `clear_cache()` | キャッシュクリア |

キャッシュ方式: モジュールローカル変数 `_cache` にツール検出結果を保存。セッション中の重複検出を回避。

次ステップ: Part 2でflash/debuggerプラグインをリファクタリングして共通関数を使用。

## 2026-02-04 Task: task-6-tool-registry (Part 2 - Flash Plugin)

### 実施内容

flash プラグインを `tool_registry.find_pyocd()` を使用するようリファクタリング。

### 変更点

| 変更前 | 変更後 |
|--------|--------|
| 45行のインライン PyOCD 検出コード | 12行のtool_registry呼び出し |
| パッケージ/システム検出ロジック重複 | 共通レジストリに委譲 |
| 検出結果のキャッシュなし | レジストリでキャッシュ済み |

### インポート方法

```lua
import("utils.tool_registry", {alias = "tool_registry"})
local pyocd = tool_registry.find_pyocd()
```

### 返り値の変更

`find_pyocd()` は `source` フィールドを追加して返す:
- `{program = "/path/to/pyocd", source = "package"}` - xmakeパッケージから検出
- `{program = "/path/to/pyocd", source = "system"}` - システムPATHから検出

これにより、呼び出し側でソース別のメッセージ表示が可能。

### 次ステップ

Part 3: debugger プラグインも同様にリファクタリング。

## 2026-02-04 Task: task-6-tool-registry (Part 3 - Debugger Plugin)

### 実施内容

debugger プラグインを `tool_registry.find_gdb()` / `tool_registry.find_debugger()` を使用するようリファクタリング。

### 変更点

| 変更前 | 変更後 |
|--------|--------|
| embedded: `find_tool(gdb_cmd)` | `tool_registry.find_gdb(toolchain)` |
| host: 30行のプラットフォーム別デバッガ検出 | `tool_registry.find_debugger()` (8行) |
| `import("lib.detect.find_tool")` | `import("utils.tool_registry", ...)` |

### ポイント

- `find_debugger()` は `{program, type}` を返す。`type` は "gdb" or "lldb"
- cmd_args は debugger.type に基づいて構築 (lldb: `--`, gdb: `-tui`)

## 2026-02-04 Task: task-7-emulator-plugin

### 実施内容

エミュレータプラグインを新規作成。`emulator.*` 形式の統一タスクAPIを提供。

### 作成ファイル

`plugins/emulator/xmake.lua`

### タスク一覧

| タスク | 委譲先 | 説明 |
|--------|--------|------|
| `xmake emulator` | - | ヘルプ表示 |
| `xmake emulator.run` | `xmake renode` | Renode対話モード |
| `xmake emulator.test` | `xmake renode-test` | 自動テスト |
| `xmake emulator.robot` | `xmake robot` | Robot Framework |

### 設計方針

- ロジック再実装を避け、`os.exec()` でプロジェクトのタスクに委譲
- 既存コマンドとの後方互換性を維持（旧コマンドも引き続き利用可能）

## 2026-02-04 Task: task-8-emulator-install

### 実施内容

arm-embedded パッケージの `on_load` にエミュレータプラグインのインストール処理を追加。

### 変更ファイル

`packages/a/arm-embedded/xmake.lua`

### インストールパターン

```lua
local emulator_content = io.readfile(path.join(os.scriptdir(), "plugins", "emulator", "xmake.lua"))
if emulator_content then
    local user_emulator_dir = path.join(global.directory(), "plugins", "emulator")
    -- 差分チェック後、必要な場合のみインストール
    print("=> Emulator task installed to: %s", user_emulator_dir)
end
```

### ポイント

- flash/debug/test/debugger と同一パターン
- `~/.xmake/plugins/emulator/xmake.lua` にインストール
- 差分チェックで不要な上書きを回避
