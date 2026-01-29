# UMIOS 設計決定事項

## 1. Syscall体系

### 番号体系

```
0–15:   コアAPI（プロセス制御・スケジューリング・情報取得）
16–31:  予約（コアAPI拡張）
32–47:  ファイルシステム
48–63:  予約（ストレージ拡張）
64–255: 予約（実験・ベンダ拡張）
```

### 番号表

| # | Syscall | 状態 | 用途 |
|---|---------|------|------|
| **コアAPI (0–15)** ||||
| 0 | `Exit` | 実装済み | アプリ終了（将来: アンロードトリガー） |
| 1 | `Yield` | 実装済み | CPU譲渡 |
| 2 | `WaitEvent` | 実装済み | イベント待ち（timeout引数付き） |
| 3 | `GetTime` | 実装済み | モノトニック時刻取得 |
| 4 | `GetShared` | 実装済み | SharedMemoryポインタ取得 |
| 5 | `RegisterProc` | 実装済み | Processorタスク登録 |
| 6 | `UnregisterProc` | 将来 | Processor登録解除 |
| 7–15 | — | 予約 ||
| **ファイルシステム (32–47)** ||||
| 32 | `FileOpen` | 将来 | ファイルオープン |
| 33 | `FileRead` | 将来 | ファイル読み取り |
| 34 | `FileWrite` | 将来 | ファイル書き込み |
| 35 | `FileClose` | 将来 | ファイルクローズ |
| 36 | `FileSeek` | 将来 | シーク（サンプラー必須） |
| 37 | `FileStat` | 将来 | サイズ取得（バッファ事前確保） |
| 38 | `DirOpen` | 将来 | ディレクトリオープン |
| 39 | `DirRead` | 将来 | ディレクトリ列挙 |
| 40 | `DirClose` | 将来 | ディレクトリクローズ |
| 41–47 | — | 予約 ||

### WaitEvent引数仕様

```cpp
int32_t WaitEvent(uint32_t mask, uint32_t timeout_us);
```

- `mask`: 待ちイベントビットマスク（event::Audio, event::Midi等のOR）
- `timeout_us`: タイムアウト（マイクロ秒）。`0` = 無期限待ち
- 戻り値: 発生したイベントビットマスク

### OS-アプリ間通信の二択

アプリがOSとやり取りする手段は以下の2種類のみ:

| 手段 | 特性 | 用途 |
|------|------|------|
| **SharedMemory** | ゼロオーバーヘッド。アプリから直接読み書き | オーディオバッファ、メタ情報、リングバッファ等のデータ共有 |
| **Syscall** | SVC例外経由。OS特権で実行 | 特権操作、スケジューラ状態変更、ブートストラップ |

原則: **SharedMemoryで済むものはsyscallにしない**。Syscallは上記3条件のいずれかを満たす場合のみ。

### 設計原則

1. **syscallは最小限** — SharedMemoryで済むものはsyscallにしない
2. **番号は連番で詰める** — グループ内は隙間なし
3. **グループ間は予約で空ける** — 将来拡張用
4. **番号体系は独自** — POSIX/Linux準拠の意味がない（プロセスモデルが根本的に異なる）
5. **番号グループ = namespace** — グループごとにサブnamespaceを分ける

### アプリ側API namespace構造

```
umi::syscall::            コアAPI（exit, yield, wait_event, get_time, ...）
umi::syscall::fs::        ファイルシステム（open, read, write, close, seek, stat, ...）
```

番号グループ（0–15: コア、32–47: FS）がnamespace構造に直接対応する。
将来グループ追加時も同じパターン（例: `umi::syscall::midi::`）。

### 各Syscallの存在根拠

Syscallにすべきか、SharedMemoryで済むかの判定基準:

1. **特権ハードウェアアクセスが必要** → syscall（MPU境界を越える）
2. **スケジューラ状態変更が必要** → syscall（アトミック性が必須）
3. **ブートストラップ問題** → syscall（SharedMemory自体のアドレス取得）

