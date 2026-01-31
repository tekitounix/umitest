# アプリケーションメモリ管理と Fault ハンドリング（改善設計）

## 目的

1. **APP_RAMの効率化**（固定スタック廃止）
2. **MPUで境界保護**（APP_RAM外アクセスの検出）
3. **ヒープ/スタック衝突の早期検出**（片方向検出）
4. **Faultの安全な記録と復旧**（カーネル生存優先）

## 現行実装との差分（要点）

- 現行は**固定4KBスタック + 4KBバンプアロケータ**。
- 共有メモリに`MemoryUsage`は**持たない**。
- Faultは**赤LED点灯で停止**。ログ化・復旧は未接続。

## 改善設計（上位方針）

### A. メモリレイアウト

48KBを**32KB + 16KB**の2リージョンで保護する（MPU制約に適合）。

```
0x20018000 = _estack
┌────────────────────────────┐ 16KB (MPU Region: AppStack)
│ Stack（下向き）            │
│                            │
│ Heap（上向き）             │
└────────────────────────────┘ 0x20014000
┌────────────────────────────┐ 32KB (MPU Region: AppData)
│ .data / .bss                │
└────────────────────────────┘ 0x2000C000
```

### B. 衝突検出（必須）

- **ヒープ→スタック衝突は検出**（`operator new`でSPを取得）
- **スタック→ヒープ衝突は検出不可**（運用で高水位監視）

### C. Faultログの信頼性

- **ログはカーネルRAMに保持**（アプリから不可視）
- **SharedMemoryはあくまで表示用途**（アプリ故障時に破壊されうる）

## 具体設計

### 1) `app.ld` / `app_sections.ld` の更新

- `app.ld` は **MEMORY 定義のみ**（デバイス固有）
- `app_sections.ld` に **SECTIONS を集約**（共通化）
- `APP_DATA (32KB)` / `APP_STACK (16KB)` に分割
- `_sheap = ORIGIN(APP_STACK)`
- `_estack = ORIGIN(APP_STACK) + LENGTH(APP_STACK)`

### 2) ヒープ実装

- `crt0.cc`の4KBバンプアロケータを廃止。
- `operator new`で`_sheap`〜`_estack`を使用し、**SP衝突チェック**を実施。

### 3) 監視データの配置

- `MemoryUsage`や`FaultLog`は**Kernel RAMに保持**。
- UI表示に必要な要約のみ、SharedMemoryへコピー（正常時のみ）。

### 4) Fault処理の分離

**ISRでは記録のみ**、**後処理はKernelタスクで行う**。

```
Fault ISR
  └ record_fault()  // 例外レジスタ＋SP/PC/LRを保存

Kernel main loop
  ├ process_pending_fault()
  ├ App terminate
  ├ UI/LED更新
  └ Shell有効化
```

## 重要なリスクと対策

### 1. Fault復旧の難易度

- PSP/MSP判定・例外ネストに失敗すると復旧不能。
- **最初は「記録のみ＋LED通知」から開始**し、復旧は段階導入。

### 2. SharedMemoryの信頼性

- アプリが故障中の場合、SharedMemoryは信用しない。
- **FaultログはKernel専用領域**に保持。

### 3. MPUリージョン数

- 32KB+16KBを使用するため、他リージョンとの競合を事前確認。

## 実装段階（段階導入）

### Phase 1: メモリ構成の更新

- app.ld更新
- `operator new`を`_sheap`基準に変更
- .umia 生成時の `stack_size` 既定値を 16KB に調整

### Phase 2: Kernel側の監視

- Kernel RAMに`MemoryUsage`/`FaultLog`を追加
- Shellの`mem`/`fault`表示に接続

### Phase 3: Fault復旧（段階導入）

- まずは記録＋LED通知のみ
- 安定後にアプリ終了・シェル復帰まで拡張

## 成功条件

- 固定4KBスタックを廃止し、APP_RAMを全域活用できる
- `operator new`がヒープ/スタック衝突を検出できる
- Fault発生時にログが失われない（Kernel RAMに保持）

## 検証項目

- [ ] app.ld 変更後にアプリがビルド・実行できる
- [ ] `_estack`, `_sheap` シンボルが正しく参照される
- [ ] スタックが正しく RAM 終端から成長する
- [ ] MPU 設定で APP_RAM (32KB + 16KB) が保護される
- [ ] APP_RAM 外へのアクセスで MemManage Fault が発生する
- [ ] Fault 後にカーネルが正常に動作を継続する
- [ ] Fault ログがシェルから確認できる
- [ ] Fault ログの保存が ISR ではなくメインループで行われる
- [ ] EXC_RETURN 判定が正しく動作する（PSP/MSP）
- [ ] ヒープ割り当て時の衝突チェックで OOM/panic が発生する

## リスク・注意点

1. **MPU リージョン数**: Cortex-M4 は通常 8 リージョン。現状で十分
2. **アライメント**: MPU リージョンのベースアドレスはサイズにアラインが必要
3. **Fault 中の再入**: `record_fault()` は再入安全である必要（atomic 使用）
4. **スタック破壊時**: スタックが完全に破壊された場合、スタックフレーム取得も失敗する可能性
5. **スタック→ヒープ衝突**: スタック消費は暗黙的なため事前検出不可。ウォーターマーク監視で対応

## 関連ドキュメント

- [PLAN_AUDIOCONTEXT_REFACTOR.md](PLAN_AUDIOCONTEXT_REFACTOR.md) - AudioContext リファクタリング
