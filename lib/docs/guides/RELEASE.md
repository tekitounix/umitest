# リリースガイド

UMI ライブラリのバージョン管理・アーカイブ生成・GitHub Release の手順です。

---

## 概要

リリースは `xmake release` タスクで一元管理します。

- バージョンファイル更新（VERSION, xmake.lua, Doxyfile, CHANGELOG）
- tar.gz アーカイブ + SHA256 生成
- git commit + タグ作成

CI（GitHub Actions）ではタグ push をトリガーに自動リリースが実行されます。

## リリース対象の管理

`tools/release_config.lua` でライブラリごとにリリース可否を制御します。

```lua
{
    umitest = {
        publish = true,       -- リリース対象
        headeronly = true
    },
    umiport = {
        publish = false,      -- リリースから除外
        headeronly = false
    }
}
```

| フィールド | 説明 |
|-----------|------|
| `publish` | `true`: デフォルトのリリース対象に含む / `false`: 明示指定しない限り除外 |
| `headeronly` | ヘッダーオンリーライブラリかどうか（将来のパッケージ生成で利用） |

新規ライブラリを追加する場合:
1. `tools/release_config.lua` にエントリを追加
2. `lib/<name>/` ディレクトリが存在することを確認

## コマンドリファレンス

### 基本構文

```bash
xmake release --ver=<MAJOR.MINOR.PATCH> [options]
```

**注意**: `--ver=0.3.0` のように `=` で値を渡してください（`--ver 0.3.0` は動作しません）。

### オプション

| オプション | 短縮 | 説明 |
|-----------|------|------|
| `--ver=X.Y.Z` | — | リリースバージョン（必須、semver形式） |
| `--libs=name1,name2` | `-l` | 対象ライブラリを明示指定（デフォルト: `publish=true` の全ライブラリ） |
| `--dry-run` | `-d` | 変更せずに実行内容を表示 |
| `--no-test` | — | テスト実行をスキップ |
| `--no-tag` | — | git commit/tag をスキップ |
| `--no-archive` | — | アーカイブ生成をスキップ |

### 使用例

```bash
# dry-run で確認（何も変更しない）
xmake release --ver=0.3.0 --dry-run

# 全ライブラリをリリース
xmake release --ver=0.3.0

# 特定ライブラリのみ
xmake release --ver=0.3.0 --libs=umibench,umimmio

# アーカイブ生成のみ（git 操作なし、テストなし）
xmake release --ver=0.3.0 --no-tag --no-test
```

## 処理フロー

`xmake release` は以下の 6 ステップを順番に実行します。

### Step 1: 事前チェック

- `git diff --quiet` で未コミットの変更がないことを確認
- `--no-tag` または `--dry-run` 指定時はスキップ

### Step 2: テスト実行

- `xmake test` で全テストを実行
- `--no-test` 指定時はスキップ

### Step 3: バージョンファイル更新

以下のファイルを指定バージョンに更新:

| ファイル | 更新内容 |
|---------|---------|
| `VERSION` (ルート) | バージョン文字列 |
| `xmake.lua` (ルート) | `set_version("X.Y.Z")` |
| `lib/<name>/VERSION` | バージョン文字列 |
| `lib/<name>/Doxyfile` | `PROJECT_NUMBER` |
| `lib/<name>/CHANGELOG.md` | `## [Unreleased]` の下に `## [X.Y.Z] - YYYY-MM-DD` を挿入 |

### Step 4: アーカイブ生成

各ライブラリについて:

1. `build/packages/<name>-<version>/` にステージング
2. `include/`, `platforms/`, `src/`, `renode/` + メタファイル (VERSION, LICENSE, README.md, CHANGELOG.md) をコピー
3. `<name>-<version>.tar.gz` を生成
4. SHA256 チェックサムを `<name>-<version>.tar.gz.sha256` に出力
5. ステージングディレクトリを削除

### Step 5: git commit + タグ

- `git add -A && git commit -m "release: vX.Y.Z"`
- 統一タグ: `vX.Y.Z`
- 個別タグ: `<name>/vX.Y.Z`（対象ライブラリごと）

### Step 6: サマリー

- 生成されたアーカイブのパス一覧
- SHA256 ハッシュ（xmake-repo パッケージ定義への転記用）
- 次のステップ（`git push origin main --tags`）

## CI 自動リリース

`.github/workflows/release.yml` により、タグ push で自動リリースが実行されます。

### トリガー

```
git tag v0.3.0
git push origin main --tags
```

タグ名 `v*` にマッチすると CI が起動します。

### CI フロー

```
checkout → xmake test → xmake release (archives only) → GitHub Release 作成
```

CI では `--no-test`（テストは別ステップで実行済み）と `--no-tag`（タグは既に存在）を指定してアーカイブ生成のみ行います。

### リリース成果物

GitHub Release に以下がアップロードされます:

```
build/packages/
  umibench-0.3.0.tar.gz
  umibench-0.3.0.tar.gz.sha256
  umimmio-0.3.0.tar.gz
  umimmio-0.3.0.tar.gz.sha256
  ...
```

## リリース手順チェックリスト

### ローカルリリース（手動）

1. `xmake release --ver=X.Y.Z --dry-run` で内容を確認
2. `xmake release --ver=X.Y.Z` を実行
3. `git push origin main --tags`
4. xmake-repo の SHA256 を更新

### CI リリース（推奨）

1. `xmake release --ver=X.Y.Z --dry-run` で内容を確認
2. `xmake release --ver=X.Y.Z` を実行（バージョン更新 + commit + tag）
3. `git push origin main --tags` → CI が自動でアーカイブ生成 + GitHub Release 作成
4. xmake-repo の SHA256 を更新

## xmake-repo パッケージ更新

リリース後、`synthernet-xmake-repo` のパッケージ定義に新バージョンを追加します。

リリースサマリーに表示される SHA256 を使用:

```lua
-- xmake-repo/synthernet/packages/u/<name>/xmake.lua
add_versions("0.3.0", "<sha256>")
```

## トラブルシューティング

### `--ver` が認識されない

`--ver=0.3.0` のように `=` で接続してください。スペース区切り (`--ver 0.3.0`) では動作しません。

### `unknown library` エラー

`tools/release_config.lua` にライブラリが登録されていません。エントリを追加してください。

### `uncommitted changes detected`

`git stash` または `git commit` で作業ツリーをクリーンにしてから再実行してください。

---

*最終更新: 2026-02-07*
