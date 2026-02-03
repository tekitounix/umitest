# Syscall 再設計提案

## 背景

現行 [03-port/06-syscall.md](03-port/03-port/06-syscall.md) の全 syscall を選定基準に照らして検証した結果、
冗長な番号、不足している機能領域、一貫性の問題が明らかになった。

本ドキュメントは syscall 体系の再設計を提案する。

## 選定基準（変更なし）

syscall にする条件（いずれかを満たすこと）:

1. **特権ハードウェアアクセス**が必要（MPU 境界を越える）
2. **スケジューラ状態の変更**が必要（アトミック性が必須）
3. **ブートストラップ問題**（SharedMemory ポインタ取得等）

## 現行 syscall の検証結果

### 変更なし（妥当）

| Nr | 名前 | 根拠 |
|----|------|------|
| 0 | Exit | スケジューラ状態変更（タスク終了） |
| 1 | Yield | スケジューラ状態変更 |
| 2 | RegisterProc | カーネル特権領域への書き込み |
| 3 | UnregisterProc | 同上 |
| 10 | WaitEvent | スケジューラ状態変更（タスクブロック） |
| 11 | GetTime | 特権ハードウェアタイマ読み取り |
| 12 | Sleep | スケジューラ状態変更 |
| 20 | SetAppConfig | ダブルバッファ切替のアトミック性 |
| 40 | GetShared | ブートストラップ問題 |
| 50 | Log | 特権 HW（USB/SysEx 送信） |
| 51 | Panic | 特権操作（システム停止） |
| 60–84 | FS 群 | カーネルキュー投入 + SystemTask 通知 |

### 削除提案

| Nr | 名前 | 理由 |
|----|------|------|
| 21 | SetRouteTable | SetAppConfig (Nr 20) の部分操作。一括設定で十分 |
| 22 | SetParamMapping | 同上 |
| 23 | SetInputMapping | 同上 |
| 24 | ConfigureInput | 同上 |

**根拠**: AppConfig は RouteTable + ParamMapping + InputParamMapping + InputConfig の一括構造体。
個別に設定する場合でも、アプリ側で AppConfig を部分更新して `SetAppConfig` で適用すれば済む。
ダブルバッファのブロック境界切替は一括操作でのみ意味がある（部分更新は中間状態を生む）。

> 削除した番号は ABI 互換性のため予約として保持し、再利用しない。

### 変更提案

| Nr | 名前 | 変更内容 |
|----|------|---------|
| 25 | SendParamRequest | 維持。Controller→EventRouter へのリアルタイムパラメータ変更は AppConfig 切替とは異なる用途 |
| 31 | MidiRecv | **削除**。EventRouter 経由の ControlEvent で代替可能。後述 |

---

## 追加提案

### グループ 3: MIDI (30–39) — 整理

MIDI 受信は EventRouter が既に担っている。アプリは WaitEvent(event::control) で MIDI 由来の
ControlEvent を受け取る。`MidiRecv` は EventRouter を迂回する手段であり、アーキテクチャに反する。

SysEx はサイズが大きく EventQueue に載らないため、専用 syscall が必要。

| Nr | 名前 | アプリ API | 状態 |
|----|------|-----------|------|
| 30 | MidiSend | `umi::midi_send(data, len, port)` | 維持（特権 HW） |
| ~~31~~ | ~~MidiRecv~~ | — | **削除**（EventRouter 経由で代替） |
| 32 | ReadSysex | `umi::read_sysex(buf, len)` | 維持（カーネル側バッファ読み取り） |
| 33 | SendSysex | `umi::send_sysex(data, len, port)` | 維持（特権 HW） |

### グループ 1 拡張: 電源管理 (13–16)

