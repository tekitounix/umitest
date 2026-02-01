# 12 — メモリ保護と監視

## 概要

MPU によるメモリ保護、ヒープ/スタック衝突検出、Fault 処理と隔離。
本章は [07-memory.md](07-memory.md) のレイアウト定義を前提とし、保護・監視・障害対応の仕様を規定する。

| 項目 | 状態 |
|------|------|
| MPU リージョン設計 | 実装済み |
| ヒープ/スタック衝突検出 | 実装済み |
| Fault 記録と隔離 | 実装済み |
| ヒープ/スタック使用量モニタリング | 実装済み |
| MPU 非搭載ターゲットの縮退動作 | 新設計 |

---

## 設計原則

- OS はアプリの Fault から**生存**できなければならない
- アプリは非特権モードで実行され、カーネル領域にはアクセスできない
- 共有メモリは OS/アプリ双方からアクセス可能だが、Fault 時は信頼しない
- MPU 非搭載ターゲットでは完全な隔離は不可能なため、OS 生存はベストエフォートとする

---

## MPU リージョン設計

STM32F4 の MPU はリージョンサイズが 2^n アラインメントの制約を持つ。
メモリレイアウトの詳細は [07-memory.md](07-memory.md) を参照。

### 保護リージョン

| リージョン | アドレス範囲 | サイズ | 権限（アプリから） | 実行 |
|-----------|-------------|--------|-------------------|------|
| Kernel | 0x2000_0000 – 0x2000_7FFF | 32KB | アクセス不可 | — |
| Shared | 0x2000_8000 – 0x2000_BFFF | 16KB | 読み書き可 | 不可 |
| App Data | 0x2000_C000 – 0x2001_3FFF | 32KB | 読み書き可 | 不可 |
| App Stack | 0x2001_C000 – 0x2001_FFFF | 16KB | 読み書き可 | 不可 |
| App Text | Flash 上 | 可変 | 読取専用 | 可 |

### 2^n 制約によるギャップ

App Data の論理領域（0x2000C000–0x20017FFF, 48KB）と MPU リージョン（32KB）にはギャップがある。
0x20014000–0x20017FFF は MPU 保護外のため、アプリがアクセスすると MemManage Fault となる。

0x20018000–0x2001BFFF はガードページまたは将来の拡張用として予約。

### リンカ配置要件

- `app.ld` は **MEMORY 定義のみ**（デバイス依存）
- `app_sections.ld` に **SECTIONS を集約**（共通化）
- `APP_DATA` / `APP_STACK` の 2 リージョンに分割する

---

## ヒープ/スタック衝突検出

App Stack 領域内で Heap は上向き、Stack は下向きに成長する。

```
_estack ┌──────────────┐
        │ Stack ↓      │
        │              │
        │ (空き)       │
        │              │
        │ Heap ↑       │
_sheap  └──────────────┘
```

### 検出方式

| 方向 | 方式 | タイミング |
|------|------|-----------|
| Heap → Stack | `operator new` 内で SP と比較 | ヒープ割り当て時 |
| Stack → Heap | 事前検出不可。MPU ガードページまたは定期監視 | — |

### ヒープ衝突検出アルゴリズム

ヒープ拡張時に現在の SP を参照し、次の割り当て範囲が SP を越えないことを確認する:

```cpp
void* operator_new(std::size_t size, std::size_t align) {
    uint8_t* aligned = align_up(heap_cur, align);
    uint8_t* sp = read_current_sp();
    constexpr std::size_t guard = 64;  // 最小安全マージン

    if (aligned + size + guard > sp) {
        return nullptr;  // OOM/衝突
    }

    heap_cur = aligned + size;
    return aligned;
}
```

- `read_current_sp()` は PSP/MSP の運用に合わせる
- `guard` の値はターゲット依存（最小安全マージン）
- 失敗時は `nullptr` 返却、または Kernel 方針に従い panic

---

## Fault 処理と隔離

### 処理フロー

```
Fault ISR
  └─ record_fault()    // 例外レジスタ + SP/PC/LR を保存

Kernel Task (SystemTask)
  ├─ process_pending_fault()
  ├─ アプリ terminate
  ├─ UI/LED 更新（エラー表示）
  └─ Shell 有効化（デバッグ可能に）
```

### 規範

- Fault ログは**カーネル RAM に保持**する（SharedMemory は Fault 時に破壊される可能性があるため信頼しない）
- Fault ISR は記録のみ行い、復旧処理は SystemTask で行う
- アプリ Fault 後も OS（シェル、USB、LED）は動作を継続する

### 記録内容

| フィールド | 内容 |
|-----------|------|
| PC | Fault 発生アドレス |
| LR | リンクレジスタ |
| SP | スタックポインタ |
| CFSR | Configurable Fault Status Register |
| MMFAR | MemManage Fault Address Register |
| BFAR | BusFault Address Register |
| タイムスタンプ | Fault 発生時刻 |

---

## ヒープ/スタック使用量モニタリング

### MemoryUsage 構造体

カーネルが定期的に収集し、シェルコマンド `show memory` で表示:

| 項目 | 内容 |
|------|------|
| heap_used | 現在のヒープ使用量 |
| heap_peak | ヒープ使用量のピーク値 |
| stack_used | 現在のスタック使用量（_estack - SP） |
| stack_peak | スタック使用量のピーク値 |

- `MemoryUsage` / `FaultLog` は**カーネル RAM に保持**する
- UI 表示に必要な要約のみ、正常時に SharedMemory へコピー

---

## MPU 非搭載ターゲットの縮退動作

### ProtectionMode

MPU 有無とデバッグ用途に応じてコンパイル時に選択:

| モード | MPU | 特権分離 | 用途 |
|--------|-----|---------|------|
| Full | 有効 | 非特権 | 本番 |
| Privileged | 無効 | 特権 | MPU なし MCU |
| PrivilegedWithMpu | 有効 | 特権 | デバッグ |

```cpp
template <class HW, ProtectionMode Mode = ProtectionMode::Full>
class Protection {
    static constexpr bool uses_mpu() { return Mode != ProtectionMode::Privileged; }
    static constexpr bool needs_syscall() { return Mode == ProtectionMode::Full; }
};
```

- Privileged モードでは MPU 設定をスキップし、全コードが特権で動作する
- API 経路（syscall）は統一のまま維持する
- OS 生存はベストエフォート（アプリの不正アクセスを検出できない）

---

## 関連ドキュメント

- [07-memory.md](07-memory.md) — メモリレイアウト（アドレス、サイズ）
- [11-scheduler.md](11-scheduler.md) — タスク隔離とコンテキストスイッチ
- [09-app-binary.md](09-app-binary.md) — AppHeader のセクションサイズ定義
- [13-system-services.md](13-system-services.md) — Fault 後の Shell 有効化、Diagnostics