| Syscall | 根拠 |
|---------|------|
| `Exit` | スケジューラ状態変更（タスク終了）+ 特権操作（MPU解放、Processor切断） |
| `Yield` | スケジューラ状態変更（PendSVトリガー）。SharedMemoryフラグでは不可 |
| `WaitEvent` | スケジューラ状態変更（Blocked遷移）+ アトミックなフラグ確認→ブロック |
| `GetTime` | 64bit RTC読み取り。Cortex-M4のLDRDは非アトミック → SVC内クリティカルセクションで安全な読み取りが必要 |
| `GetShared` | ブートストラップ問題。SharedMemoryポインタ自体をアプリに渡す手段が他にない |
| `RegisterProc` | 特権操作（オーディオパスへのProcessor接続）。OS内部構造への書き込み |
| `UnregisterProc` | 同上（Processor切断） |
| FS系 (32–40) | 特権ハードウェアアクセス（Flash/SD、MPU境界）+ 排他制御 + ブロッキング操作（タスク状態管理） |

#### GetTime vs アプリ側タイマーの使い分け

| 用途 | 方式 | 理由 |
|------|------|------|
| RTC（モノトニック時刻） | `GetTime` syscall | 64bit、精度重要、アトミック性必須 |
| 短インターバル計測 | SharedMemory 32bitカウンタ or DWT CYCCNT | ゼロオーバーヘッド、速度最優先 |

#### デバッグ出力

デバッグ出力はUARTではなくMIDI SysEx経由。SharedMemoryリングバッファ → OS側シェルサービスが送出。専用syscall（DebugLog等）は不要。

### メタ情報はSharedMemoryヘッダに統合

```cpp
struct SharedMemory {
    // --- メタ情報（先頭固定） ---
    uint32_t abi_version;      // ABI互換判定（メジャー変更で互換性なし）
    uint32_t shared_size;      // SharedMemory全体サイズ（バイト）
    uint32_t app_ram_size;     // アプリ使用可能RAM（バイト）
    uint32_t max_processors;   // 登録可能Processor数
    uint32_t feature_flags;    // 機能フラグ（ビット定義は別途）
    // --- 既存フィールド ---
    uint32_t sample_rate;
    uint64_t sample_position;
    // ...
};
```

- `abi_version`: メジャー.マイナーをパック（`(major << 16) | minor`）。メジャー変更 = 非互換
- `feature_flags` ビット定義（将来）:
  - bit 0: ファイルシステム有効
  - bit 1: USB MIDI有効
  - bit 2: PDMマイク有効
  - bit 3–31: 予約

GetVersion, GetLimits, GetSharedInfo等の情報取得syscallは不要。`GetShared`一発で全メタ情報を取得。

---

## 2. アプリ形式（.umiapp）

### ELFベース + Flash書き込み時リロケーション

```
.umiapp (ELF, relocatable)
  → リロケーション（.rel.dynセクション処理）
  → flat binary
  → Flash書き込み
  → XIP実行
```

### 利点

- **ゼロランタイムオーバーヘッド**: fPICのGOT間接参照なし
- **アーキテクチャ非依存**: M0, M4, M7, ESP32, RISC-V全て同一方式
- **標準ツールチェーン**: `-fPIE -pie`でビルド、ELF標準形式

### 代替案の却下理由

| 方式 | 却下理由 |
|------|---------|
| fPIC（GOT/PLT） | M4にデータキャッシュなし。GOT間接参照が毎アクセスFlash→SRAM往復 |
| 固定アドレスリンク | アドレス変更不可。マルチスロット・異機種対応不能 |
| RAM上ロード時リロケーション | Flash XIP不可。RAM消費大 |

### ハードウェア構成パターン

リロケーションは書き込み先アドレスに合わせて参照を解決するだけなので、配置先に依存しない。

| 構成 | アプリ配置 | XIP | 例 |
|------|-----------|-----|-----|
| 内蔵Flash大容量 | 内蔵Flashの後半セクタ | 可 | STM32F4 (1MB, 複数セクタ) |
| 内蔵Flash小 + 外部QSPI | 外部QSPI Flash | 可（memory-mapped） | STM32H750 (128KB内蔵 + 外部QSPI) |
| 内蔵Flash小 + 外部SPI | RAM（外部SPIからロード） | 不可 → RAMコピー実行 | SPI Flashのみの構成 |
| RAM潤沢 | RAM | — | ESP32, RISC-V等 |

