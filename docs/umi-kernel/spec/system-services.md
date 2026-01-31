# UMI Kernel システムサービス仕様

**規範レベル:** MUST/SHALL/REQUIRED, SHOULD/RECOMMENDED, MAY/NOTE/EXAMPLE
**対象読者:** Kernel Dev / Porting / App Dev
**適用範囲:** UMI-OS の OS サービス（syscall/シェル/USB/FS/監視）

---

## 1. 目的・スコープ
本書は OS が提供するサービスの規範を定義する。主対象は `syscall` と OS 内サービスである。

---

## 2. Syscall 体系
### 2.1 番号体系
```
0–15:   コアAPI（プロセス制御・スケジューリング・情報取得）
16–31:  予約（コアAPI拡張）
32–47:  ファイルシステム
48–63:  予約（ストレージ拡張）
64–255: 予約（実験・ベンダ拡張）
```

### 2.2 Syscall 選定基準
Syscall は以下のいずれかを満たす場合に限定する。

1. **特権ハードウェアアクセス**が必要（MPU 境界を越える）。
2. **スケジューラ状態の変更**が必要（アトミック性が必須）。
3. **ブートストラップ問題**（SharedMemory ポインタ取得など）。

SharedMemory で済むものは syscall にしない。

### 2.3 コアAPI（0–15）
| # | Syscall | 状態 | 用途 |
|---|---|---|---|
| 0 | `Exit` | 実装済み | アプリ終了（将来: アンロードトリガー） |
| 1 | `Yield` | 実装済み | CPU譲渡 |
| 2 | `WaitEvent` | 実装済み | イベント待ち（timeout引数付き） |
| 3 | `GetTime` | 実装済み | モノトニック時刻取得 |
| 4 | `GetShared` | 実装済み | SharedMemoryポインタ取得 |
| 5 | `RegisterProc` | 実装済み | Processor登録 |
| 6 | `UnregisterProc` | 将来 | Processor解除 |

### 2.4 ファイルシステムAPI（32–47）
| # | Syscall | 状態 | 用途 |
|---|---|---|---|
| 32 | `FileOpen` | 将来 | ファイルオープン |
| 33 | `FileRead` | 将来 | ファイル読み取り |
| 34 | `FileWrite` | 将来 | ファイル書き込み |
| 35 | `FileClose` | 将来 | ファイルクローズ |
| 36 | `FileSeek` | 将来 | シーク |
| 37 | `FileStat` | 将来 | サイズ取得 |
| 38 | `DirOpen` | 将来 | ディレクトリオープン |
| 39 | `DirRead` | 将来 | ディレクトリ列挙 |
| 40 | `DirClose` | 将来 | ディレクトリクローズ |

### 2.5 コアAPI 詳細（0–15）
#### 2.5.1 `Exit`
- 引数: `code`（int）
- 戻り値: なし（終了）
- 効果: アプリ終了（将来: アンロードトリガー）

#### 2.5.2 `Yield`
- 引数: なし
- 戻り値: 0
- 効果: CPU 譲渡（スケジューラに再選択を依頼）

#### 2.5.3 `WaitEvent`
```
int32_t WaitEvent(uint32_t mask, uint32_t timeout_us);
```
- `mask`: 待ちイベントの OR マスク
- `timeout_us`: タイムアウト（0 は無期限）
- 戻り値: 発生したイベントビットマスク

**例: SVC 呼び出し（概念コード）**
```cpp
int32_t syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2) {
	register uint32_t r0 __asm__("r0") = nr;
	register uint32_t r1 __asm__("r1") = a0;
	register uint32_t r2 __asm__("r2") = a1;
	register uint32_t r3 __asm__("r3") = a2;
	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3) : "memory");
	return static_cast<int32_t>(r0);
}
```

#### 2.5.4 `GetTime`
`GetTime` は **64-bit モノトニック時刻（μs）** を返す。
カーネル側は `sp[0]`（r0）に下位 32-bit、`sp[1]`（r1）に上位 32-bit を格納し、
アプリ側 `get_time_usec()` が 64-bit に組み立てる。

**注:** `GetTime` は `gettimeofday` の実装に利用し、
`std::chrono::now` など高水準 API が利用可能になることを前提とする。

**タイマ実装指針:** 多くの MCU では 32-bit タイマを **1µs刻み**で動作させ、
**オーバーフロー割り込みのみ**で上位ビットを加算して 64-bit 化する。
割り込み頻度は約 71.6 分に 1 回（$2^{32}\,\mu s$）とする。

#### 2.5.5 `GetShared`
- 引数: なし
- 戻り値: `SharedMemory*`
- 効果: SharedMemory 先頭アドレスを返す

#### 2.5.6 `RegisterProc`
- 引数: `processor`, `fn`
- 戻り値: 0 / -1
- 効果: Processor 登録（`fn==0` なら simple 登録）