オーディオ機器にとって電源管理はハード制約。バッテリー駆動、スリープ復帰、シャットダウン前の
FS sync が必要。全て特権 HW アクセス（基準 1）またはスケジューラ状態変更（基準 2）に該当。

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 根拠 |
|----|------|-----------|------|--------|------|
| 13 | GetPowerState | `umi::get_power_state(info)` | `info: PowerStateInfo*` | 0 / エラー | 基準 1（ADC/PMIC 読み取り） |
| 14 | SetPowerPolicy | `umi::set_power_policy(policy)` | `policy: const PowerPolicy*` | 0 / エラー | 基準 1,2（スリープモード設定） |
| 15 | RequestShutdown | `umi::request_shutdown()` | — | — (停止) | 基準 1,2（電源制御 + FS sync） |

```cpp
struct PowerStateInfo {
    uint16_t battery_mv;        // バッテリー電圧 (mV)、0 = バス電源
    uint8_t battery_percent;    // 残量 (0–100)、0xFF = 不明
    uint8_t charge_state;       // 0=非充電, 1=充電中, 2=満充電
    uint8_t power_source;       // 0=バッテリー, 1=USB, 2=DC
};

struct PowerPolicy {
    uint32_t idle_timeout_ms;   // アイドルスリープまでの時間 (0=無効)
    uint8_t sleep_mode;         // 0=WFI, 1=Stop, 2=Standby
};
```

### グループ 4 拡張: 診断・プロファイリング (41–44)

KernelMetrics / FaultLog は実装済みだが、アプリからの取得 API がない。
デバッグビルドや開発ツール連携に必須。全て特権領域の読み取り（基準 1）。

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 根拠 |
|----|------|-----------|------|--------|------|
| 41 | GetMetrics | `umi::get_metrics(m)` | `m: KernelMetrics*` | 0 / エラー | 基準 1（カーネル内部構造体） |
| 42 | GetFaultLog | `umi::get_fault_log(idx, entry)` | `idx: uint8_t, entry: FaultEntry*` | 0 / エラー | 基準 1 |
| 43 | ResetMetrics | `umi::reset_metrics()` | — | 0 | 基準 1 |

> DWT サイクルカウンタは `GetMetrics` 内の `process_cycles` フィールドで十分。
> 専用の profiling syscall は不要（YAGNI）。

### グループ 5 拡張: ディスプレイ (52–54)

`event::vsync` フラグは定義済みだが、対応する syscall がない。
OLED/LCD 搭載ボード（Daisy Pod 等の拡張、将来の製品）で必要。

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 根拠 |
|----|------|-----------|------|--------|------|
| 52 | GetDisplayInfo | `umi::get_display_info(info)` | `info: DisplayInfo*` | 0 / エラー | 基準 1 |
| 53 | SwapDisplayBuffer | `umi::swap_display_buffer()` | — | 0 / エラー | 基準 1（DMA/SPI 転送開始） |

```cpp
struct DisplayInfo {
    uint16_t width;
    uint16_t height;
    uint8_t pixel_format;       // 0=MONO_1BPP, 1=RGB565, 2=RGB888
    uint8_t buffer_count;       // 1=シングル, 2=ダブル
    void* draw_buffer;          // アプリが描画するバッファ（SharedMemory 内）
};
```

> フレームバッファは SharedMemory 上に配置し、アプリが直接描画。
> `SwapDisplayBuffer` で転送を開始し、完了時に `event::vsync` が通知される。
> 解像度変更は組み込みでは不要（固定ハードウェア）。必要になったら追加。

### グループ 9 (新設): オーディオデバイス制御 (90–95)

オーディオインターフェイスアプリケーションの要件。サンプルレート、バッファサイズ、
コーデック設定等をアプリ側から制御する。全て特権 HW アクセス（基準 1）。

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 根拠 |
|----|------|-----------|------|--------|------|
| 90 | GetAudioDeviceInfo | `umi::get_audio_device_info(info)` | `info: AudioDeviceInfo*` | 0 / エラー | 基準 1 |
| 91 | GetAudioDeviceCaps | `umi::get_audio_device_caps(caps)` | `caps: AudioDeviceCaps*` | 0 / エラー | 基準 1 |
| 92 | SetAudioDeviceConfig | `umi::set_audio_device_config(cfg)` | `cfg: const AudioDeviceConfig*` | 0=受付 / エラー | 基準 1（DMA/PLL/コーデック再構成） |
| 93 | SetCodecParam | `umi::set_codec_param(param)` | `param: const CodecParam*` | 0=受付 / エラー | 基準 1（I2C コーデック制御） |
| 94 | GetCodecParam | `umi::get_codec_param(param)` | `param: CodecParam*` | 0 / エラー | 基準 1 |

