# umios-architecture 仕様実装計画

## 概要

`docs/umios-architecture/` の全仕様に対して、未実装項目を実装する。
ドキュメントが正（実装をドキュメントに合わせる）。

## 実装状況サマリ

| # | ドキュメント | 実装率 | 主な未実装項目 |
|---|------------|--------|--------------|
| 05 | midi | 60% | MidiInput/Output concept, UsbMidiInput/UartMidiInput, SysExAssembler, hw_timestamp_to_sample_pos |
| 07 | memory | 90% | SharedMemory サブリージョンシンボルの実使用 |
| 12 | memory-protection | 85% | MemoryUsage 構造体、process_pending_fault() の形式化 |
| 13 | system-services | 70% | SystemTask のクラス化・イベントディスパッチ形式化 |
| 14 | security | 80% | SHA-256 を umios/crypto/ に移動 |
| 17 | shell | 95% | 軽微な差分のみ |
| 18 | updater | 70% | OS-side Updater クラス（umiboot に基盤あり） |
| 19 | storage-service | 0% | StorageService, BlockDevice, littlefs/FATfs 全て |
| 20 | diagnostics | 85% | FaultLog クラス、ErrorLedPattern、ScopedCycles |

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

#### Step 2-1: FaultLog クラス

**ファイル:** `lib/umios/kernel/fault_log.hh` (新規)

```cpp
struct FaultLogEntry {
    backend::cm::FaultInfo info;
    uint32_t timestamp_ms;
};

template <size_t N = 8>
class FaultLog {
    void push(const FaultLogEntry& entry);
    const FaultLogEntry* get(size_t idx) const;
    const FaultLogEntry* latest() const;
    void clear();
    size_t count() const;
};
```

グローバル `g_fault_log`, `g_fault_pending`, `g_pending_fault` を追加。

#### Step 2-2: ErrorLedPattern + classify_fault

**ファイル:** `lib/umios/kernel/fault_log.hh` に追加

```cpp
enum class ErrorLedPattern { StackOverflow, AccessViolation, InvalidInstruction, BusFault, Unknown };
ErrorLedPattern classify_fault(const backend::cm::FaultInfo& info);
```

#### Step 2-3: DWT ユーティリティ

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

#### Step 2-4: ScopedCycles

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

既存: MPU 設定は kernel.cc に実装済み。StackMonitor/HeapMonitor 実装済み。

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

#### Step 3-2: Fault 処理の形式化

**ファイル:** `lib/umios/kernel/fault_handler.hh` (新規)

- `record_fault()`: ISR 内で FaultInfo を保存、`g_fault_pending = true`
- `process_pending_fault()`: SystemTask から呼び出し、FaultLog に記録、アプリ terminate

kernel.cc の既存 HardFault ハンドラからロジックを抽出。

#### Step 3-3: ProtectionMode テンプレート

**ファイル:** `lib/umios/kernel/protection.hh` (新規)

```cpp
enum class ProtectionMode { Full, Privileged, PrivilegedWithMpu };
template <class HW, ProtectionMode Mode = ProtectionMode::Full>
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

完全に新規実装。ハードウェア依存（Flash/SD）が大きいため、抽象層のみ先行実装。

#### Step 7-1: BlockDevice インターフェース

**ファイル:** `lib/umios/kernel/block_device.hh` (新規)

```cpp
struct BlockDevice {
    virtual int read(uint32_t block, uint32_t offset, void* buf, uint32_t size) = 0;
    virtual int write(uint32_t block, uint32_t offset, const void* buf, uint32_t size) = 0;
    virtual int erase(uint32_t block) = 0;
    virtual uint32_t block_size() = 0;
    virtual uint32_t block_count() = 0;
    virtual ~BlockDevice() = default;
};
```

注: 組み込みでの vtable 使用だが、StorageService は非リアルタイムパスのため許容。

#### Step 7-2: StorageService クラス

**ファイル:** `lib/umios/kernel/storage_service.hh` (新規)

- 非同期リクエストキュー
- FS マウントポイント管理 (`/flash/` → littlefs, `/sd/` → FATfs)
- ファイルディスクリプタ管理（アプリごと最大4個）

#### Step 7-3: FS Syscall ハンドラ

**ファイル:** kernel.cc の svc_handler_impl に case 60-68 追加

リクエストを StorageService キューに投入、notify(FS) で応答。

#### Step 7-4: littlefs 移植実装

`.refs/` に littlefs 元リポジトリをクローンし、参照しながらプロジェクトスタイル（C++23、命名規則、エラー処理）に合わせて移植実装する。ラッパーではなく、コードをこちらの規約で書き直す。

**ファイル:** `lib/umios/fs/littlefs/` (新規ディレクトリ)

- `lfs.hh` / `lfs.cc` — littlefs コア（COW、ウェアレベリング、電断耐性）
- `lfs_block_device.hh` — BlockDevice を実装する Flash 向けアダプタ

**参照元:** `.refs/littlefs/` (git clone)

#### Step 7-5: FATfs 移植実装

同様に `.refs/` に FATfs 元コードを配置し、参照しながら移植。

**ファイル:** `lib/umios/fs/fatfs/` (新規ディレクトリ)

- `ff.hh` / `ff.cc` — FAT32 コア
- `fatfs_block_device.hh` — BlockDevice を実装する SD カード向けアダプタ

**参照元:** `.refs/fatfs/` (元コード配置)

---

### Phase 8: SharedMemory サブリージョン (07-memory.md)

#### Step 8-1: リンカシンボルの実使用

kernel.cc の `SharedMemory g_shared` を、リンカシンボル `_shared_audio_start` 等を使って配置するように変更。現在は `.shared` セクション一括配置。

```cpp
extern "C" {
    extern uint8_t _shared_audio_start[];
    extern uint8_t _shared_midi_start[];
    extern uint8_t _shared_hwstate_start[];
}
```

SharedMemory 構造体のレイアウトがサブリージョン境界と一致することを static_assert で検証。

---

## 実装順序と依存関係

```
Phase 1 (MIDI)          ← 依存なし
Phase 2 (Diagnostics)   ← 依存なし
Phase 3 (MemProtection) ← Phase 2 (FaultLog)
Phase 4 (Security)      ← 依存なし
Phase 5 (SystemServices)← Phase 2, 3
Phase 6 (Updater)       ← Phase 4 (SHA-256), Phase 5
Phase 7 (Storage)       ← Phase 5 (SystemTask)
Phase 8 (Memory)        ← 依存なし
```

Phase 1, 2, 4, 8 は並行可能。

## 検証

各 Phase 完了時:
1. `xmake build stm32f4_kernel && xmake build synth_app` — ビルド成功
2. 実機フラッシュ (`xmake flash-kernel && xmake flash-synth-app`)
3. デバッガで syscall 動作確認（GDB/pyOCD で変更箇所のブレークポイント検証）

Phase 7 (Storage) はさらに Flash/SD の実機 I/O 確認が必要。

## 前提: 解決済みのドキュメント修正

以下はドキュメント側を実装に合わせて修正済み（コミット 16c9818）:

- 01-audio-context.md: output_events は `EventQueue<>&`、shared state はポインタ型、`input_state` 名を維持
- 04-param-system.md / 10-shared-memory.md: SharedParamState サイズ 164B → 136B に修正

## 注意事項

- 既存の kernel.cc の動作を壊す変更は段階的に移行する
- 新規ファイルはヘッダオンリー（テンプレート）を基本とし、.cc が必要な場合のみ分離
- Phase 7 の FS 移植は規模が大きいため、BlockDevice 抽象層と syscall ハンドラを先行し、FS コア実装は後続
