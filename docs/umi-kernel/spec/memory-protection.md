# UMI メモリ/保護仕様

**規範レベル:** MUST/SHALL/REQUIRED, SHOULD/RECOMMENDED, MAY/NOTE/EXAMPLE
**対象読者:** Kernel Dev / Porting
**適用範囲:** UMI-OS のメモリ保護・Fault 方針

---

## 1. 目的・スコープ
本書は APP_RAM のレイアウト、MPU 境界、Fault 記録と隔離方針を定義する。

---

## 2. メモリレイアウト
- APP_RAM は **Data領域** と **Stack領域** の2リージョンで保護する。
- サイズやアドレスはターゲット依存であり、**下記は例**である。

**例: 32KB(Data) + 16KB(Stack)**
```
0x20018000 = _estack (例)
┌────────────────────────────┐ 16KB (AppStack)
│ Stack（下向き）            │
│                            │
│ Heap（上向き）             │
└────────────────────────────┘ 0x20014000 (例)
┌────────────────────────────┐ 32KB (AppData)
│ .data / .bss                │
└────────────────────────────┘ 0x2000C000 (例)
```

**構造上の要件**
- `_sheap` は Stack 領域の下端に置く。
- `_estack` は Stack 領域の上端に置く。
- Heap は上向き、Stack は下向きに成長する。

**リンカ配置要件（共通）**
- `app.ld` は **MEMORY 定義のみ**（デバイス依存）。
- `app_sections.ld` に **SECTIONS を集約**（共通化）。
- `APP_DATA` / `APP_STACK` の2リージョンに分割する。

---

## 3. MPU 境界
- AppText: 実行可・読取専用
- AppData/AppStack: 実行不可・読書可
- SharedMemory: 実行不可・読書可

---

## 4. ヒープ/スタック衝突検出
- `operator new` は `_sheap`〜`_estack` を使用する。
- **ヒープ→スタック衝突**は検出する。
- **スタック→ヒープ衝突**は事前検出不可のため、監視で対応する。

### 4.1 衝突検出アルゴリズム（必須）
ヒープ拡張時に SP を参照し、**次の割り当て範囲が SP を越えないこと**を確認する。

**疑似コード（規範的）**
```cpp
// 事前定義シンボル
extern uint8_t _sheap;   // Heap開始
extern uint8_t _estack;  // Stack上端

static uint8_t* heap_cur = &_sheap;

void* operator_new(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
	// 1) アライン
	uint8_t* aligned = align_up(heap_cur, align);

	// 2) 現在SP取得（PSP想定）
	uint8_t* sp = read_current_sp();

	// 3) 予約領域（ガード）
	constexpr std::size_t guard = 64; // 例: 最低限の安全マージン

	// 4) 衝突判定
	if (aligned + size + guard > sp) {
		return nullptr; // OOM/衝突
	}

	// 5) 確保
	heap_cur = aligned + size;
	return aligned;
}
```

**要件**
- `read_current_sp()` は PSP/MSP の運用に合わせる。
- `guard` の値はターゲット依存であり、**最小安全マージン**として定義する。
- 失敗時は `nullptr` を返すか、Kernel方針に従って panic を発火する。

---

## 5. Fault 記録と隔離
- Fault ログは **Kernel RAM に保持**する。
- SharedMemory は Fault 時に破壊される可能性があるため、信頼しない。
- Fault ISR は記録のみ行い、復旧処理は Kernel タスクで行う。

**処理フロー（規範）**
```
Fault ISR
	└ record_fault()  // 例外レジスタ＋SP/PC/LRを保存

Kernel Task
	├ process_pending_fault()
	├ App terminate
	├ UI/LED更新
	└ Shell有効化
```

## 6. 監視データの配置
- `MemoryUsage` / `FaultLog` は **Kernel RAM に保持**する。
- UI 表示に必要な要約のみ、SharedMemory へコピー（正常時のみ）。
