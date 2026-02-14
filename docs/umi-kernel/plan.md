# UMI Kernel ドキュメント再構成計画

この文書は、UMI Kernel の仕様書を「カーネル仕様／システムサービス仕様／アプリケーション規格仕様」を中心に再構成するための詳細計画です。**実装は行わず、ドキュメントの構成と移行手順のみ**を定義します。

---

## 1. 目的とスコープ

### 目的
- 仕様書としての**正本**を明確化する
- 仕様・設計判断・実装詳細・計画を分離し、更新負担を減らす
- 読者（カーネル開発者／移植者／アプリ開発者）が必要な情報に最短で到達できる状態にする

### スコープ
- 対象は docs/umi-kernel 配下の既存文書および新設文書
- コード変更・ビルド手順・実装は対象外

---

## 2. 最終構成（仕様中心・詳細）

### 2.1 正本（規範）

#### docs/umi-kernel/spec/kernel.md
1. 目的・スコープ
  - カーネル仕様の範囲と前提
2. 実行モデル
  - 特権/非特権、OS/アプリ分離、実行契約
3. タスクモデル
  - タスク種別、優先度、状態遷移、FPUポリシー
4. スケジューリング
  - O(1)ビットマップ、選択アルゴリズム、文脈切替
5. 例外/割り込み
  - SysTick/SVC/PendSVの責務と順序
6. IPC/イベント
  - notify/wait、イベントフラグの意味
7. 共有メモリ（要約）
  - 参照の導線のみ（詳細は memory-protection / applicationへ）
8. 互換性・ABI方針
  - ABIバージョン、互換性ルール

#### docs/umi-kernel/spec/system-services.md
1. 目的・スコープ
  - OSが提供するサービスの規範
2. Syscall体系
  - 番号体系、API一覧、引数/戻り値、エラー規約
3. SysEx/シェル/stdio
  - SysEx経由の入出力とコマンド設計
4. USB MIDI/Audio
  - 入出力の契約、ストリーミング状態
5. ファイルシステム（将来）
  - 仕様方針、非同期設計、イベント通知
6. 監視/ログ
  - メトリクス、ログ出力、診断の仕様

#### docs/umi-kernel/spec/application.md
1. 目的・スコープ
  - アプリ規格仕様の範囲
2. .umia形式
  - ヘッダ構造、検証、署名、ロード契約
3. エントリポイント契約
  - main/process、戻り値、終了時の動作
4. Processor/AudioContext契約
  - process()のリアルタイム制約、イベント入出力
5. アプリから見たIPC
  - syscallの使用範囲、共有メモリの利用
6. ABI/互換性
  - 互換性方針とバージョン運用

#### docs/umi-kernel/spec/memory-protection.md
1. 目的・スコープ
  - メモリ保護・Fault方針
2. メモリレイアウト
  - APP_RAM、共有メモリ、カーネル領域
3. MPU境界
  - Region構成、権限、実行可否
4. ヒープ/スタック衝突検出
  - 検出条件と動作
5. Faultログと隔離
  - 記録場所、復旧方針

### 2.2 実装仕様（プラットフォーム）

#### docs/umi-kernel/platform/stm32f4.md
1. 目的・スコープ
  - STM32F4固有の実装仕様
2. 起動シーケンス
  - Reset→main、初期化フェーズ
3. 例外/IRQ優先度
  - IRQの優先度設計と依存関係
4. DMA/Audio/MIDIフロー
  - DMAバッファ、USB Audio/MIDIの流れ
5. 既知制約
  - サンプルレートや周辺制約

### 2.3 非規範（補助）

#### docs/umi-kernel/adr.md
1. 目的・スコープ
2. ADR一覧
  - Syscall番号体系、SharedMemory優先方針など

#### docs/umi-kernel/plan.md
1. 本計画文書

---

## 3. 規範レベルの統一

各正本文書の冒頭に以下を明示する:
- **規範レベル**: MUST/SHALL/REQUIRED、SHOULD/RECOMMENDED、MAY/NOTE/EXAMPLE
- **対象読者**: Kernel Dev / Porting / App Dev
- **適用範囲**: どのターゲットに適用する仕様か

---

## 4. 既存文書の移行方針