```cpp
struct AudioDeviceInfo {
    uint32_t sample_rate;           // 現在のサンプルレート
    uint16_t buffer_size;           // 現在のバッファサイズ（サンプル数）
    uint8_t input_channels;         // 入力チャンネル数
    uint8_t output_channels;        // 出力チャンネル数
    uint8_t bit_depth;              // ビット深度 (16/24/32)
    uint8_t codec_id;               // コーデック種別 (0=AK4556, 1=WM8731, 2=PCM3060, ...)
    uint8_t clock_source;           // 0=Internal, 1=External, 2=USB SOF
    uint8_t _pad;
};

struct AudioDeviceCaps {
    uint32_t supported_rates;       // ビットフィールド (bit0=8kHz, bit1=16kHz, ..., bit5=48kHz, bit6=96kHz, bit7=192kHz)
    uint8_t max_input_channels;
    uint8_t max_output_channels;
    uint8_t supported_bit_depths;   // ビットフィールド (bit0=16, bit1=24, bit2=32)
    uint8_t flags;                  // bit0=external_clock, bit1=usb_audio
};

struct AudioDeviceConfig {
    uint32_t sample_rate;           // 希望サンプルレート (0=変更なし)
    uint16_t buffer_size;           // 希望バッファサイズ (0=変更なし)
    uint8_t bit_depth;              // 希望ビット深度 (0=変更なし)
    uint8_t clock_source;           // クロックソース (0xFF=変更なし)
};

struct CodecParam {
    uint8_t codec_id;               // 対象コーデック
    uint8_t param_type;             // パラメータ種別（下記参照）
    uint8_t channel;                // 対象チャンネル (0xFF=全ch)
    uint8_t _pad;
    int32_t value;                  // パラメータ値（型はparam_typeに依存）
};
```

#### CodecParam の param_type

| 値 | 名前 | value の意味 | 備考 |
|----|------|-------------|------|
| 0 | GAIN_DB | ゲイン (0.1dB 単位、例: -120 = -12.0dB) | 入出力ゲイン |
| 1 | MUTE | 0=解除, 1=ミュート | チャンネル単位 |
| 2 | HPF_ENABLE | 0=無効, 1=有効 | DC カットフィルタ |
| 3 | DEEMPHASIS | 0=Off, 1=32kHz, 2=44.1kHz, 3=48kHz | デエンファシス |
| 4 | PHANTOM_POWER | 0=Off, 1=On | コンデンサマイク用（対応 HW のみ） |
| 5 | PAD | 0=Off, 1=-20dB | 入力パッド |
| 6 | IMPEDANCE | インピーダンス値 (Ω) | Hi-Z 切替等 |

#### SetAudioDeviceConfig の非同期実行

サンプルレート変更は重い操作（DMA 停止 → PLL 再構成 → コーデック再設定 → DMA 再開）。
FS syscall と同じ非同期パターンを使う:

```
1. set_audio_device_config(cfg) → 0=受付 / -EBUSY
2. カーネルが DMA 停止 → PLL/コーデック再構成 → DMA 再開
   （この間 process() は呼ばれない）
3. event::audio_config 通知
4. アプリが get_audio_device_info() で新しい設定を確認
```

SetCodecParam（ゲイン変更等）は軽量（I2C 数バイト書き込み）なので、
同期で返しても問題ない。ただし統一性のため非同期にしてもよい。

#### USB Audio ホストからのサンプルレート変更

