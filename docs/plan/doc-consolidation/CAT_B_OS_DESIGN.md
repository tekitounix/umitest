# CAT_B: OS設計 — 統合内容要約

**カテゴリ:** B. OS設計
**配置先:** `docs/umi-kernel/` + `docs/umios-architecture/`
**前提仕様:** [LIBRARY_SPEC.md](../LIBRARY_SPEC.md) v1.3.0 / [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) v1.1.0
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

> **注意:** IMPLEMENTATION_PLAN v1.1.0 により、コードパス `lib/umi/kernel/` 等は Phase 0 で `lib/_archive/` に退避される。本文書内のコードパスは**現行の配置**を記録している。また、`examples/stm32f4_kernel/` は `stm32f4_os` として再構築される。

---

## 1. カテゴリ概要

UMI-OS カーネルの仕様・設計・実装に関する文書群。カーネル仕様正本（spec/）、OS アーキテクチャ仕様（umios-architecture/）、プラットフォーム固有実装（platform/）、ADR、旧文書を含む。

**対象読者:** カーネル開発者、移植者
**最大の問題:** 同じトピック（カーネルアーキテクチャ、ブートシーケンス、メモリ保護等）が3つのディレクトリ（`docs/umi-kernel/`, `docs/umios-architecture/`, `docs/archive/umi-kernel/`）に分散し、正本が不明確。

---

## 2. 所属ドキュメント一覧

### 2.1 正本（保持）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 1 | docs/umi-kernel/spec/kernel.md | ~120 | ★ | **保持（正本）** |
| 2 | docs/umi-kernel/spec/application.md | ~120 | ★ | **保持（正本）** |
| 3 | docs/umi-kernel/spec/memory-protection.md | ~100 | ★ | **保持（正本）** |
| 4 | docs/umi-kernel/spec/system-services.md | ~120 | ★ | **保持（正本）** |
| 5 | docs/umi-kernel/adr.md | ~23 | ★ | **保持（正本）** |
| 6 | docs/umi-kernel/plan.md | ~225 | ★ | **保持（メタ文書）** |
| 7 | docs/umi-kernel/platform/stm32f4.md | ~100 | ★ | **保持（正本）** |

### 2.2 umios-architecture（保持）

| # | サブディレクトリ | ファイル数 | 有用性 | 統廃合アクション |
|---|-----------------|-----------|--------|-----------------|
| 8 | 00-fundamentals/ | 3+index | ★ | **保持** — コア概念の正本 |
| 9 | 01-application/ | 6+index | ★ | **保持** — アプリ層の正本 |
| 10 | 02-kernel/ | 4+index | ★ | **保持** — カーネル実装の正本 |
| 11 | 03-port/ | 2+index | ★ | **保持** — ポート層の正本 |
| 12 | 04-services/ | 5+index | ★ | **保持** — サービスの正本 |
| 13 | 05-binary/ | 2+index | ★ | **保持** — バイナリ仕様の正本 |
| 14 | 06-09 (dsp/usb/gfx/midi) | 4 index | ▽ | **保持** — プレースホルダ（空） |
| 15 | 99-proposals/ | 5+index | ◆ | **保持** — 一部は実装済みで参考 |

### 2.3 統合/archive 対象

| # | ファイル | 有用性 | 統廃合アクション |
|---|---------|--------|-----------------|
| 16 | docs/umi-kernel/OVERVIEW.md | ◆ | **archive移動** — spec/kernel.md に内容包含 |
| 17 | docs/umi-kernel/ARCHITECTURE.md | ◆ | **archive移動** — umios-architecture/02-kernel/ が詳細版 |
| 18 | docs/umi-kernel/BOOT_SEQUENCE.md | ◆ | **archive移動** — 15-boot-sequence.md が正本 |
| 19 | docs/umi-kernel/DESIGN_DECISIONS.md | ▽ | **archive移動** — adr.md に統合 |
| 20 | docs/umi-kernel/IMPLEMENTATION_PLAN.md | ▽ | **archive移動** — docs/dev/IMPLEMENTATION_PLAN.md が最新 |
| 21 | docs/umi-kernel/MEMORY.md | ▽ | **archive移動** — spec/memory-protection.md が正本 |
| 22 | docs/umi-kernel/service/FILESYSTEM.md | ▽ | **archive移動** — umios-architecture/04-services/ が正本 |
| 23 | docs/umi-kernel/service/SHELL.md | ▽ | **archive移動** — umios-architecture/04-services/17-shell.md が正本 |
| 24 | docs/umios-architecture/README.md | ◆ | **統合** — index.md とほぼ同一内容。1つに統合 |
| 25 | docs/umios-architecture/index.md | ◆ | **保持** — README.md の内容を吸収 |