**XIP可能**（内蔵Flash、QSPI memory-mapped）: リロケーション済みflat binaryをそのまま書き込み → 直接実行。RAM消費はスタック+データのみ。

**XIP不可**（SPI Flash、memory-mapped非対応）: Flash上にリロケーション済みバイナリを保存 → 起動時にRAMへコピーして実行。RAM消費が増える（コード+データ）。

### ビルドとFlash書き込み

#### ビルド（elf → umiapp）

```
arm-none-eabi-gcc (-fPIE -pie) → ELF → elf2umiapp → .umiapp (relocatable ELF)
```

`elf2umiapp`はELFからumiapp形式への変換ツール。xmakeビルドルールから自動呼び出し。

#### Flash書き込みツール

| ツール | 書き込み経路 | 用途 |
|--------|-------------|------|
| `umiflash` | pyOCD（SWD） | 開発用。デバッガ経由で直接Flash書き込み |
| `umiflash` | システムDFUブートローダ（USB） | 生産用。USB経由でFlash書き込み |
| デバイスアップデータ | SysEx DFU（USB MIDI） | フィールドアップデート。OS上で動作 |

`umiflash`はPython製ホストツール。ELFパース → リロケーション → flat binary変換 → Flash書き込みを一貫して行う。pyOCDもPythonライブラリのため、単一のPythonツールで完結。

デバイスアップデータはOS上のSystemTaskで動作（§7参照）。リロケーションロジックはC++で実装し、デバイス側で使用。

---

## 3. アプリロード・アンロード

### ライフサイクル

```
OS: elf2flash(slot, app.umiapp)  ← リロケーション + Flash書き込み
OS: load_app(slot)               ← MPU設定、エントリ呼び出し
App: RegisterProc(...)           ← syscall
App: WaitEvent / Yield ループ
App: Exit(0)                     ← syscall（またはOS側から強制終了）
OS: unload_app()                 ← Proc切断、メモリ解放
OS: load_app(next_slot)          ← 次のアプリ（AB切り替え）
```

### Exit syscallの拡張

`Exit`はアプリ終了通知からアンロードトリガーに拡張:
1. アプリのProcessorタスクを全てsuspend
2. オーディオパスからProcessor切断（ミュート or バイパス）
3. アプリのメモリ領域解放（MPU無効化）
4. 次のアプリをロード or 待機状態へ

### 考慮事項

- **オーディオ連続性**: アンロード→ロード間のクロスフェード or ミュート期間。Processor未登録時はOS側で無音出力
- **リソース制約通知**: SharedMemoryヘッダのメタ情報（app_ram_size, max_processors）で通知
- **アプリ署名・検証**: ELFにHMAC/署名セクション付与。書き込み時は整合性チェック、起動時に署名検証（どちらか一方なら起動時を優先）。開発時スキップ可
- **Fault隔離**: MPUでアプリ領域分離。HardFault/MPU Fault時にOS生存→アプリのみアンロード

---

## 4. IPC（プロセス間通信）

### 現行モデル

| 通信 | 方式 |
|------|------|
| OS → App（オーディオバッファ） | SharedMemory（`_shared_start`） |
| App → OS（Processor出力） | SharedMemory |
| ISR → Task（イベント通知） | `notify()` / `wait_block()` |
| Task間（MIDI, ボタン等） | SpscQueue（ロックフリー） |

### 設計原則

- リアルタイムパス（process()）: ロックフリーのみ。ヒープ割り当て・ブロッキング禁止
- 非リアルタイムパス: syscall経由（WaitEvent, GetShared）
- stdio: SysEx経由プロトコル。OS側シェルサービスが処理

---

## 5. ファイルシステム（将来）

