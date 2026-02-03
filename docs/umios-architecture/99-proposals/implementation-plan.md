# umios-architecture 仕様実装計画

## 概要

`docs/umios-architecture/` の全仕様に対して、未実装項目を実装する。
ドキュメントが正（実装をドキュメントに合わせる）。

## 完了済み作業

### ドキュメント ↔ 実装の整合性修正

以下は実装に合わせてドキュメントを修正済み:

- 01-audio-context.md: Event 構造体を 8B → 24B（union 型）に更新、EventType を UPPER_CASE に
- 03-event-system.md: AudioEventQueue サイズを 24B × 64 = 1536B に更新
- 05-midi.md: UMP32 メンバ名 `data` → `word`
- 03-port/06-syscall.md: イベントフラグを lower_case に（constexpr 変数）
- 10-shared-memory.md: event_queue サイズ 512B → 1536B
- 11-scheduler.md: イベントフラグを lower_case に
- 17-shell.md: KernelStateView を 17 フィールド → 33 フィールドに拡張
- 20-diagnostics.md: ErrorLedPattern に NONE=0 追加、UPPER_CASE に

### コード修正

- **Syscall 番号統一**: `syscall_numbers.hh`, `syscall_handler.hh` を doc の sparse layout (10-group) に修正
- **イベントフラグ**: KernelEvent namespace に timer, control, shutdown 追加、全フラグ lower_case に
- **enum UPPER_CASE 移行**: 全 enum class 値を UPPER_CASE に（~30 enum、~40 ファイル）
- **xmake.lua**: BuildType::Development/Release → DEVELOPMENT/RELEASE

### ビルド検証

- `xmake build stm32f4_kernel` ✅ (Flash 4.6%, RAM 58.0%)
- `xmake build synth_app` ✅ (.umia 生成)
- `xmake run test_kernel` ✅ (88/88 passed)

---

## 実装状況サマリ

| # | ドキュメント | 実装率 | 主な未実装項目 | 備考 |
|---|------------|--------|--------------|------|
| 05 | midi | 60% | MidiInput/Output concept, SysExAssembler, hw_timestamp_to_sample_pos | |
| 07 | memory | 90% | SharedMemory サブリージョンシンボルの実使用 | |
| 12 | memory-protection | 85% | MemoryUsage 構造体、protection.hh ファイル未実装 | |
| 13 | system-services | 70% | SystemTask のクラス化 | |
| 14 | security | 80% | SHA-256 を umios/crypto/ に移動 | |
| 17 | shell | 95% | 軽微な差分のみ | |
| 18 | updater | 70% | OS-side Updater クラス | |
| 19 | storage-service | 30% | BlockDevice 実装クラス、FS コア、syscall 実装 | concept + StorageService テンプレートは定義済み |
| 20 | diagnostics | 85% | fault_handler.hh の統合（定義済みだが未使用） | |

---

## 既存コードの使用状況

実装済みコードがカーネル/アプリで実際に使われているかの監査結果。

### ✅ 使用されている

| コード | 使用箇所 |
|--------|---------|
| `syscall_numbers.hh` | platform/syscall.hh（アプリ側 SVC ラッパー）, svc_handler.hh（カーネル SVCハンドラ） |
| `syscall_handler.hh` の `app_syscall` | kernel.cc svc_handler_impl の switch 文で全 syscall 参照 |
| `KernelEvent::audio / AudioReady` | kernel.cc（signal/wait_block）, test_kernel.cc |
| `mpu_config.hh` | main.cc（MPU リージョン設定）, loader.cc（configure_mpu） |
| `app_header.hh` (AppHeader, LoadResult, BuildType) | loader.cc（バリデーション、CRC 検証、署名検証） |

### ⚠️ 定義のみ（型制約やテンプレートとして参照されるが、実装クラス/呼び出し元がない）