USB Audio デバイスとして動作する場合、ホスト PC がサンプルレート変更を要求する。
これはカーネルの USB ドライバが処理し、アプリには `event::audio_config` で通知する。
アプリは `get_audio_device_info()` で新しい設定を確認する。

---

## イベントフラグの追加

```cpp
namespace umi::syscall::event {

constexpr uint32_t audio        = (1 << 0);   // オーディオバッファ準備完了（既存）
constexpr uint32_t midi         = (1 << 1);   // MIDI データ利用可能（既存）
constexpr uint32_t vsync        = (1 << 2);   // ディスプレイリフレッシュ（既存）
constexpr uint32_t timer        = (1 << 3);   // タイマーティック（既存）
constexpr uint32_t control      = (1 << 4);   // ControlEvent 到着（既存）
constexpr uint32_t fs           = (1 << 5);   // FS 操作完了（既存）
constexpr uint32_t audio_config = (1 << 6);   // ★ オーディオデバイス設定変更完了
constexpr uint32_t power        = (1 << 7);   // ★ 電源状態変化（バッテリー低下、USB 接続等）
constexpr uint32_t shutdown     = (1u << 31); // シャットダウン要求（既存）

} // namespace umi::syscall::event
```

---

## 再設計後の番号体系

```
  0– 9:  プロセス制御      (0-3 使用、4-9 予約)
 10–19:  時間・電源        (10-15 使用、16-19 予約)    ★ 電源管理追加
 20–29:  構成・パラメータ  (20,25 使用、21-24 廃止予約、26-29 予約)  ★ 21-24 削除
 30–39:  MIDI / SysEx      (30,32-33 使用、31 廃止予約、34-39 予約)  ★ 31 削除
 40–49:  情報・診断        (40-43 使用、44-49 予約)    ★ 診断追加
 50–59:  I/O・ディスプレイ (50-53 使用、54-59 予約)    ★ ディスプレイ追加
 60–89:  ファイルシステム  (60-84 使用、85-89 予約)    変更なし
 90–99:  オーディオデバイス (90-94 使用、95-99 予約)    ★ 新設
100–255: 予約
```

## 全 syscall 一覧（再設計後）

### グループ 0: プロセス制御 (0–9)

| Nr | 名前 | 状態 |
|----|------|------|
| 0 | Exit | 実装済み |
| 1 | Yield | 実装済み |
| 2 | RegisterProc | 実装済み |
| 3 | UnregisterProc | 将来 |

### グループ 1: 時間・スケジューリング・電源 (10–19)

| Nr | 名前 | 状態 |
|----|------|------|
| 10 | WaitEvent | 実装済み |
| 11 | GetTime | 実装済み |
| 12 | Sleep | 実装済み |
| 13 | GetPowerState | **新規** |
| 14 | SetPowerPolicy | **新規** |
| 15 | RequestShutdown | **新規** |

### グループ 2: 構成・パラメータ (20–29)

| Nr | 名前 | 状態 |
|----|------|------|
| 20 | SetAppConfig | 新設計 |
| ~~21~~ | ~~SetRouteTable~~ | **廃止** — SetAppConfig に統合 |
| ~~22~~ | ~~SetParamMapping~~ | **廃止** — SetAppConfig に統合 |
| ~~23~~ | ~~SetInputMapping~~ | **廃止** — SetAppConfig に統合 |
| ~~24~~ | ~~ConfigureInput~~ | **廃止** — SetAppConfig に統合 |
| 25 | SendParamRequest | 将来 |

### グループ 3: MIDI / SysEx (30–39)

| Nr | 名前 | 状態 |
|----|------|------|
| 30 | MidiSend | 将来 |
| ~~31~~ | ~~MidiRecv~~ | **廃止** — EventRouter 経由で代替 |
| 32 | ReadSysex | 将来 |
| 33 | SendSysex | 将来 |

### グループ 4: 情報取得・診断 (40–49)

| Nr | 名前 | 状態 |
|----|------|------|
| 40 | GetShared | 実装済み |
| 41 | GetMetrics | **新規** |
| 42 | GetFaultLog | **新規** |
| 43 | ResetMetrics | **新規** |

