# umios-architecture ドキュメント拡張計画

## 現状分析

### 既存ドキュメント（umios-architecture/）

| # | ファイル | カバー範囲 | 完成度 |
|---|---------|-----------|--------|
| 00 | overview | システム全体像・タスクモデル | ○ |
| 01 | audio-context | AudioContext 統一仕様 | ○ |
| 02 | processor-controller | Processor/Controller モデル | ○ |
| 03 | event-system | イベントシステム・EventRouter | ○ |
| 04 | param-system | パラメータシステム | ○ |
| 05 | midi | MIDI 統合 | ○ |
| 06 | syscall | Syscall 番号体系 | ○ |
| 07 | memory | メモリレイアウト（組み込み） | ○ |
| 08 | backend-adapters | バックエンドアダプタ | ○ |
| 09 | app-binary | .umia バイナリ仕様 | ○ |
| 10 | shared-memory | SharedMemory 構造体 | ○ |
| 11 | scheduler | スケジューラ・FPU ポリシー | **△ 大部分がTODO** |

### 不足領域（structure.md との差分）

structure.md が定義する「umios lib」の4柱:

1. **RT-Kernel** — scheduler, notify, wait_block, context switch
2. **System Service** — loader, updater, file system, shell, diagnostics
3. **Memory Management** — MPU, heap/stack monitor, fault handle
4. **Security** — crypto (sha256, sha512, ed25519)

このうち umios-architecture でカバーされているのは:
- RT-Kernel: 11-scheduler に FPU ポリシーのみ。他は TODO
- System Service: 06-syscall に番号体系。個別サービスの設計仕様なし
- Memory Management: 07-memory にレイアウトのみ。MPU/Fault/監視なし
- Security: **完全に未カバー**

### umi-kernel/ の関連仕様（統合元候補）

| umi-kernel ファイル | 関連する追加領域 | 扱い |
|---|---|---|
| spec/kernel.md (§3-5) | RT-Kernel（タスクモデル、スケジューリング、割り込み、通知） | → 11-scheduler に統合 |
| spec/memory-protection.md | Memory Management（MPU、Fault、ヒープ/スタック監視） | → 新規 12 に昇格 |
| spec/system-services.md | System Service（Syscall詳細、監視） | → 06-syscall 拡張 + 新規 13 |
| service/FILESYSTEM.md | System Service（FS設計） | → 新規 13 に含む |
| service/SHELL.md | System Service（シェル） | 空ファイル。新規 13 に項目のみ |
| BOOT_SEQUENCE.md | System Service（loader/起動） | → 新規 13 に含む |

---

## 追加計画

### 方針

- 既存の番号体系（00-11）を継続し、12番以降を追加
- 11-scheduler は TODO を埋める形で **拡充**（新規作成ではない）
- umi-kernel/spec/ の規範仕様を umios-architecture の「目標設計仕様」レベルに抽象化して統合
- ターゲット固有の詳細（STM32F4 レジスタアドレス等）は umios-architecture には入れず、umi-kernel に残す

### 変更一覧

#### 1. 11-scheduler.md — 拡充（RT-Kernel）

**現状:** FPU ポリシーのみ記載、他 TODO
**対応:** umi-kernel/spec/kernel.md §3-5 を元に以下を追記

- タスク優先度と実行モデル（4タスク表、役割分離規範）
- コンテキストスイッチ（レジスタ退避/復元、PendSV/SVC フロー）
- スケジューリングアルゴリズム（O(1) ビットマップ）
- 割り込みとタスク通知（notify、wait_block、WaitEvent）
- タイマーとスリープ（SysTick、delay）
- ポート層 API 一覧（structure.md の RT-Kernel Port セクション参照）

**ソース:** umi-kernel/spec/kernel.md, umi-kernel/ARCHITECTURE.md, structure.md

#### 2. 12-memory-protection.md — 新規（Memory Management）

**理由:** 07-memory はレイアウトのみ。保護・監視・Fault は別の関心事

- MPU 境界設計（AppText/AppData/AppStack/SharedMemory）
- ヒープ/スタック衝突検出アルゴリズム
- Fault 処理と隔離方針（OS 生存保証）
- ヒープ/スタック使用量モニタリング
- ターゲット非搭載時の縮退動作（MPU なし、特権分離なし）

**ソース:** umi-kernel/spec/memory-protection.md, umi-kernel/MEMORY.md

#### 3. 13-system-services.md — 新規（System Service）

**理由:** 06-syscall は ABI 番号体系。サービスの設計・ライフサイクルは別

- サービスアーキテクチャ概要（SystemTask 上で動作するサービス群）
- App Loader（.umia 検証、ロードフロー、sign validator）
- Updater（DFU over SysEx、relocator、CRC validator）
- File System（littlefs 統合、BlockDevice 抽象、非同期 syscall）
- Shell（SysEx stdio、コマンド体系）
- Diagnostics（プロファイラ、ヒープ統計、タスク統計）
- Boot Sequence（Reset→main のフロー概要）