| コード | 状況 | 必要なアクション |
|--------|------|----------------|
| `BlockDeviceLike` concept | StorageService, littlefs, fatfs のテンプレート制約として参照。**具体的な Flash/SD 実装クラスなし** | Phase 7 で実装クラスを追加 |
| `StorageService` テンプレート | 定義済みだがインスタンス化されていない | Phase 7 で kernel.cc に統合 |
| `KernelEvent::midi, timer` | `syscall::event::midi/timer` として kernel.cc で使用されているが、`KernelEvent::midi/timer` 自体は未参照。**namespace 二重化** | KernelEvent を syscall::event に統合するか、kernel.cc 側の参照を KernelEvent に変更 |
| `KernelEvent::vsync, control, shutdown` | **完全未使用** — 発火する ISR/タスクが存在しない | 将来の VSyncドライバ、コントロール入力、シャットダウンシーケンスで使用予定 |

### ❌ 未使用（定義のみ、include も呼び出しもなし）

| コード | 状況 | 必要なアクション |
|--------|------|----------------|
| `fault_handler.hh` 全体 | FaultLog, ErrorLedPattern, classify_fault, record_fault, process_pending_fault — **どこからも include されていない** | Phase 2/3 で kernel.cc の HardFault ハンドラに統合 |
| `protection.hh` | **ファイル自体が未実装**（docs に記載あり） | Phase 3 で実装 |

---

## 実装ステップ

### Phase 1: MIDI インフラ (05-midi.md)

既存: `lib/umidi/` に Parser/SysExBuffer あり。kernel.cc に USB コールバック直接実装。

#### Step 1-1: MidiInput / MidiOutput concept

**ファイル:** `lib/umidi/include/core/transport.hh` (新規)

```cpp
namespace umidi {

template <typename T>
concept MidiInput = requires(T& t) {
    { t.poll() } -> std::same_as<void>;
    { t.is_connected() } -> std::convertible_to<bool>;
};

template <typename T>
concept MidiOutput = requires(T& t, const UMP32& ump) {
    { t.send(ump) } -> std::convertible_to<bool>;
    { t.is_connected() } -> std::convertible_to<bool>;
};

} // namespace umidi
```

#### Step 1-2: SysExAssembler

**ファイル:** `lib/umidi/include/core/sysex_assembler.hh` (新規)

仕様通りの SysExAssembler 構造体。既存 SysExBuffer とは別（SysExBuffer はリングバッファ、Assembler は単一メッセージ組み立て）。

```cpp
struct SysExAssembler {
    uint8_t buffer[256];
    uint16_t length = 0;
    bool complete = false;
    void feed(const UMP32& ump);
    bool is_complete() const;
    std::span<const uint8_t> data() const;
    void reset();
};
```

#### Step 1-3: UsbMidiInput / UartMidiInput アダプタ

**ファイル:** `lib/umios/backend/cm/usb_midi_input.hh` (新規), `lib/umios/backend/cm/uart_midi_input.hh` (新規)

既存の kernel.cc USB コールバックを MidiInput concept に適合するクラスとして抽出。

#### Step 1-4: hw_timestamp_to_sample_pos

**ファイル:** `lib/umidi/include/core/timestamp.hh` (新規)

```cpp
uint16_t hw_timestamp_to_sample_pos(
    uint64_t event_time_us, uint64_t block_start_us,
    uint32_t sample_rate, uint32_t buffer_size);
```

kernel.cc の既存インライン計算をこの関数に置換。

---

### Phase 2: Diagnostics (20-diagnostics.md)

既存: KernelMetrics 実装済み (metrics.hh)、StackMonitor/HeapMonitor 実装済み (umi_monitor.hh)、デバッグカウンタは g_dbg_* アドホック変数。
**fault_handler.hh は定義済みだが未使用**。

#### Step 2-1: fault_handler.hh を kernel.cc に統合

`fault_handler.hh` を kernel.cc から include し、既存の HardFault ハンドラで `record_fault()` を呼ぶ。
SystemTask のメインループで `process_pending_fault()` を呼び、FaultLog に記録する。

#### Step 2-2: DWT ユーティリティ

**ファイル:** `lib/umios/backend/cm/dwt.hh` (新規)

```cpp
namespace dwt {
    void enable();
    void disable();
    uint32_t cycles();
    void reset();
    bool is_available();
}
```

#### Step 2-3: ScopedCycles

**ファイル:** `lib/umios/kernel/metrics.hh` に追加

```cpp
class ScopedCycles {
    uint32_t start;
    uint32_t& dest;
public:
    ScopedCycles(uint32_t& dest);
    ~ScopedCycles();
};
```

---

### Phase 3: Memory Protection (12-memory-protection.md)

