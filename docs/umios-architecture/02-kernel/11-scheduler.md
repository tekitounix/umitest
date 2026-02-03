# 11 — スケジューラと RT-Kernel

## 概要

タスクスケジューリング、コンテキストスイッチ、優先度管理、タスク通知、タイマー。
本章は組み込みターゲット（Cortex-M）の RT-Kernel 仕様を規定する。

WASM / Plugin ターゲットではホストが直接 `process()` を呼ぶため、本章のスケジューラは不要。

| 項目 | 状態 |
|------|------|
| タスクモデル（4 優先度） | 実装済み |
| O(1) ビットマップスケジューラ | 実装済み |
| コンテキストスイッチ（PendSV） | 実装済み |
| notify / wait_block | 実装済み |
| FPU ポリシー自動決定 | 実装済み |
| Tickless 電力管理 | 新設計 |
| パフォーマンス計測（DWT） | 実装済み |

---

## 実行モデル

- OS とアプリは**完全に分離されたバイナリ**として扱う
- アプリは**非特権モード**で実行され、特権操作は syscall（SVC）経由で行う
- OS はアプリの Fault から**生存**できなければならない
- MPU 非搭載ターゲットでは特権分離は実施できないが、API 経路は syscall に統一する

## タスク優先度と実行モデル

Kernel は 4 タスクの固定優先度プリエンプティブスケジューラを持つ。

| 優先度 | タスク | 役割 | FPU | 備考 |
|-------:|--------|------|-----|------|
| 0 | AudioTask | DMA 通知 → `process()` 呼び出し | 使用 | ハードリアルタイム |
| 1 | SystemTask | MIDI ルーティング、SysEx、シェル、FS、Driver | 使用可 | OS サービス群 |
| 2 | ControlTask | アプリ `main()` 実行 | 使用 | 非特権モード（MPU 隔離） |
| 3 | IdleTask | WFI スリープ | 不使用 | 全タスク Blocked 時のみ実行 |

### 役割分離の規範

- **AudioTask** は System/Control によって遅延してはならない（最高優先度で保証）
- **SystemTask** は OS 生存保証のため ControlTask より高優先度でなければならない
- **IdleTask** は ready_bitmap が 0 のとき実行され、`WFI` で省電力待機する

---

## スケジューリングアルゴリズム

**O(1) ビットマップ方式**で Ready タスクを選択する。

```
ready_bitmap: bit 0 = Audio, bit 1 = System, bit 2 = Control, bit 3 = Idle
```

最高優先度タスクの選択は `CTZ`（Count Trailing Zeros）1 命令で完了する:

```cpp
auto bitmap = ready_bitmap_.load(std::memory_order_acquire);
if (bitmap == 0) return std::nullopt;
auto highest_prio = static_cast<std::size_t>(__builtin_ctz(bitmap));
```

- タスク数は固定（4）のため、動的キュー管理は不要
- `ready_bitmap_` は `std::atomic<uint8_t>` でロックフリーに更新

---

## コンテキストスイッチ

### 例外の役割分担

| 例外 | 用途 |
|------|------|
| **SVC** | Syscall の入口。アプリからの特権要求 |
| **PendSV** | コンテキストスイッチ専用。最低優先度で実行 |
| **SysTick** | 周期タイマー。タスク起床とタイムスライス管理 |

### コンテキストスイッチフロー

1. `schedule()` が呼ばれ、次に実行すべきタスクを決定
2. 現タスクと異なる場合、PendSV をペンドする
3. PendSV ハンドラで実際のレジスタ退避/復元を実行:
   - 現タスクの R4–R11（+ FPU 使用時は S16–S31）をスタックに退避
   - PSP（Process Stack Pointer）を TCB に保存
   - 次タスクの PSP を TCB から復元
   - 次タスクの R4–R11（+ FPU レジスタ）をスタックから復元
   - `EXC_RETURN` 値で FPU フレームの有無を切り替え

### TCB（Task Control Block）

各タスクの TCB は以下を保持する:

- スタックポインタ（PSP）
- 優先度
- 状態（Ready / Blocked）
- FPU 使用フラグ（`uses_fpu`）
- 通知ビットマスク

### クリティカルセクション

- **BASEPRI 方式**を採用（PRIMASK 全禁止ではなく優先度ベース）
- Audio DMA 割り込みはクリティカルセクション中も実行可能
- RAII ガードで自動的に保護/解除:

```cpp
template <class HW>
class MaskedCritical {
    MaskedCritical() { HW::enter_critical(); }
    ~MaskedCritical() { HW::exit_critical(); }
};
```

---

## FPU コンテキスト退避ポリシー