### 2.4 archive から削除

| # | ファイル | アクション | 理由 |
|---|---------|-----------|------|
| 26 | docs/archive/umi-kernel/OVERVIEW.md | **削除** | docs/umi-kernel/ と完全同一 |
| 27 | docs/archive/umi-kernel/DESIGN_DECISIONS.md | **削除** | docs/umi-kernel/ と完全同一 |
| 28 | docs/archive/umi-kernel/MEMORY.md | **削除** | docs/umi-kernel/ と完全同一 |
| 29 | docs/archive/umi-kernel/service/FILESYSTEM.md | **削除** | docs/umi-kernel/ と完全同一 |
| 30 | docs/archive/umi-kernel/service/SHELL.md | **削除** | docs/umi-kernel/ と完全同一 |
| 31 | docs/archive/umi-kernel/ARCHITECTURE.md | **保持(archive)** | umi-kernel/ 版と異なる旧バージョン |
| 32 | docs/archive/umi-kernel/BOOT_SEQUENCE.md | **保持(archive)** | 同上 |
| 33 | docs/archive/umi-kernel/IMPLEMENTATION_PLAN.md | **保持(archive)** | 同上 |

---

## 3. ドキュメント別内容要約

### 3.1 spec/kernel.md — カーネル仕様

規範文書。RFC スタイルの MUST/SHALL/SHOULD を使用。

**主要内容:**
- **実行モデル** — OS/アプリのバイナリ分離、非特権モード実行、syscall 経由のアクセス
- **タスクモデル** — 4タスク固定（AudioTask=P0, SystemTask=P1, ControlTask=P2, IdleTask=P3）
- **FPU ポリシー** — `uses_fpu` 宣言、EXC_RETURN 切替
- **スケジューリング** — O(1) ビットマップ方式、PendSV コンテキストスイッチ
- **例外/割り込み** — SysTick(1ms tick), SVC(syscall), PendSV(context switch)
- **タスク通知/イベント** — notify/wait_block の原子的操作

**コード参照:** `lib/umi/kernel/umi_kernel.hh`, `lib/umi/kernel/port/cm4/context.hh`

---

### 3.2 spec/application.md — アプリケーション規格仕様

.umia バイナリ形式の規範的定義。

**主要内容:**
- **AppHeader** — 128 bytes、magic("UMIA")、ABI version、target(User/Development/Release)
- **検証チェーン** — magic → ABI → target互換 → size整合 → entry_offset範囲 → CRC32 → Ed25519署名(Release時)
- **リロケーション** — リロケータブル ELF → flat binary → Flash 書き込み
- **エントリポイント契約** — `_start` → `main()` → `register_processor()` → `wait_event()` ループ
- **process() のリアルタイム制約** — ヒープ/mutex/例外/stdio 禁止

---

### 3.3 spec/memory-protection.md — メモリ/保護仕様

MPU 設定とメモリレイアウトの規範。

**主要内容:**
- **APP_RAM レイアウト** — Data 領域 + Stack 領域の2リージョン保護
- **MPU 境界** — AppText(RX), AppData/AppStack(RW-NX), SharedMemory(RW-NX)
- **ヒープ/スタック衝突検出** — ヒープ拡張時の SP 参照チェック
- **Fault ログと隔離** — HardFault/MemManage/BusFault の記録と復旧方針