既存: MPU 設定は kernel.cc + mpu_config.hh に実装済み。StackMonitor/HeapMonitor 実装済み。

#### Step 3-1: MemoryUsage 構造体

**ファイル:** `lib/umios/kernel/umi_monitor.hh` に追加

```cpp
struct MemoryUsage {
    size_t heap_used;
    size_t heap_peak;
    size_t stack_used;
    size_t stack_peak;
};
```

HeapMonitor と StackMonitor から集約するユーティリティ関数も追加。

#### Step 3-2: protection.hh の実装

**ファイル:** `lib/umios/kernel/protection.hh` (新規)

```cpp
enum class ProtectionMode { FULL, PRIVILEGED, PRIVILEGED_WITH_MPU };
template <class HW, ProtectionMode Mode = ProtectionMode::FULL>
class Protection {
    static constexpr bool uses_mpu();
    static constexpr bool needs_syscall();
};
```

---

### Phase 4: Security (14-security.md)

既存: SHA-512 + Ed25519 は `lib/umios/crypto/`。SHA-256 は `lib/umiboot/include/umiboot/auth.hh` 内にソフトウェア実装あり。CRC32 は umiboot に実装済み。

#### Step 4-1: SHA-256 を umios/crypto/ に移動

**ファイル:** `lib/umios/crypto/sha256.hh` (新規), `lib/umios/crypto/sha256.cc` (新規)

umiboot/auth.hh 内の `detail::Sha256` を独立モジュールとして抽出。umiboot 側は umios/crypto/sha256.hh を include するように変更。

---

### Phase 5: System Services (13-system-services.md)

既存: `system_task_entry()` 関数が kernel.cc に実装。ShellCommands, StandardIO 実装済み。

#### Step 5-1: SystemTask イベントディスパッチの形式化

**ファイル:** `lib/umios/kernel/system_task.hh` (新規)

```cpp
template <class HW, class Kernel>
class SystemTask {
    void run();  // メインループ: wait_block → dispatch
private:
    void dispatch_sysex();
    void dispatch_fs();
    void dispatch_fault();
    void dispatch_tick();
};
```

kernel.cc の `system_task_entry()` をこのクラスのインスタンス化に置換。

---

### Phase 6: Updater (18-updater.md)

既存: umiboot に BootloaderInterface (A/B partition, rollback, commit) が完全実装。DFU SysEx プロトコル定義済み。OS-side Updater クラスが未実装。

#### Step 6-1: Updater クラス

**ファイル:** `lib/umios/kernel/updater.hh` (新規)

- DFU コマンドハンドラ (FW_QUERY, FW_BEGIN, FW_DATA, FW_VERIFY, FW_COMMIT, FW_ROLLBACK, FW_REBOOT)
- umiboot::BootloaderInterface をバックエンドとして使用
- 7-bit エンコード/デコード
- SystemMode 列挙型（既存 shell_commands.hh から移動）

#### Step 6-2: kernel.cc 統合

既存の DFU 処理コード（あれば）を Updater クラスに委譲。

---

### Phase 7: Storage Service (19-storage-service.md)

BlockDeviceLike concept と StorageService テンプレートは定義済み。**実装クラスとインスタンス化が未実装。**

#### Step 7-1: BlockDevice 実装クラス

- `lib/umios/backend/cm/internal_flash.hh` — STM32F4 内蔵 Flash
- `lib/umios/backend/cm/spi_flash.hh` — W25Qxx SPI Flash
- `lib/umios/backend/cm/sd_spi.hh` — SD カード (SPI モード)

#### Step 7-2: FS Syscall ハンドラ

**ファイル:** kernel.cc の svc_handler_impl に case 60-68 追加

リクエストを StorageService キューに投入、notify(FS) で応答。

#### Step 7-3: littlefs 移植実装 ✅ 実装済み

`.refs/littlefs/` の lfs.c (6549行) を C++23 に忠実移植。COW、CTZスキップリスト、メタデータペアのアルゴリズムはそのまま維持。LFS_NO_MALLOC（ユーザ提供バッファ必須）、LFS_MIGRATE 除外。

