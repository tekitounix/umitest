# UMI Kernel 仕様

**規範レベル:** MUST/SHALL/REQUIRED, SHOULD/RECOMMENDED, MAY/NOTE/EXAMPLE
**対象読者:** Kernel Dev / Porting / App Dev
**適用範囲:** UMI-OS Kernel 共通仕様

---

## 1. 目的・スコープ
本書は、UMI-OS Kernel の**実行モデル・タスクモデル・スケジューリング・例外/割り込み・タスク通知/イベント**の規範を定義する。

---

## 2. 実行モデル
- OS とアプリは**完全に分離されたバイナリ**として扱う。
- アプリは**非特権モード**で実行され、特権操作は `syscall` 経由で行う。
	- **MPU 非搭載ターゲット（例: Cortex-M0）**では特権分離は実施できないため、
		同じ特権レベルで動作するが、API 経路は `syscall` に統一する。
- OS はアプリの Fault から**生存**できなければならない。
	- **MPU 非搭載ターゲット**では完全な隔離は不可能なため、
		「OS 生存」は**ベストエフォート**とする。

**注:** 本システムは **一定の余裕がある MCU** を前提とするため、
実用上は Cortex-M0 などのエントリークラスは対象外とする。

---

## 3. タスクモデル
### 3.1 タスク種別と優先度
- Kernel は最低限、以下の4タスクを持つ。

| タスク | 優先度 | 役割 | 備考 |
|---|---:|---|---|
| AudioTask | 0 | オーディオ処理 | ハードリアルタイム
| SystemTask | 1 | OSサービス | SysEx/FS/Driver 等
| ControlTask | 2 | アプリ主処理 | `main()` 実行
| IdleTask | 3 | 省電力 | `WFI` で待機

### 3.2 役割分離の規範
- **AudioTask** は System/Control によって遅延してはならない。
- **SystemTask** は OS 生存保証のため ControlTask より高優先度でなければならない。

### 3.3 FPU ポリシー
- タスクは `uses_fpu` を宣言し、Kernel がポリシーを自動決定する。
- FPU の save/restore は **Kernel 実装に委ねる**。

**実装抜粋（Cortex-M4）**
FPU 使用有無はタスク作成時の `uses_fpu` により決定し、
**FPU レジスタのスタック退避有無**を EXC_RETURN で切り替える。
退避方式（lazy stacking など）は **MCU/ポート実装依存**である。

```cpp
// lib/umios/kernel/port/cm4/context.hh
*(--stack_top) = uses_fpu ? exc_return::THREAD_PSP_EXTENDED
                          : exc_return::THREAD_PSP_BASIC;
```

FPU の詳細動作はポート実装に従う（例: [lib/umios/kernel/port/cm4/context.hh](lib/umios/kernel/port/cm4/context.hh)）。

---

## 4. スケジューリング
- Kernel は **O(1) ビットマップ方式**で Ready タスクを選択する。
- `PendSV` を用いてコンテキストスイッチを行う。
- ブロック待ち (`wait_block`) は**不可分にイベント消費とブロック遷移**を行う。

**実装抜粋（O(1) ビットマップ選択）**
```cpp
// lib/umios/kernel/umi_kernel.hh
auto bitmap = ready_bitmap_.load(std::memory_order_acquire);
if (bitmap == 0) {
    return std::nullopt;
}
auto highest_prio = static_cast<std::size_t>(__builtin_ctz(bitmap));
```

**実装抜粋（wait_block の原子性）**
```cpp
// lib/umios/kernel/umi_kernel.hh
std::uint32_t wait_block(TaskId id, std::uint32_t mask) {
	{
		MaskedCritical<HW> guard;
		auto bits = notifications.take(id, mask);
		if (bits != 0) {
			return bits;
		}
		if (valid_task(id)) {
			notifications.set_wait_mask(id, mask);
			set_blocked_with_bitmap(id);
			schedule();
		}
	}
	return notifications.take(id, mask);
}
```

---

## 5. 例外/割り込み
- `SysTick` は時間管理とタスク起床のために使用する。
- `SVC` は syscall の入口として使用する。
- `PendSV` はコンテキストスイッチ専用とする。
- クリティカルセクションは **BASEPRI** により実装する。

**実装抜粋（クリティカルセクション）**
```cpp
// lib/umios/kernel/umi_kernel.hh
template <class HW>
class MaskedCritical {
public:
	MaskedCritical() { HW::enter_critical(); }
	~MaskedCritical() { HW::exit_critical(); }
	MaskedCritical(const MaskedCritical&) = delete;
	MaskedCritical& operator=(const MaskedCritical&) = delete;
};
```

---

## 6. タスク通知/イベント
- ISR → Task 通知は `notify()` / `wait_block()` を用いる。
- Task 間通信は **ロックフリー SPSC** を原則とする。
- イベントフラグは **ビットマスク**で表現する。

**実装抜粋（イベントフラグ定義）**
```cpp
// lib/umios/kernel/umi_kernel.hh
namespace KernelEvent {
    constexpr std::uint32_t AudioReady = 1 << 0;
    constexpr std::uint32_t MidiReady  = 1 << 1;
    constexpr std::uint32_t VSync      = 1 << 2;
}
```

**実装抜粋（SPSC キュー）**
```cpp
// lib/umios/kernel/umi_kernel.hh
template <typename T, std::size_t Capacity>
class SpscQueue {
public:
	bool try_push(const T& item) {
		const std::size_t write = write_pos_.load(std::memory_order_relaxed);
		const std::size_t read = read_pos_.load(std::memory_order_acquire);
		if (((write + 1) & mask()) == read) {
			return false;
		}
		buffer_[write] = item;
		write_pos_.store((write + 1) & mask(), std::memory_order_release);
		return true;
	}

	std::optional<T> try_pop() {
		const std::size_t read = read_pos_.load(std::memory_order_relaxed);
		const std::size_t write = write_pos_.load(std::memory_order_acquire);
		if (read == write) {
			return std::nullopt;
		}
		T item = buffer_[read];
		read_pos_.store((read + 1) & mask(), std::memory_order_release);
		return item;
	}
};
```

---

## 7. 共有メモリ（要約）
- SharedMemory は **ゼロオーバーヘッド**のデータ共有手段である。
- 共有メモリの詳細仕様は **メモリ保護仕様**および **アプリ規格仕様**に従う。

---

## 8. 互換性・ABI 方針
- ABI 互換性は `abi_version` で判断する。
- **メジャー不一致は非互換**とし、ロードを拒否する。