---

### 3.4 spec/system-services.md — システムサービス仕様

OS 提供サービスの規範。

**主要内容:**
- **Syscall 番号体系** — 0-15: コアAPI, 16-31: 予約, 32-47: FS, 48-63: 予約, 64-255: ベンダ拡張
- **Syscall 選定基準** — 特権HWアクセス / スケジューラ状態変更 / ブートストラップ の3条件
- **コアAPI 詳細** — Exit(0), Yield(1), WaitEvent(2), GetTime(3), GetShared(4), RegisterProc(5)
- **FS API** — 非同期一本化（ADR-0005）、FileOpen(32)〜DirClose(40)
- **SysEx/Shell/stdio** — SysEx 経由の入出力
- **USB MIDI/Audio** — ストリーミング状態管理
- **監視/ログ** — メトリクス、診断出力

---

### 3.5 adr.md — Architecture Decision Records

5件の簡潔な設計判断記録。

| ADR | 内容 |
|-----|------|
| ADR-0001 | Syscall 番号体系のグルーピング方式 |
| ADR-0002 | SharedMemory 優先方針（syscall は最小限） |
| ADR-0003 | SharedMemory 先頭へのメタ情報集約 |
| ADR-0004 | SystemTask > ControlTask の優先度関係 |
| ADR-0005 | FS 非同期一本化、完了通知はイベント |

---

### 3.6 platform/stm32f4.md — STM32F4 実装仕様

STM32F407VG 固有の実装規範。

**主要内容:**
- **起動シーケンス** — Reset → FPU有効化 → .data/.bss初期化 → SRAMベクタテーブル → VTOR切替 → IRQ登録 → main()
- **ベクタテーブル** — Flash に最小ベクタ(SP+Reset)、SRAMに動的テーブル
- **例外/IRQ 優先度** — Audio DMA 最優先、USB 次点、SysTick 低優先度、PendSV 最下位
- **DMA/Audio/MIDI フロー** — ダブルバッファリング、コールバック設計

---

### 3.7 umios-architecture/ — OS設計仕様群（41ファイル）

コードディレクトリ構成と対応する体系的な設計仕様群。

**構成:**
| セクション | 対応コード | 内容 | ファイル数 |
|-----------|-----------|------|-----------|
| 00-fundamentals | lib/umi/core/ | AudioContext, Processor/Controller モデル | 3 |
| 01-application | lib/umi/app/ | Events, Parameters, MIDI, Backend, SharedMemory | 6 |
| 02-kernel | lib/umi/kernel/ | Scheduler, MPU, Boot, AppLoader | 4 |
| 03-port | lib/umi/port/ | Syscall, Memory layout | 2 |
| 04-services | lib/umi/service/ | Shell, Updater, Storage, Diagnostics | 5 |
| 05-binary | lib/umi/boot/ | App binary format, Security | 2 |
| 99-proposals | — | 設計提案（io-port v1-v4, syscall-redesign 等） | 5 |

**特徴:**
- `lib/umi/` のコードディレクトリと1:1で対応
- 実装済み(✓) / 将来(💡) のステータスが明示
- README.md と index.md がほぼ同内容で重複（→ 統合対象）

---

### 3.8 旧カーネル文書（archive 対象）

| 文書 | 現在の正本 | 差異 |
|------|-----------|------|
| OVERVIEW.md | spec/kernel.md | 概要レベルの情報。spec/ に内容包含 |
| ARCHITECTURE.md | umios-architecture/02-kernel/ | 旧アーキテクチャ記述。02-kernel/ が最新 |
| BOOT_SEQUENCE.md | 02-kernel/15-boot-sequence.md | 旧ブートシーケンス。15-boot-sequence.md が正本 |
| DESIGN_DECISIONS.md | adr.md | 設計判断。adr.md に簡潔化して移行済み |
| MEMORY.md | spec/memory-protection.md | メモリマップ。spec/ に正式化 |
| IMPLEMENTATION_PLAN.md | docs/dev/IMPLEMENTATION_PLAN.md | 実装計画。dev/ の方が最新 |