タスクは `uses_fpu` フラグのみを宣言する。FPU コンテキストスイッチの方式は Kernel がコンパイル時に自動決定する（ポリシーベース設計）。

```cpp
enum class FpuPolicy : uint8_t {
    Forbidden = 0,  // FPU 使用禁止
    Exclusive = 1,  // FPU 独占（退避不要）
    LazyStack = 2,  // ハードウェア遅延保存（LSPACT）
};
```

### 自動決定ルール

| 条件 | ポリシー | 退避処理 |
|------|---------|---------|
| `uses_fpu = false` | Forbidden | なし。FPU を使うと他タスクの状態を破壊する |
| FPU 使用タスクが 1 つだけ | Exclusive | なし（独占所有のため退避不要） |
| FPU 使用タスクが複数 | LazyStack | ハードウェア Lazy Stacking（LSPACT）に委ねる |

`FpuPolicy` は Kernel 内部の実装詳細であり、タスク作成者が直接指定する必要はない。

### 実装（`fpu_policy.hh` / `context.hh`）

自動決定は `consteval` で実現される。カーネル側で `TaskFpuDecl` を宣言すると、各タスクの `FpuPolicy` がコンパイル時に確定する:

```cpp
#include <umios/kernel/fpu_policy.hh>

static constexpr umi::TaskFpuDecl fpu_decl {
    .audio   = true,
    .system  = false,
    .control = true,
    .idle    = false,
};
static constexpr int fpu_task_count = umi::count_fpu_tasks(fpu_decl);  // 2
static constexpr auto audio_fpu = umi::resolve_fpu_policy(fpu_decl.audio, fpu_task_count);
// → LazyStack（FPU 使用タスクが 2 つのため）
```

`init_task_stack` はテンプレートパラメータとして `FpuPolicy` を受け取り、`Forbidden` なら BASIC フレーム、`Exclusive`/`LazyStack` なら EXTENDED フレームで初期化する。PendSV ハンドラは従来通り `EXC_RETURN` の bit 4 で動的判定するため変更不要。

### 設計上の注意

- 実際には複数タスクが FPU を使いたい場面が多い（AudioTask の DSP 処理、EventRouter の float 演算等）
- 2 つ以上のタスクが FPU を使うなら LazyStack が必須。Forbidden で済むのは IdleTask 程度
- **FPU なしターゲット**（Cortex-M0 等）ではこのポリシー自体が無効になる。`#if __FPU_PRESENT` で条件コンパイル

### FPU なし/あり版のコード設計方針

- 基本は FPU なしでも動作するコードを書く
- FPU あり版は最適化パスとして用意（`#if __FPU_PRESENT` 等で分岐）
- DSP ライブラリ（umidsp）は float 版と固定小数点版を提供する方針
- ドキュメント内のコード例が float 前提の場合、FPU 前提であることを注記する

---

## 割り込みとタスク通知

### 通知メカニズム

ISR → Task 通知は 2 つの API で実現する:

| API | 動作 | 安全なコンテキスト |
|-----|------|-------------------|
| `notify(id, bits)` | フラグ設定 + wake 判定 + Ready 遷移 | MaskedCritical 内（BASEPRI でマスクされる ISR・タスク） |
| `signal(id, bits)` | フラグ設定のみ（`atomic::fetch_or`） | **任意の ISR**（BASEPRI を超える優先度でも安全） |

Task 間通信は**ロックフリー SPSC キュー**を原則とする。

### signal() + PendSV パターン

BASEPRI 閾値を超える ISR（Audio DMA 等）は `notify()` を使えない。`notify()` 内部で MaskedCritical を取るが、BASEPRI を超える ISR はそのクリティカルセクションを貫通してしまい、共有状態の不整合を起こすためである。

代わりに以下の 2 段階パターンを使う:

1. **ISR**: `signal(id, bits)` でフラグだけアトミックに設定し、PendSV をペンドする
2. **PendSV**: `resolve_pending()` でフラグを検査し、条件を満たす Blocked タスクを Ready に遷移させる

```
DMA ISR (優先度 0x00, BASEPRI 非マスク)
  │
  ├── signal(audio_task, AudioReady)   ← atomic fetch_or のみ
  └── request_context_switch()         ← PendSV ペンド
                                          │
PendSV (優先度 0xFF)                       ▼
  ├── resolve_pending()                ← Blocked→Ready 遷移
  └── get_next_task() → コンテキストスイッチ
```

`resolve_pending()` は全タスクを走査し、`wait_mask` に一致する通知ビットが立っている Blocked タスクを Ready に遷移させる。PendSV は BASEPRI でマスクされるコンテキストで実行されるため安全である。

