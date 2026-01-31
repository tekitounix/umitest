# UMI-OS イベント・入力アーキテクチャ整理

## 目的

* Audio のリアルタイム性を絶対に守る
* Control の柔軟性とアプリ依存性を許容する
* 同一ハードウェアでアプリごとに異なる入力の扱い方を可能にする
* MIDI 1.0 / 2.0 を共通構造で扱う

---

## 基本原則

* Audio は遅延不可、ジッター不可、待ち不可
* Control は数 ms〜数十 ms の遅延を許容
* 音に効く変更は必ず System を経由して確定
* データ表現と意味付けを分離する

---

## 全体構成

```
[ Driver / ISR ]
        |
        v
[ System Input ]  取得・正規化・時刻付け
        |
        v
[ Dispatcher / ClockDomain ]
   |        |        |
   v        v        v
AudioQueue ControlQueue Stream(Shared)
```

---

## データ経路の種類

### 1. AudioQueue

* 欠落不可
* サンプル精度
* Processor 専用
* 固定長イベント

用途

* NoteOn / NoteOff
* Pitch / Aftertouch
* ParamSet
* Clock 同期イベント

---

### 2. ControlQueue

* 欠落可
* UI・状態遷移向け

用途

* ボタン操作
* ノブ通知
* Program Change
* Learn / Mode 切替

---

### 3. Stream（共有メモリ）

* 単一 writer（System）
* 複数 reader（Processor / Controller）
* reader ごとに cursor

用途

* MIDI ルーター
* MIDI Learn
* ログ
* 高密度イベント観測

必要な場合のみ有効化

---

## 値とイベントの分離

### イベント

* 一度性
* 順序重要
* 欠落が意味を壊すもの

### 値

* 最新値のみ重要
* 上書き前提
* versioned shared state

---

## ParamSet / ParamSetRequest

### ParamSetRequest

* Control が生成
* 意味付け済みだが時刻未確定

含む情報

* param_id
* value（論理値）
* source
* option（即時 / 補間希望など）

### ParamSet

* System が生成
* Audio に効く最終命令
* sample_offset 確定済み

Audio は ParamSet のみを見る

---

## MIDI メッセージの責務分離

### Audio

* NoteOn / NoteOff
* Pitch Bend
* Aftertouch
* ParamSet

### Control

* CC
* NRPN / RPN
* Program Change
* Learn 対象全般

### System

* SysEx / SysEx8
* MIDI CI
* Transport / Clock
* ジッター補正と時刻写像

---

## MIDI 2.0 とジッター補正

* UMP 受信は System
* タイムスタンプを Audio フレームに変換
* sample_offset を確定
* Audio に確定イベントのみ渡す

Audio 側で時刻推定はしない

---

## ボタン・ノブ・CV 入力

### ボタン

* System で debounce
* イベント化
* 状態は共有メモリ

### ノブ / CV

同一入力を二表現で提供

1. ControlState / ControlEvent

* UI 操作
* Learn

2. AudioInputStream

* 一定レート連続値
* Audio レート変調

---

## アプリ依存を吸収する仕組み

入力チャンネルごとに購読モードを指定

* control_only
* audio_stream
* both

オプション

* 要求サンプルレート
* レイテンシ
* 平滑化

切替はブロック境界で反映

---

## 競合回避の原則

* キュー要素はコピー
* 状態の書き込み権限は一箇所
* Audio に効く命令は System のみ
* Control は Request のみ発行

---

## 結論

* Audio は締め切り世界
* Control は意味世界
* System は時間と仲裁の世界

この三層分離により

* 実行時効率
* 拡張性
* MIDI 1.0 / 2.0 対応
* アプリ依存入力処理
  を同時に満たす