**ファイル:** `lib/umios/fs/littlefs/`
- `lfs_types.hh` — enum class (LfsError, LfsType, LfsOpenFlags, LfsWhence)、構造体定義
- `lfs_util.hh` — CRC32テーブル、ビット演算、エンディアン変換ユーティリティ
- `lfs_config.hh` — LfsConfig 構造体 + `make_lfs_config<T>()` テンプレート
- `lfs.hh` — Lfs クラス宣言（format/mount/file_*/dir_* 等の全API）
- `lfs.cc` — ~6164行の忠実な C++23 移植

**テスト:** `tests/test_littlefs.cc` — RAMブロックデバイス上で 25/25 チェック通過

**参照元:** `.refs/littlefs/` (git clone)

#### Step 7-4: FATfs 移植実装 ✅ 実装済み

`.refs/fatfs/source/` の ff.c (7249行) を C++23 に移植。FAT12/16/32 対応、exFAT 除外 (FF_FS_EXFAT=0)。LFN対応 (FF_USE_LFN=1、BSS上スタティックバッファ)、CP437のみ。

**ファイル:** `lib/umios/fs/fatfs/`
- `ff_types.hh` — FatResult enum class、FatFsVolume/FatFile/FatDir/FatFileInfo 構造体
- `ff_config.hh` — `namespace umi::fs::fat::config` 内の constexpr 設定値
- `ff_diskio.hh` — DiskIo 構造体 + `make_diskio<T>()` テンプレート
- `ff_unicode.hh` / `ff_unicode.cc` — CP437 OEM⇔Unicode 変換テーブル
- `ff.hh` — FatFs クラス宣言（mount/open/read/write/mkdir/unlink 等）
- `ff.cc` — ~2300行の忠実な C++23 移植

**テスト:** `tests/test_fatfs.cc` — RAMブロックデバイス上で FAT16イメージを手動作成、18/18 チェック通過

**参照元:** `.refs/fatfs/` (元コード配置)

---

### Phase 8: SharedMemory サブリージョン (03-port/07-memory.md)

#### Step 8-1: リンカシンボルの実使用

kernel.cc の `SharedMemory g_shared` を、リンカシンボル `_shared_audio_start` 等を使って配置するように変更。現在は `.shared` セクション一括配置。

SharedMemory 構造体のレイアウトがサブリージョン境界と一致することを static_assert で検証。

---

### 要検討: KernelEvent namespace 二重化の解消

現状 `KernelEvent` と `syscall::event` の2箇所で同じイベントフラグを定義している。

- **kernel.cc** は `syscall::event::midi`, `syscall::event::timer` を使用
- **KernelEvent::midi**, **KernelEvent::timer** は直接参照なし
- **KernelEvent::AudioReady** のみ kernel.cc と test_kernel.cc で使用

方針候補:
1. KernelEvent を廃止し、syscall::event に統一
2. KernelEvent を正として kernel.cc の参照を変更
3. 現状維持（カーネル内部用と ABI 用で分離）

---

## 実装順序と依存関係

```
Phase 1 (MIDI)          ← 依存なし
Phase 2 (Diagnostics)   ← 依存なし（fault_handler.hh の統合）
Phase 3 (MemProtection) ← Phase 2 (FaultLog)
Phase 4 (Security)      ← 依存なし
Phase 5 (SystemServices)← Phase 2, 3
Phase 6 (Updater)       ← Phase 4 (SHA-256), Phase 5
Phase 7 (Storage)       ← Phase 5 (SystemTask) + BlockDevice 実装クラス
Phase 8 (Memory)        ← 依存なし
```

Phase 1, 2, 4, 8 は並行可能。

## 検証

各 Phase 完了時:
1. `xmake build stm32f4_kernel && xmake build synth_app` — ビルド成功
2. 実機フラッシュ (`xmake flash-kernel && xmake flash-synth-app`)
3. デバッガで syscall 動作確認（GDB/pyOCD で変更箇所のブレークポイント検証）

Phase 7 (Storage) はさらに Flash/SD の実機 I/O 確認が必要。

## 注意事項

- 既存の kernel.cc の動作を壊す変更は段階的に移行する
- 新規ファイルはヘッダオンリー（テンプレート）を基本とし、.cc が必要な場合のみ分離
- Phase 7 の FS 移植は規模が大きいため、BlockDevice 実装クラスと syscall ハンドラを先行し、FS コア実装は後続