### イベントフラグ

イベントフラグはビットマスクで表現する（[03-port/06-syscall.md](03-port/03-port/06-syscall.md) §イベントフラグ参照）。

```
audio    = (1 << 0)   オーディオバッファ準備完了
midi     = (1 << 1)   MIDI データ利用可能
vsync    = (1 << 2)   画面リフレッシュ
timer    = (1 << 3)   タイマーティック
control  = (1 << 4)   コントロール入力
shutdown = (1 << 31)  シャットダウン要求
```

### notify()

BASEPRI でマスクされるコンテキスト（タスクまたは BASEPRI 閾値以下の ISR）からイベントフラグを設定する。対象タスクが Blocked 状態かつマスクが一致すれば Ready に遷移させる。

> **注意**: BASEPRI 閾値を超える ISR（Audio DMA 等）からは `notify()` を呼んではならない。上記の `signal()` + PendSV パターンを使うこと。

### wait_block()

イベントフラグの消費とブロック遷移を**不可分**に行う:

1. クリティカルセクションに入る
2. 指定マスクに一致するフラグがあれば消費して即戻る
3. なければ wait_mask を設定し、タスクを Blocked に遷移
4. `schedule()` で別タスクに切り替え
5. 再開時にフラグを消費して戻る

この原子性が競合条件（notify と wait の間にフラグが失われる問題）を防ぐ。

### 割り込み優先度設計

```
優先度 0x00: Audio DMA (I2S, PDM) — 最高。BASEPRI 非マスク
優先度 0x40: USB OTG FS           — BASEPRI でマスクされる
優先度 0xF0: SysTick              — BASEPRI でマスクされる
優先度 0xFF: PendSV               — 最低（コンテキストスイッチ用）
```

BASEPRI 閾値は `0x10`（STM32F4 の 4-bit 優先度で priority ≥ 1 をマスク）。優先度 0x00 の Audio DMA だけが閾値を超え、クリティカルセクション中も実行可能。それ以外の ISR（USB、SysTick 等）はクリティカルセクション中はマスクされる。

Audio DMA ISR はロックフリー操作のみ（SPSC push + signal + PendSV ペンド）を行い、数十サイクルで完了する。これにより DMA ダブルバッファの切り替えが途切れないことを保証する。

---

## タイマーとスリープ

### SysTick

- 1ms 周期で動作
- タスクの定期起床（Timer イベント）を管理
- `Sleep` syscall のタイムアウト管理

### Tickless 電力管理（新設計）

アイドル時の消費電力を最小化する:

| モード | 復帰時間 | 条件 |
|--------|---------|------|
| WFI | ~1μs | オーディオ処理中、または次の起床が近い |
| Stop | ~5μs | 次の起床まで 100μs 以上、かつオーディオ非アクティブ |

IdleTask がスリープモードを自動選択する。

---

## パフォーマンス計測

DWT（Data Watchpoint and Trace）を使用したサイクル精度の計測を提供する。

| メトリクス | 内容 |
|-----------|------|
| context_switch | 切替回数、最小/最大/累積サイクル |
| audio | 最新/最大処理サイクル、overrun/underrun 回数 |

シェルコマンド `show cpu` で確認可能。

---

## ポート層 API

RT-Kernel は以下のポート関数をターゲット実装に要求する:

| 関数 | 責務 |
|------|------|
| `init_systick` | SysTick タイマー初期化 |
| `init_dwt` | DWT サイクルカウンタ初期化 |
| `get_dwt_cycles` | 現在のサイクルカウント取得 |
| `yield` | CPU 制御の自発的な譲渡 |
| `wfi` | Wait For Interrupt |
| `request_ctx_switch` | PendSV をペンド |
| `init_task` | TCB とスタックの初期化 |
| `start_scheduler` | スケジューラ開始（最初のタスクに切り替え） |
| `svc_callback` | SVC ハンドラ（syscall ディスパッチ） |
| `ctx_switch_callback` | PendSV ハンドラ（レジスタ退避/復元） |
| `tick_callback` | SysTick ハンドラ |
| `enable_fpu` | FPU 有効化 |
| `enable_dwt` | DWT 有効化 |

---

## 関連ドキュメント

- [00-overview.md](00-overview.md) — タスクモデル概要
- [03-port/06-syscall.md](03-port/03-port/06-syscall.md) — WaitEvent / Yield / Sleep の ABI 定義
- [03-port/07-memory.md](03-port/03-port/07-memory.md) — タスクスタックのメモリ配置
- [12-memory-protection.md](12-memory-protection.md) — MPU によるタスク隔離、Fault 処理