#### 2.5.7 `UnregisterProc`
- 将来実装

### 2.6 ファイルシステムAPI 詳細（32–47）
#### 2.6.1 `FileOpen`
- 将来実装

#### 2.6.2 `FileRead`
- 将来実装

#### 2.6.3 `FileWrite`
- 将来実装

#### 2.6.4 `FileClose`
- 将来実装

#### 2.6.5 `FileSeek`
- 将来実装

#### 2.6.6 `FileStat`
- 将来実装

#### 2.6.7 `DirOpen`
- 将来実装

#### 2.6.8 `DirRead`
- 将来実装

#### 2.6.9 `DirClose`
- 将来実装

---

## 3. SysEx / シェル / stdio
- 標準入出力とシェルは **SysEx 経由**で提供する。
- OS は SysEx を受信し、SystemTask 内でコマンド処理を行う。
- デバッグ出力は SysEx 経由とし、専用の UART 依存は持たない。

**例: SysEx メッセージ構造（概念）**
```
F0 ... [Manufacturer] [CMD] [SEQ] [PAYLOAD...] [CHECKSUM] F7
```

### 3.1 シェルコマンド（仕様）
**最低限提供すべきコマンド群（例）**

基本:
- `help`, `version`, `uptime`, `whoami`, `auth`, `logout`

表示系:
- `show system`, `show cpu`, `show memory`, `show tasks`, `show audio`, `show midi`, `show usb`, `show errors`

管理系（管理者権限）:
- `config midi channel <1-16>`
- `config audio gain <0-100>`
- `diag watchdog [feed|enable|disable]`
- `diag reset`

**注:** コマンド名や引数は実装で拡張され得るが、**同等の機能群**は維持すること。

### 3.2 シェル状態取得インターフェース（概念）
```cpp
struct StateProvider {
	KernelStateView& state();
	ShellConfig& config();
	ErrorLog<16>& error_log();
	void reset_system();
	void feed_watchdog();
	void enable_watchdog(bool);
};
```

### 3.3 SysEx コマンド定義（例示）
**注:** コマンド値は実装で拡張され得るため、**ここでは代表例**を示す。

| 範囲 | コマンド | 用途 |
|---|---|---|
| 0x01–0x0F | `STDOUT_DATA`, `STDERR_DATA`, `STDIN_DATA`, `STDIN_EOF` | 標準入出力 |
| 0x10–0x1F | `FW_BEGIN`, `FW_DATA`, `FW_VERIFY`, `FW_COMMIT`, `FW_ACK`, `FW_NACK` | 更新 |
| 0x20–0x2F | `PING`, `PONG`, `RESET`, `VERSION` | システム |

**例: 定義（概念コード）**
```cpp
enum class SysExCmd : uint8_t {
	StdoutData = 0x01,
	StderrData = 0x02,
	StdinData  = 0x03,
	StdinEof   = 0x04,
	FwBegin    = 0x12,
	FwData     = 0x13,
	FwVerify   = 0x14,
	FwCommit   = 0x15,
	FwAck      = 0x18,
	FwNack     = 0x19,
	Ping       = 0x20,
	Pong       = 0x21,
	Reset      = 0x22,
	Version    = 0x23,
};
```

### 3.4 7-bit エンコーディング（必須）
SysEx では 7-bit にパックし、**MSB を別バイトに集約**する。

**例: 7バイト入力 → 8バイト出力（概念コード）**
```cpp
std::vector<uint8_t> encode7bit(const uint8_t* data, size_t len) {
	std::vector<uint8_t> out;
	size_t i = 0;
	while (i < len) {
		uint8_t msb = 0;
		for (int j = 0; j < 7 && i < len; ++j, ++i) {
			if (data[i] & 0x80) msb |= (1u << j);
			out.push_back(data[i] & 0x7F);
		}
		out.insert(out.end() - std::min<size_t>(7, len - (i - 7)), msb);
	}
	return out;
}
```

### 3.5 チェックサム（例示）
チェックサム方式は **実装依存**だが、最小限として XOR 方式を採用可能。

**例: XOR チェックサム（概念コード）**
```cpp
uint8_t checksum_xor7(const uint8_t* data, size_t len) {
	uint8_t c = 0;
	for (size_t i = 0; i < len; ++i) c ^= (data[i] & 0x7F);
	return c & 0x7F;
}
```

---

## 4. USB MIDI / Audio
- USB MIDI は **イベント入力**および **送信 API**として提供する。
- USB Audio は **OUT パス**と **IN パス**を独立系統として扱う。
- ストリーミング状態に応じて IN パス出力の有効/無効を切り替える。

**例: MIDI 受信フロー（概念）**
```
USB ISR → SPSC push → AudioTask drain → Event化 → process()
```

---