**ソース:** umi-kernel/spec/system-services.md, umi-kernel/service/FILESYSTEM.md, umi-kernel/BOOT_SEQUENCE.md

#### 4. 14-security.md — 新規（Security / Crypto）

**理由:** 完全に未カバー。structure.md に crypto 項目あり

- セキュリティモデル概要（何を守るか: アプリ署名検証、OTA 改竄検知）
- 暗号プリミティブ（SHA-256, SHA-512, Ed25519）
- アプリ署名検証フロー（Loader → sign validator → 実行許可）
- OTA/DFU の完全性検証（CRC + 署名）
- 鍵管理（公開鍵の格納場所、更新方針）
- 組み込み制約（リアルタイム安全性、ROM/RAM フットプリント）

**ソース:** structure.md, 09-app-binary.md（AppHeader の sign フィールド）

#### 5. 06-syscall.md — 軽微な拡張

- 13-system-services への相互参照を追加
- ファイルシステム API の詳細は 13 に委譲する旨を明記

#### 6. README.md — 更新

- ドキュメント一覧に 12-14 を追加
- 推奨読み順フローチャートに新ドキュメントを追加

---

## 新しいドキュメント構成（完成後）

```
00-overview
01-audio-context
02-processor-controller
03-event-system          ← EventRouter はここ
04-param-system
05-midi
06-syscall               ← ABI 定義（軽微更新）
07-memory                ← レイアウト（変更なし）
08-backend-adapters
09-app-binary
10-shared-memory
11-scheduler             ← RT-Kernel 全体に拡充 ★
12-memory-protection     ← 新規: MPU/Fault/監視 ★
13-system-services       ← 新規: Loader/FS/Shell/Diagnostics ★
14-security              ← 新規: Crypto/署名検証 ★
```

## 推奨読み順（追加分）

```
11-scheduler ─→ 12-memory-protection
      │
      ▼
07-memory
      │
      ▼
13-system-services ←── 06-syscall
      │
      ▼
09-app-binary ─→ 14-security
```

## 実施順序

1. **11-scheduler 拡充** — 既存 TODO を埋める。他の新規ドキュメントの基盤
2. **12-memory-protection** — 11 と密接に関連（Fault→タスク停止等）
3. **13-system-services** — Loader/FS 等。12 の MPU 設定に依存
4. **14-security** — Loader の署名検証に依存するため最後
5. **06-syscall, README.md 更新** — 相互参照の整備

## umi-kernel/ との関係

- umi-kernel/spec/ は**ポーティング向けの規範仕様**（MUST/SHALL レベル）として維持
- umios-architecture/ は**設計仕様**（目標アーキテクチャ、ターゲット非依存の考え方）
- 重複する内容は umios-architecture を正とし、umi-kernel/spec/ から参照する形にする
- umi-kernel/platform/ のターゲット固有情報は umios-architecture には含めない

---

## 統合前の矛盾チェック

umios-architecture/ と umi-kernel/ の間で確認された不整合を以下にまとめる。
新ドキュメント作成時にこれらを解決しないと矛盾が拡大するため、**統合作業の前に方針を確定する必要がある**。

### 凡例

- **正:** どちらを正とするか
- **対応:** 具体的な修正アクション

---

### 矛盾1（高）: Syscall 番号体系が3系統存在する

| ドキュメント | 体系 | 例: Yield / RegisterProc / Log |
|---|---|---|
| 06-syscall.md | 10刻みスパース | 1 / 2 / 50 |
| umi-kernel/spec/system-services.md | 16刻みブロック | 1 / 5 / (なし) |
| umi-kernel/ARCHITECTURE.md | 独自配置 | 5 / 1 / 20 |
| **実装コード** (syscall_numbers.hh) | system-services.md + Configuration(20-25) | 1 / 5 / 10 |

**正:** 06-syscall.md（目標設計）を正とする。README.md で「矛盾がある場合は本仕様を正とする」と宣言済み。
**対応:**
1. 実装コードは現時点では旧番号のまま運用可（移行は実装フェーズ）
2. umi-kernel/ARCHITECTURE.md の Syscall 一覧は**廃止注記**を追加し、06-syscall.md を参照させる
3. umi-kernel/spec/system-services.md は 06-syscall.md への参照に書き換える
4. 13-system-services.md 作成時は 06-syscall.md の番号体系のみを使う

### 矛盾2（高）: FS Syscall 番号（60番台 vs 32番台）

06-syscall.md では FileOpen=60、実装と system-services.md では FileOpen=32。

**正:** 06-syscall.md（60番台）
**対応:** 矛盾1 と同じ。実装の移行は別途。13-system-services.md では 60番台を使用。