---

## 4. カテゴリ内の関連性マップ

```
docs/umi-kernel/
├── spec/ ← 正本（規範的仕様）
│   ├── kernel.md ← タスク・スケジューラ・例外
│   ├── application.md ← .umia形式・ロード契約
│   ├── memory-protection.md ← MPU・メモリレイアウト
│   └── system-services.md ← Syscall・サービス
├── adr.md ← 設計判断記録（5件）
├── plan.md ← ドキュメント再構成計画
└── platform/stm32f4.md ← プラットフォーム固有

docs/umios-architecture/ ← 正本（設計仕様）
├── 00-fundamentals/ ← AudioContext, Processor/Controller
├── 01-application/ ← Events, Params, MIDI, Backend
├── 02-kernel/ ← Scheduler(11), MPU(12), Boot(15), AppLoader(16)
│                  ↕ 補完関係
│              spec/kernel.md, spec/memory-protection.md
├── 03-port/ ← Syscall(06), Memory(07)
│                  ↕ 補完関係
│              spec/system-services.md
├── 04-services/ ← Shell(17), Updater(18), Storage(19), Diagnostics(20)
├── 05-binary/ ← AppBinary(09), Security(14)
│                  ↕ 補完関係
│              spec/application.md, docs/refs/SECURITY.md
└── 99-proposals/ ← 設計提案
```

**spec/ と umios-architecture/ の関係:**
- `spec/` = 規範的仕様（MUST/SHALL ベース、簡潔）
- `umios-architecture/` = 設計仕様（詳細な背景説明、図、コード例）
- 両者は補完関係にあり、統合ではなく相互リンクで整理

---

## 5. 統廃合アクション

### Phase 2 実行項目（カーネル文書一本化）

| ステップ | アクション | 対象 |
|---------|-----------|------|
| 2.1 | archive 内の完全同一コピー5件を削除 | archive/umi-kernel/ の同一ファイル |
| 2.2 | umi-kernel/ 直下の旧文書6件を archive に移動 | OVERVIEW, ARCHITECTURE, BOOT_SEQUENCE, DESIGN_DECISIONS, MEMORY, IMPLEMENTATION_PLAN |
| 2.3 | umi-kernel/service/ を archive に移動 | FILESYSTEM.md, SHELL.md |
| 2.4 | umios-architecture/README.md と index.md を統合 | 内容が重複するため1ファイルに |
| 2.5 | spec/ 内の相互リンクを整備 | umios-architecture/ の対応セクションへのリンク追加 |

### リスク

| リスク | 緩和策 |
|--------|--------|
| plan.md が旧文書を参照 | plan.md 内のパスを更新 |
| spec/ と umios-architecture/ の内容不整合 | 移動前に差分確認、必要なら spec/ を更新 |

---

## 6. 品質評価

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★★ | カーネルの全側面がカバーされている |
| 一貫性 | ★★★☆☆ | 3箇所に分散が最大の問題。spec/ は統一フォーマット |
| 更新頻度 | ★★★★☆ | spec/ と umios-architecture/ は2026-02に更新 |
| 読みやすさ | ★★★★☆ | spec/ は規範スタイルで明快。umios-architecture/ は図が豊富 |
| コードとの整合 | ★★★★☆ | 実装コードへの参照が充実（ファイルパス、コード抜粋） |

---

## 7. 推奨事項

1. **Phase 2 の最優先実行** — カーネル文書の3重管理解消が本カテゴリ最大の改善
2. **spec/ と umios-architecture/ の役割明確化** — spec/ = What（何を保証するか）、umios-arch/ = How（どう実現するか）
3. **README.md / index.md の統合** — umios-architecture/ のナビゲーションを1ファイルに
4. **99-proposals/ の棚卸し** — 実装済みの提案は明示的に「adopted」マーク
5. **plan.md の更新** — 本統廃合計画との整合性を確保