| 項目 | 方針 |
|------|------|
| メディア | 外部SPI Flash / SD (SDIO or SPI) |
| FS | LittleFS（NOR Flash向け、ウェアレベリング内蔵）or FatFs（SD互換性） |
| アクセスモデル | OS側サービスタスク（Server優先度）がFS操作担当 |
| アプリからのアクセス | syscall経由（FileOpen/Read/Write/Close/Seek/Stat, DirOpen/Read/Close） |
| リアルタイム安全 | process()から直接アクセス不可。事前ロード → SharedMemory経由バッファ渡し |
| 用途 | サンプラー、プリセット保存、ウェーブテーブル等 |

---

## 6. Hw<Impl>インターフェース

### 保持（スケジューラ必須）

```
enter_critical / exit_critical     — クリティカルセクション
request_context_switch             — PendSV
monotonic_time_usecs               — tickless
set_timer_absolute                 — tickless
enter_sleep                        — idle
trigger_ipi / current_core         — マルチコア
cycle_count / cycles_per_usec      — LoadMonitor
```

### 削除済み（OS層/arch層に移動）

```
save_fpu / restore_fpu             → arch層（PendSV内で直接処理）
mute_audio_dma                     → OS層
write_backup_ram / read_backup_ram → OS層
configure_mpu_region               → OS層
cache_clean / invalidate / clean_invalidate → OS層
system_reset                       → OS層
start_first_task                   → OS層
watchdog_init / watchdog_feed      → OS層
```

---

## 7. タスクアーキテクチャ

### 4タスク構成

| タスク | 優先度 | 責務 |
|--------|--------|------|
| AudioTask | Realtime (0) | オーディオ処理（DMAコールバック → Processor.process()） |
| SystemTask | Server (1) | ドライバ・サービス（USB SysEx、シェル、FS操作、アップデータ） |
| ControlTask | User (2) | アプリケーションmain()（Controller task） |
| IdleTask | Idle (3) | スリープ・電力管理 |

### 分離の根拠

| 境界 | 理由 |
|------|------|
| Audio ↔ System | **デッドライン保証**。オーディオはハードリアルタイム（数百μs）。System処理がブロックしてもオーディオは中断しない |
| System ↔ Control | **特権/MPU分離**。SystemはOS特権でハードウェア直接アクセス。Controlはアプリ（MPU隔離）。Fault時にOS生存 |
| Idle ↔ 全タスク | **電力管理**。全タスクBlocked時のみIdleが実行されWFI/スリープ。専用タスクでないと判定不可 |

### SystemTask > ControlTask の優先度根拠

SystemTask（Server優先度1）がControlTask（User優先度2）より高い理由は **OS生存保証**:

- ControlTaskはアプリコード（MPU隔離）。HardFault/MPU Faultが起こりうる
- Fault発生時、SystemTaskが生存していればアプリのアンロード・復旧が可能
- SystemTaskが低優先度だと、アプリFault時にOSサービス（シェル、DFU）も巻き添えで応答不能になるリスクがある
- 「OSは常にアプリより先に動ける」ことが、Fault隔離アーキテクチャの前提

### SystemTaskの統合サービス

以下は全てSystemTask内で処理。個別タスク化しない:

- **シェル**: SysEx経由のコマンド応答
- **FS操作**: アプリのsyscall要求をFlash/SDに実行
- **アップデータ**: SysEx DFU受信 → Flash書き込み（通常アプリとモード排他）

#### アップデータのモード切り替え

アップデータ実行時は通常アプリを停止し、SystemTask内でモード切り替え:

```
通常モード: SystemTask = シェル + FS + ドライバ
更新モード: SystemTask = アップデータ（DFU受信 → Flash書き込み）
```

通常アプリとアップデータは相互排他。別タスクにする必要はない（同時実行しないため）。

#### FS syscallの非同期設計

FS syscallは非同期一本化。要求発行→即return→event::FSで完了通知:

- ControlTaskはUI/表示も担う。同期syscallだとFS操作中にUIがフリーズする
- syscallは最小プリミティブ。同期/非同期の選択はアプリ層の判断事項
- 同期的に使いたい場合はアプリ側ユーティリティで「発行+wait」をまとめる

#### BlockDevice内のブロッキング方式

SystemTask > ControlTaskの優先度関係上、SystemTaskがCPU占有するとControlTaskは動けない。
BlockDevice内部の待ち方を操作ごとに使い分けることで、長時間のCPU占有を防ぐ:

| SPI操作 | 待ち時間 | 待ち方式 | 理由 |
|---------|---------|---------|------|
| read | 数十μs | busy wait | コンテキストスイッチコスト（数μs）に対して短すぎる |
| prog (page write) | 数百μs〜1ms | busy wait | UIフレーム（16ms）に対して十分短い |
| erase (sector) | 数ms〜数十ms | DMA + WaitEvent | Blocked化してControlTaskにCPUを渡す |

erase待ち中のSystemTaskはBlocked状態（CPU解放）。ControlTaskがUIアニメーション等を継続できる。
littlefsのerase()コールバック内でWaitEventしても、内部状態はスタック上に保持されるため問題ない。

全体の流れ:

```
ControlTask: fs::write() → SVC → 要求キュー → 即return → UIアニメーション継続
SystemTask:  要求受信 → littlefs → BlockDevice::erase()
  → SPI eraseコマンド発行 → WaitEvent(SpiComplete) → Blocked
ControlTask: CPU取得 → UIアニメーション更新
SPI完了IRQ: notify(SystemTask, SpiComplete)
SystemTask:  Resume → littlefs続行 → 完了 → notify(ControlTask, FS)
```

---

## 8. UMI-UNIT（将来設計）

### 概要

UMI-UNIT (UMIU) は、実行中のアプリケーションにDSP処理ユニットを動的に差し込む仕組み。RAMリロケーション方式を採用する。

### ユニット形式: .umiu

| 項目 | 内容 |
|------|------|
| フォーマット | リロケータブルELF（位置依存コード） |
| ビルド | `arm-none-eabi-gcc → ELF → elf2umiu → .umiu` |
| 保存先 | Flash FS上のファイル |
| 実行場所 | RAM（ロード時にリロケーション） |

### ロード手順

```
1. Flash FSから.umiuを読み出し
2. RAM実行領域にコピー
3. リロケーションテーブルに従いアドレス解決
4. process()関数ポインタをアプリに返却
5. アプリはprocess()を直接呼び出し（BLX命令）
```

### ユニット境界の設計原則

**ユニットは完全に自己完結する。** 境界をまたぐAPI呼び出しは行わない。

- **入力**: バッファポインタ + パラメータ（関数引数として渡す）
- **出力**: バッファへの書き込み（戻り値不要）
- **内部**: sin/cos、フィルタ係数計算等のDSP処理はすべてユニット内にインライン展開
- **公開関数**: `process()` のみ（+ 必要に応じて `init()` / `teardown()`）

ユニット境界をまたぐ関数呼び出しはインライン展開が効かないため、DSP処理では避ける。
ユニット内部のコードは通常のコンパイラ最適化（インライン展開、SIMD化等）が完全に適用される。

### fPICを採用しない理由

| 方式 | ロード時コスト | 実行時コスト | 備考 |
|------|---------------|-------------|------|
| **RAMリロケーション** | リロケーション処理（数百μs） | **ゼロ** | 直接アドレスでアクセス |
| fPIC (GOT/PLT) | 不要 | **全グローバルアクセスにGOT間接参照** | DSPでは許容しがたいオーバーヘッド |

RAMリロケーション方式はロード時に一度だけコストを払い、実行時は位置固定コードと同等の性能を得る。
umiappの既存リロケーションロジックを再利用できるため、実装コストも低い。

### メモリレイアウト

```
Flash FS:
  /modules/osc_saw.umiu
  /modules/fx_delay.umiu
  ...

RAM:
  [Module execution area]  ← .umiuをロード・リロケーション
  [Module data area]       ← .bss / .data
```

### logue-SDKとの比較

| 項目 | logue-SDK | UMI-UNIT |
|------|-----------|----------|
| コード位置 | fPIC（位置独立） | RAM（リロケーション済み） |
| 実行時オーバーヘッド | GOT間接参照あり | なし |
| API依存 | SDK APIをPLT経由で呼び出し | なし（自己完結） |
| ユニットサイズ | GOT/PLTセクション分増加 | リロケーションテーブル分増加 |
| ロード手順 | RAMコピーのみ | RAMコピー + リロケーション |