### 4.1 移行元（→ archive 移動済み）
- ~~docs/umi-kernel/OVERVIEW.md~~ → docs/archive/umi-kernel/
- ~~docs/umi-kernel/ARCHITECTURE.md~~ → docs/archive/umi-kernel/
- ~~docs/umi-kernel/BOOT_SEQUENCE.md~~ → docs/archive/umi-kernel/
- ~~docs/umi-kernel/DESIGN_DECISIONS.md~~ → docs/archive/umi-kernel/
- ~~docs/umi-kernel/IMPLEMENTATION_PLAN.md~~ → docs/archive/umi-kernel/
- ~~docs/umi-kernel/MEMORY.md~~ → docs/archive/umi-kernel/
- ~~docs/umi-kernel/LIBRARY_CONTENTS.md~~ → 削除済み（PROJECT_STRUCTURE.md に包含）

### 4.2 移行先ルール
- 仕様の正本は spec/ 配下に集約（重複は排除）
- STM32F4固有の内容は platform/stm32f4.md に集約
- 設計理由は adr.md へ移す（仕様文書には書かない）
- 計画は plan.md のみで管理
- 参考資料（棚卸し）は必要に応じて plan 末尾へ簡潔に残す

---

## 5. セクション移動の指針

- **カーネル仕様**: タスク構成、優先度、スケジューラ、割り込み、IPC
- **システムサービス仕様**: Syscall一覧、Shell/SysEx、USB MIDI/Audio、FS設計
- **アプリ規格**: .umiaヘッダ、ロード契約、Processor/AudioContextの契約
- **メモリ/保護**: MPU境界、APP_RAMレイアウト、Faultログ/隔離
- **STM32F4実装**: 起動シーケンス、IRQ優先度、DMAフロー

重複は**正本に一箇所のみ**残し、他は参照リンクとする。

---

## 6. 省く/統合しない内容（明記）

以下は「カーネル仕様／システムサービス仕様／アプリ規格仕様」の正本からは**省く**、または**別ドキュメントへ誘導**する。

1. Web UI/ブラウザ側の構成
  - 例: Webコンポーネント構成、UI部品、Web MIDI APIの詳細
  - 理由: カーネル仕様と直接関係しないため
2. WASM/UMIMなど非カーネル実装の詳細
  - 理由: プラットフォーム実装仕様の範囲外
3. ビルド/デプロイ手順の詳細
  - 例: xmakeコマンド列、Webホスト手順
  - 理由: 仕様書ではなく運用/手順書の範囲
4. サンプルコード/使用例の長文掲載
  - 理由: 仕様の正本を簡潔に維持
5. 詳細なライブラリ棚卸し
  - 例: 参照/未参照の列挙
  - 理由: 設計・仕様の正本とは性質が異なる
6. 実装計画の詳細工程
  - 理由: 仕様書ではなく計画文書に限定

---

## 7. 作業ステップ（詳細）

### Step 1: 仕様骨子の確定
- 上記「最終構成」を確定
- 各正本文書の目次（粒度は 2〜3階層）を作成

### Step 2: 既存内容の割付表作成
- 既存文書の章・節ごとに移動先をマッピング
- 重複箇所は「正本へ統合、元は参照リンク」にする

### Step 3: 新規ファイル作成
- spec/ と platform/ を作成し、空の仕様テンプレートを置く
- 冒頭に規範レベル・対象読者・適用範囲を明記

### Step 4: 移植と整理
- 既存文書から内容を移植
- 用語・表記を統一（例: Task名/優先度表記/用語の大小文字）

### Step 5: 旧文書の簡略化
- 旧文書は「移動先リンクの索引」に縮約
- 不整合リンクの修正（存在しない文書の参照を除去/置換）

### Step 5.5: 旧文書のアーカイブ
- 作業完了後、旧文書は /Users/tekitou/work/umi/docs/archive に移動
- アーカイブ後は新仕様へのリンクのみを残す

### Step 6: 最終レビュー
- 仕様の正本が一箇所にあるか
- 読者が迷わない導線か
- STM32F4固有内容が混入していないか

---

## 8. 成果物と完了条件

### 成果物
- spec/ 配下の4本の正本仕様
- platform/stm32f4.md の実装仕様
- adr.md / plan.md の補助文書
- 旧文書の索引化
- 旧文書のアーカイブ移動

### 完了条件
- 同じ仕様が複数文書に重複しない
- “仕様の正本”が明確である
- STM32F4固有と汎用仕様が混ざっていない
- OVERVIEW/ARCHITECTURE の役割が「索引」に限定されている

---

## 9. 次アクション

- 既存文書の「章・節レベルの割付表」を作成する
- spec/ と platform/ のテンプレートを作成する
