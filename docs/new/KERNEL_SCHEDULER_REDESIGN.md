# Kernel Scheduler リファクタリング計画

## 問題分析

### 根本的バグ: wait_block() のpriority starvation

`wait_block()` (umi_kernel.hh:861) のpeek fast pathがフラグ消費後にBlockedへ遷移しないため、高頻度notifyされるタスク（audio_task）が永続的にReady/Runningになり、下位優先度タスクが実行されない。

```cpp
// 現行の問題コード
uint32_t wait_block(TaskId id, uint32_t mask) {
    auto bits = notifications.peek(id, mask);  // ロックなし
    if (bits != 0) {
        return notifications.take(id, mask);   // 消費して即return → Blocked遷移なし
    }
    // ここに来る時だけBlocked化される
    ...
}
```

時間軸:
```
0.0ms  DMA TC #1 → notify(AudioReady)
0.0ms  wait_block() → peek() = true → 即return（Blockedにならない）
0.9ms  処理完了 → wait_block() → peek() = true ← DMA TC #2が既に到着
...永遠にBlockedにならない → 下位タスクがスケジュールされない
```

### 現行の対症療法

kernel.cc で `process_server_work()` を audio_task 内にinline実行。動作するが構造的に不健全。

## 修正方針

### 1. wait_block() バグ修正（最重要）

クリティカルセクション内でatomicにtake+block判定:

```cpp
uint32_t wait_block(TaskId id, uint32_t mask) {
    {
        MaskedCritical<HW> guard;
        auto bits = notifications.take(id, mask);
        if (bits != 0) return bits;  // フラグ消費済みで即return
        // フラグなし → Blocked化
        notifications.set_wait_mask(id, mask);
        tasks[id.value].state = State::Blocked;
        schedule();
    }
    // wakeup後に再take
    return notifications.take(id, mask);
}
```

これにより:
- take() がatomicにフラグを消費するため、次のwait_blockではフラグがない
- フラグなし時のみBlocked遷移 → 下位タスクに実行機会が回る
- notify() 側は既にBlocked時のみwakeするのでレースなし

### 2. kernel.cc 修正

wait_block() 修正後、audio_task は正しく Blocked になるため:
- `process_server_work()` を audio_task から削除
- `process_server_work()` 関数自体を削除
- system_task が正常にスケジュールされ、既存の system_task_entry ループがそのまま動作

### 3. API整理（削除対象）

| 削除 | 理由 |
|------|------|
| SharedRegionId, register_shared, get_shared, shared_region | kernel.ccで未使用、OS層の責務 |
| configure_mpu, Region | kernel.ccで未使用、OS層の責務 |
| CrashDump, panic, load_last_crash | kernel.ccで未使用、OS層の責務 |
| deprecated API (set_task_ready, Audio alias, set_uses_fpu, on_timer_irq(usec)) | 整理 |
| no_schedule変種 (resume_task_no_schedule, suspend_task_no_schedule) | wait_block修正後不要 |
| FpuPolicy::Exclusive, set_fpu_owner, is_fpu_owner, get_fpu_owner | 未使用 |
| Tickless API (set_audio_active, is_audio_active, get_next_wakeup) | 未使用 |

### 4. API保持

| 保持 | 用途 |
|------|------|
| create_task / resume_task / suspend_task / delete_task | タスク管理 |
| notify / wait_block / wait | イベント通知 |
| tick / on_timer_irq() | 時刻更新 |
| get_next_task / prepare_switch | コンテキストスイッチ |
| yield | CPU譲渡 |
| time() | 時刻取得 |
| current_task() | 実行中タスク |
| for_each_task / get_task_state_str / get_task_name / get_task_priority | シェル/デバッグ |
| call_later / TimerQueue | 将来のtickless対応 |

### 5. マルチコア対応

- `MaxCores` テンプレートパラメータ: 維持（デフォルト=1）
- `core_affinity` フィールド: 維持
- `trigger_ipi` / `current_core`: Hw APIに残す
- 実装は完成しているが実機テスト未実施（STM32F4はシングルコア）

### 6. Hw<Impl> 整理

```
保持:
  enter_critical / exit_critical     — スケジューラ必須
  request_context_switch             — PendSV
  monotonic_time_usecs               — tickless
  set_timer_absolute                 — tickless
  enter_sleep                        — idle
  trigger_ipi / current_core         — マルチコア
  cycle_count / cycles_per_usec      — LoadMonitor

削除（OS層/arch層に移動）:
  save_fpu / restore_fpu             → arch層（PendSV内で直接処理）
  mute_audio_dma                     → OS層
  write_backup_ram / read_backup_ram → OS層
  configure_mpu_region               → OS層
  cache_clean / invalidate / clean_invalidate → OS層
  system_reset                       → OS層
  start_first_task                   → OS層
  watchdog_init / watchdog_feed      → OS層
```

## 修正ファイル

1. `lib/umios/kernel/umi_kernel.hh` — リファクタリング本体
2. `examples/stm32f4_kernel/src/kernel.cc` — process_server_work削除、Stm32F4Hw簡素化
3. `tests/test_kernel.cc` — テスト更新（削除APIのテスト除去、wait_blockテスト追加）
4. `docs/new/STM32F4_KERNEL_FLOW.md` — ドキュメント更新

## 検証手順

1. `xmake build test_kernel && xmake run test_kernel` — ホストテスト通過
2. `xmake build stm32f4_kernel` — ビルド成功
3. `xmake flash-kernel` — フラッシュ
4. pyOCDデバッガで確認:
   - system_task の TCB レジスタが初期化マジック値でない（実行された）
   - g_pdm_ready == 0（PDM処理されている）
   - pcm_buf にデータあり
5. ユーザー確認: Audio OUT + Mic IN 両方グリッチなし

## 期待される効果

- audio_task が正しく Blocked/Ready を遷移し、system_task/control_task に実行機会が回る
- process_server_work() ワークアラウンドが不要になる
- umi_kernel.hh が〜700行程度にコンパクト化
- スケジューラとOS固有機能の責務分離が明確になる