### 矛盾3（高）: メモリレイアウトのアドレス範囲

| 項目 | 07-memory.md | umi-kernel/spec/memory-protection.md |
|---|---|---|
| App Data | 0x2000C000–0x20018000 (48KB) | 0x2000C000–0x20014000 (32KB) |
| App Stack | 0x2001C000–0x20020000 (16KB) | 0x20014000–0x20018000 (16KB) |
| SRAM 使用上限 | 0x20020000 (128KB) | 0x20018000 (96KB) |

**正:** 要確認。07-memory.md は STM32F407VG の SRAM 128KB をフルに使う設計。memory-protection.md は 96KB しか使っていない。
**提案:** 07-memory.md の値が新しい設計意図であると考えられるため 07-memory.md を正とする。ただし実装（リンカスクリプト）との整合確認が必要。12-memory-protection.md 作成時に 07-memory.md のアドレスを使用し、memory-protection.md は参照のみとする。

### 矛盾4（中）: FPU Exclusive ポリシーの意味が逆

| ドキュメント | Exclusive の動作 |
|---|---|
| 11-scheduler.md | 「常に保存/復元する」 |
| umi-kernel/ARCHITECTURE.md | 「独占所有のため保存/復元**不要**」 |

**正:** ARCHITECTURE.md の解釈が論理的に正しい。「Exclusive = そのタスクが FPU を独占 = 他タスクは FPU 不使用 = 退避不要」。
**対応:** 11-scheduler.md の Exclusive の説明を修正する。拡充時に合わせて対応。

### 矛盾5（中）: SharedMemory の buffer_size（64 vs 256）

ARCHITECTURE.md は 64 frames、10-shared-memory.md は 256 frames。

**正:** 07-memory.md で階層を明確化済み（DMA=64, process()=256）。10-shared-memory.md の 256 が正。
**対応:** ARCHITECTURE.md の更新は umios-architecture のスコープ外。新規ドキュメントでは 256 を使用。

### 矛盾6（中）: SharedMemory params の型（flat array vs SharedParamState）

ARCHITECTURE.md と application.md は `params[32]`、10-shared-memory.md は `SharedParamState`。

**正:** 10-shared-memory.md（新設計）
**対応:** 新規ドキュメントでは SharedParamState を使用。umi-kernel 側は参照更新のみ。

### 矛盾7（中）: イベントフラグ名 Button vs Control

06-syscall.md は `Control = (1 << 4)` に改名。実装コードは `Button` のまま。

**正:** 06-syscall.md（Control）
**対応:** 新規ドキュメントでは Control を使用。実装の改名は実装フェーズ。

### 矛盾8（中）: Syscall 呼び出し規約（r12 vs r0）

system-services.md の概念コードでは r0 に番号を渡しているが、06-syscall.md と実装は r12。

**正:** 06-syscall.md + 実装（r12）
**対応:** system-services.md の概念コードは参考程度。13-system-services.md では 06-syscall.md の規約を参照。

### 矛盾9（中）: Configuration API 内の番号並び順

| Syscall | 06-syscall.md | 実装 (syscall_numbers.hh) |
|---|---|---|
| SetRouteTable | 21 | 20 |
| SetParamMapping | 22 | 21 |
| SetInputMapping | 23 | 22 |
| ConfigureInput | 24 | 23 |
| SetAppConfig | 20 | 24 |
| SendParamRequest | 25 | 25 |

**正:** 06-syscall.md（目標設計）
**対応:** 実装の移行は実装フェーズ。新規ドキュメントでは 06-syscall.md の番号を使用。

### 矛盾10（中）: ARCHITECTURE.md に存在し目標設計で廃止された Syscall

SendEvent, PeekEvent, GetParam, SetParam, SetLed, GetLed, GetButton が ARCHITECTURE.md にあるが 06-syscall.md にない。

**正:** 06-syscall.md（これらは SharedMemory 経由に移行済み）
**対応:** 新規ドキュメントではこれらの Syscall を参照しない。ARCHITECTURE.md には「06-syscall.md を参照」の注記を推奨。

---

### 統合作業の前提条件まとめ

新ドキュメント（11拡充, 12-14新規）を書く際の統一ルール:

1. **Syscall 番号は 06-syscall.md の体系を使用する**（10刻みスパース）
2. **メモリアドレスは 07-memory.md を使用する**（128KB フル活用）
3. **SharedMemory 構造体は 10-shared-memory.md を使用する**（SharedParamState）
4. **FPU Exclusive は「独占 = 退避不要」に修正する**
5. **イベント名は Control（Button ではなく）を使用する**
6. **umi-kernel/ のドキュメント修正は本計画のスコープ外**とし、別途対応する
7. 各新規ドキュメントの冒頭に「状態」欄を設け、既存ドキュメントとの対応を明記する