### グループ 5: I/O・ディスプレイ (50–59)

| Nr | 名前 | 状態 |
|----|------|------|
| 50 | Log | 実装済み |
| 51 | Panic | 実装済み |
| 52 | GetDisplayInfo | **新規** |
| 53 | SwapDisplayBuffer | **新規** |

### グループ 6: ファイルシステム (60–89)

変更なし。60–84 は現行仕様を維持。

### グループ 9: オーディオデバイス制御 (90–99)

| Nr | 名前 | 状態 |
|----|------|------|
| 90 | GetAudioDeviceInfo | **新規** |
| 91 | GetAudioDeviceCaps | **新規** |
| 92 | SetAudioDeviceConfig | **新規** |
| 93 | SetCodecParam | **新規** |
| 94 | GetCodecParam | **新規** |

---

## 変更の影響

### バックエンド別の対応

| syscall | 組み込み | WASM | Plugin |
|---------|---------|------|--------|
| Audio Device (90-94) | SAI/DMA/I2C 制御 | Web Audio API (setSampleRate 不可、情報取得のみ) | ホスト API 委譲 |
| Power (13-15) | ADC/PMIC/PWR レジスタ | 無操作 (ENOSYS) | 無操作 (ENOSYS) |
| Display (52-53) | SPI/I2C ディスプレイドライバ | Canvas API | プラグイン GUI |
| Diagnostics (41-43) | DWT/SysTick カウンタ | performance.now() ベース | OS プロファイラ |

> WASM/Plugin で非対応の syscall は `ENOSYS` (-38) を返す。
> アプリは `get_audio_device_caps()` 等で能力を確認してからデバイス固有機能を使う。

### 移行手順

1. `syscall_nr.hh` で Nr 21-24, 31 を `_reserved_XX` としてコメント付きで残す
2. `syscall.hh`（アプリ側 API）から対応する関数を削除
3. `syscall_handler.hh` のディスパッチから該当ケースを削除（`INVALID_SYSCALL` を返す）
4. 新規 syscall の番号を追加
5. 各バックエンドで新規 syscall のスタブ実装を追加

---

## 追加しない理由を明記するもの

以下は検討した結果、追加しないと判断したもの。

### USB デバイス制御 syscall

USB Audio/MIDI のデバイス構成はカーネルの責務。アプリが USB エンドポイントを
直接操作する必要はない。USB 接続状態は `event::power` または `GetAudioDeviceInfo` の
フラグで通知すれば十分。

### MIDI ポート列挙・有効化 syscall

MIDI ポートの有効/無効はハードウェア構成に依存し、起動時に確定する。
動的な MIDI ポート管理（BLE MIDI 接続等）が必要になった時点で追加する。

### DWT プロファイリング専用 syscall

`GetMetrics` で process() のサイクル数は取得できる。DWT のイベントカウンタを
個別制御する必要が生じるまでは追加しない。

### オーディオルーティング syscall

USB Audio ↔ コーデック間のルーティングは `SetAudioDeviceConfig` の拡張か、
AppConfig の RouteTable で対応可能。専用 syscall は過剰。

---

## 関連ドキュメント

- [03-port/06-syscall.md](../03-port/06-syscall.md) — 現行 syscall 仕様（本提案で更新対象）
- [01-application/04-param-system.md](../01-application/04-param-system.md) — AppConfig / ParamMapping 設計
- [01-application/08-backend-adapters.md](../01-application/08-backend-adapters.md) — バックエンド別実装方式
- [01-application/10-shared-memory.md](../01-application/10-shared-memory.md) — SharedMemory 構造
- [04-services/13-system-services.md](../04-services/13-system-services.md) — SystemTask サービスアーキテクチャ
- [04-services/19-storage-service.md](../04-services/19-storage-service.md) — FS 非同期モデル（Audio Device と同パターン）
- [04-services/20-diagnostics.md](../04-services/20-diagnostics.md) — KernelMetrics / FaultLog
