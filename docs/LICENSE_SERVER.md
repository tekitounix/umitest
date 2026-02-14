# ライセンス認証システムサーバー仕様

本書は、インターネット接続機能を一切持たない組み込みデバイスを、PC に USB 接続して利用する前提で、ミニマルかつ堅牢、管理コスト最小、無料枠中心で運用できるライセンス認証基盤を定義する。

対象は電子楽器などのハードウェアデバイスで、PC 側に Web ベースのアップデーターを用意し、基本はキャッシュされ、認証回数は極限まで少ない。

本設計では **台数制限は採用せず、回数制限（アクティベーション回数）を正とする**。

# 要件と非要件

## 要件

* デバイスはネット接続しない
* PC アプリまたは Web アップデーターが USB 経由でデバイスを検出して起動する
* 起動時の照合は完全オフラインで完結する
* デバイスの複製や USB エミュレーションに一定の耐性を持つ
* サーバーは最小データのみ保持する
* サーバーは複数箇所にバックアップされ、復旧が容易
* 登録や発行の入口は攻撃耐性を持つ

## 非要件

* 国家レベルの物理解析耐性
* 常時オンライン必須の失効
* 厳密な同時接続台数制御

# 全体アーキテクチャ

## 概要

* デバイスは秘密鍵保持者
* サーバーはライセンス署名者
* PC は検証者

起動時は PC がサーバーに問い合わせず、以下のみで起動可否を決定する。

1. ライセンス証明書の署名検証
2. デバイスのチャレンジレスポンス署名検証

## 構成

```
[ Device ] --USB--> [ Web Updater (PWA) ] --HTTPS--> [ License Server ]

License Server:
  - Cloudflare Pages
  - Cloudflare Workers
  - Cloudflare D1
  - Cloudflare R2
  - 外部ストレージ
```

# コンポーネント

## デバイス

### 役割

* デバイス固有の秘密鍵を保持
* USB 経由で署名応答し実機性を証明

### 初回起動時鍵生成

* MCU UID を device_id として取得
* Ed25519 鍵ペアをデバイス自身で生成
* 秘密鍵は Flash 保護領域に保存
* 鍵が存在する限り再生成しない

### USB API

* GET_DEVICE_INFO

  * device_id
  * device_pubkey

* SIGN_CHALLENGE

  * 入力 nonce
  * 出力 signature

署名対象は domain separation を含めた nonce とする。

## 出荷ツール

### 役割

* 生産時にデバイスをサーバー登録
* 偽装や取り違えを防止

### フロー

1. GET_DEVICE_INFO
2. nonce 生成
3. SIGN_CHALLENGE
4. 署名検証
5. 合格時のみ登録

## Web Updater (PWA)

### 役割

* ライセンス取得と保存
* オフライン起動時検証

### キャッシュ

* Service Worker による完全キャッシュ
* 通常起動時は通信不要

### 起動時検証

1. ライセンス証明書署名検証
2. activation 証明書署名検証
3. デバイス署名検証

## ライセンスサーバー

### 役割

* デバイス登録
* ライセンス発行
* アクティベーション回数管理

### DB 最小構成

* devices

  * device_id
  * device_pubkey
  * status

* licenses

  * license_id
  * entitlement
  * status

* activations

  * license_id
  * device_id
  * activated_at

## ライセンス証明書

* license_id
* entitlement
* issued_at

サーバー秘密鍵で署名する。

## アクティベーション回数制限

### 方針

* 台数制限は行わない
* サーバーが関与するアクティベーション回数のみ制限する
* 起動回数は制限しない

### entitlement 例

```json
{
  "type": "activation_limited",
  "max_activations": 5
}
```

### アクティベーション証明書

```json
{
  "ver": 1,
  "kind": "activation",
  "license_id": "...",
  "device_id": "...",
  "activated_at": "..."
}
```

### フロー

1. PC がデバイスに接続
2. nonce 生成と署名取得
3. サーバーが回数検証
4. activation 証明書発行

### 起動時

* activation 証明書が存在すれば起動可
* 回数超過後も既存 activation は有効

## デバイス BAN ポリシー

### 方針

秘密鍵漏洩が合理的に確認されたデバイスは BAN する。

* BAN は UID + 公開鍵単位
* 新規発行 再発行不可
* 既存証明書の即時失効は行わない

## バックアップ

* 重要イベントは即時ログ
* 日次スナップショット
* 3 箇所分散保存

## 攻撃耐性

* API レート制限
* 管理画面 Turnstile
* 出荷用 API 分離

# まとめ

* オフライン前提でも破綻しない
* 回数制限は現実的で運用しやすい
* 小規模ハードウェア事業向けの最小強度設計