## 5. ファイルシステム（将来）
- FS syscall は **非同期**（要求発行→即 return→`WaitEvent(event::FS)`で完了取得）。
- SystemTask 内の **StorageService** が要求キューを逐次処理する。
- 下層は **BlockDevice** 抽象で統一し、操作ごとに待ち方式を分ける:
	- read/prog: busy wait
	- erase: DMA + `WaitEvent`（CPU解放）
- FS 実装は **littlefs（Flash系）** と **FATfs（SD）** を想定。
- 電断耐性は littlefs を前提とし、SD は PC 互換用途に限定する。

### 5.1 階層構造
```
App (非同期syscall: fs::open/read/write/close → 即return → WaitEvent(FS)で完了取得)
	↓
StorageService (SystemTask内、要求キューから逐次処理)
	↓
ファイルシステム (littlefs / FATfs)
	↓
BlockDevice (erase: DMA+WaitEvent、read/prog: busy wait)
	↓
InternalFlash / SPI Flash / FRAM/EEPROM / SD(SPI/SDIO)
```

### 5.2 BlockDevice 抽象
```cpp
struct BlockDevice {
		uint32_t block_size;      // 最小消去単位
		uint32_t write_size;      // 最小書き込み単位
		uint32_t total_size;
		bool needs_erase;         // Flash系: true, FRAM/EEPROM: false

		int32_t read(uint32_t addr, void* buf, uint32_t len);
		int32_t write(uint32_t addr, const void* buf, uint32_t len);
		int32_t erase(uint32_t addr, uint32_t len);
};
```

### 5.3 BlockDevice 内の待ち方式
| SPI操作 | 待ち時間 | 待ち方式 | 理由 |
|---|---:|---|---|
| read | 数十µs | busy wait | コンテキストスイッチが相対的に高コスト |
| prog (page write) | 数百µs〜1ms | busy wait | UIフレーム(16ms)に対して十分短い |
| erase (sector) | 数ms〜数十ms | DMA + `WaitEvent` | CPU浪費を避けるためBlocked化 |

### 5.4 非同期フロー
```
ControlTask: fs::write() → SVC → 要求キュー → 即return → UI継続
SystemTask:  要求受信 → littlefs → BlockDevice::erase()
	→ SPI eraseコマンド → WaitEvent(SpiComplete) → Blocked
IRQ: notify(SystemTask, SpiComplete)
SystemTask:  Resume → littlefs続行 → 完了 → notify(ControlTask, FS)
ControlTask: WaitEvent(FS)で結果取得
```

### 5.5 FS 選定
- **Flash系**: littlefs（電断耐性・動的ウェアレベリング）
- **SDカード**: FATfs（PC互換）

#### 5.5.1 併用構成例
```
/flash/... → littlefs (SPI Flash)
/sd/...    → FATfs (SD)
```

### 5.6 電断耐性
- littlefs: COW 方式により電断耐性を標準で確保。
- FATfs: 電断耐性は弱いため、重要データは Flash(littlefs)に置く。

### 5.7 メディア別特性（要約）
| メディア | block_size | write_size | needs_erase | FS |
|---|---:|---:|---:|---|
| 内蔵Flash | 16KB–128KB | 1–4B | true | littlefs |
| SPI Flash | 4KB | 256B | true | littlefs |
| FRAM/EEPROM | 1B | 1–128B | false | （KVS推奨） |
| SD | 512B | 512B | false* | FATfs |

**例: 非同期フロー（概念）**
```
App: fs::write() → return
SystemTask: execute → notify(event::FS)
App: WaitEvent(event::FS) → result
```

### 5.1 BlockDevice 抽象（概念）
```cpp
struct BlockDevice {
	uint32_t block_size;  // 消去単位（例: Flash 4KB など）
	uint32_t write_size;  // 書き込み単位
	uint32_t total_size;
	bool needs_erase;

	int32_t read(uint32_t addr, void* buf, uint32_t len);
	int32_t write(uint32_t addr, const void* buf, uint32_t len);
	int32_t erase(uint32_t addr, uint32_t len);
};
```

### 5.2 ブロッキング方針（重要）
**長時間操作は Blocked 化**し、ControlTask に CPU を渡す。

**例: erase の待機（概念）**
```
SystemTask: issue erase → WaitEvent(SpiComplete) → Blocked
IRQ: notify(SystemTask)
SystemTask: resume → continue
```

### 5.3 FS 選定方針（例示）
- Flash系: littlefs
- SDカード: FATfs

**注:** 実装選定はターゲット依存のため、ここでは方針のみを規定する。

---

## 6. 監視/ログ
- CPU サイクル計測・オーディオ負荷等のメトリクスを提供する。
- ログ出力は SysEx 経由の stdio に集約する。

**例: メトリクス構造（概念）**
```cpp
struct KernelMetrics {
	struct ContextSwitch { uint32_t count; } context_switch;
	struct Audio { uint32_t cycles_max; uint32_t overruns; } audio;
};
```
