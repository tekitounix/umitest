# 17 — Shell

## 概要

SysEx stdio 経由の対話型シェル。デバッグ、設定変更、診断情報の表示を提供する。
SystemTask 上で動作し、USB MIDI SysEx を通じてホスト PC と通信する。

| 項目 | 状態 |
|------|------|
| SysEx stdio 通信 | 実装済み |
| コマンドパーサー | 実装済み |
| アクセス制御（User/Admin/Factory） | 実装済み |
| show コマンド群 | 実装済み |
| config コマンド群 | 実装済み |
| factory コマンド群 | 実装済み |

---

## アーキテクチャ

### 通信経路

```
Host (Web UI / ターミナル)
  │
  │  SysEx STDIN_DATA (0x..., payload)
  ▼
USB MIDI → OTG_FS ISR → SysEx バッファ → notify(SystemTask)
  │
  ▼
SystemTask → Shell::process_input()
  │
  │  SysEx STDOUT_DATA (0x..., payload)
  ▼
USB MIDI → Host
```

Shell は文字単位で入力を処理し、改行で1コマンドを実行する。

### Shell テンプレート

```cpp
template <typename HW, typename Kernel>
class Shell {
    void process_char(char c);        // 1 文字処理
    const char* execute(const char* line);  // コマンド実行
    // ...
private:
    char line_buf[64];                // 行バッファ
    uint8_t line_pos = 0;
};
```

Shell は `HW`（ハードウェア抽象）と `Kernel`（カーネル状態）をテンプレートパラメータとして受け取る。

---

## アクセス制御

### アクセスレベル

| レベル | 権限 | 認証 |
|--------|------|------|
| User | 基本コマンド（help, version, show） | 不要 |
| Admin | 設定変更（config, diag） | パスワード |
| Factory | 全機能（factory test, serial set） | パスワード |

### 認証

```
auth <admin|factory> <password>
```

- セッションタイムアウト: 5 分（無操作時に User に降格）
- ロックアウト: 3 回連続失敗で 30 秒間ロック
- パスワード照合はカーネルのコールバックに委ねる

---

## コマンド一覧

### 基本コマンド（User）

| コマンド | 説明 |
|---------|------|
| `help` / `?` | コマンド一覧 |
| `version` | UMI-OS バージョン表示 |
| `uptime` | アップタイム表示 |
| `whoami` | 現在のアクセスレベル表示 |
| `logout` | User レベルに降格 |

### show コマンド（User）

| コマンド | 説明 | データソース |
|---------|------|------------|
| `show system` | プラットフォーム、バージョン、シリアル、アップタイム | KernelStateView |
| `show cpu` | DSP 負荷 %、ピーク、IRQ 回数 | KernelMetrics |
| `show memory` | ヒープ使用量/ピーク、SRAM、Flash | MemoryUsage |
| `show tasks` | タスクリスト（Total/Ready/Blocked） | Kernel |
| `show audio` | サンプルレート、バッファ数、ドロップ、DSP 負荷、ゲイン | AudioTask 統計 |
| `show midi` | チャンネル、トランスポーズ、RX/TX カウント | MIDI 統計 |
| `show battery` | 電圧、容量 %、充電状態 | 電源管理 |
| `show power` | スリープタイムアウト、バッテリー閾値、USB 状態 | 電源管理 |
| `show usb` | 接続状態、モード、エンドポイント | USB ドライバ |
| `show errors` | エラーログ（最大 16 エントリ） | ErrorLog |
| `show config` | 現在の設定値 | ShellConfig |
| `show mode` | システムモード | SystemMode |

### config コマンド（Admin）

| コマンド | 説明 |
|---------|------|
| `config midi channel <1-16>` | MIDI チャンネル設定 |
| `config midi transpose <-24..24>` | MIDI トランスポーズ |
| `config audio gain <0-100>` | オーディオゲイン |
| `config power sleep <0-60>` | スリープタイムアウト（分） |
| `config save` | 設定を永続化 |
| `config reset` | 設定をデフォルトに戻す |

### MIDI コマンド（User）

| コマンド | 説明 |
|---------|------|
| `midi status` | MIDI 状態表示 |
| `midi monitor [on\|off]` | MIDI モニター切替 |
| `midi panic` | All Notes Off 送信 |

### diag コマンド（Admin）

| コマンド | 説明 |
|---------|------|
| `diag watchdog [feed\|enable\|disable]` | ウォッチドッグ制御 |
| `diag reset [soft\|hard]` | システムリセット |

### factory コマンド（Factory）

| コマンド | 説明 |
|---------|------|
| `factory info` | シリアル番号、製造日、ロック状態 |
| `factory serial [set <sn>\|clear]` | シリアル番号管理 |
| `factory test [all\|...]` | ファクトリーテスト |
| `factory lock` | ファクトリーロック（不可逆） |

### mode コマンド（Admin）

| コマンド | 説明 |
|---------|------|
| `mode` | 現在のモード表示 |
| `mode <normal\|dfu\|bootloader\|safe>` | モード切替 |

---

## StateProvider インターフェース

Shell はカーネル状態を `StateProvider` テンプレートパラメータ経由で取得する:

### KernelStateView

```cpp
struct KernelStateView {
    uint64_t uptime_us;
    uint32_t audio_buffer_count;
    uint32_t audio_drop_count;
    uint32_t dsp_load_percent_x100;     // 100 = 1.00%
    uint32_t midi_rx_count;
    uint32_t midi_tx_count;
    uint8_t  battery_percent;
    bool     battery_charging;
    uint32_t heap_used;
    uint32_t heap_total;
    uint32_t heap_peak;
    uint32_t flash_total;
    uint32_t flash_used;
    uint8_t  task_count;
    uint8_t  task_ready;
    uint8_t  task_blocked;
};
```

### ShellConfig

```cpp
struct ShellConfig {
    uint8_t  midi_channel;          // 1-16
    uint8_t  audio_gain;            // 0-100
    uint8_t  sample_rate;           // kHz 表示用
    int8_t   midi_transpose;        // -24..24
    uint16_t sleep_timeout_min;     // 0 = 無効
};
```

### ErrorLog

```cpp
struct ErrorLogEntry {
    uint64_t timestamp_us;
    uint8_t  severity;       // 0=info, 1=warn, 2=error, 3=critical
    char     message[64];
};
```

リングバッファ（16 エントリ）。`show errors` で最新のエントリから表示する。

---

## 出力フォーマット

Shell の出力はプレーンテキスト。SysEx STDOUT_DATA でホストに送信される。
出力バッファは最大 2048 バイト。長い出力は切り詰められる。

```
> show system
Platform: STM32F407VG
Version:  UMI-OS v2.0.0
Serial:   UMI-00001
Uptime:   1h 23m 45s
Tasks:    4 (Ready: 1, Blocked: 3)
>
```

---

## 実装ファイル

| ファイル | 内容 |
|---------|------|
| `lib/umios/kernel/umi_shell.hh` | Shell テンプレート（入力処理、行バッファ） |
| `lib/umios/kernel/shell_commands.hh` | ShellCommands テンプレート（全コマンド実装） |

---

## 関連ドキュメント

- [05-midi.md](05-midi.md) — SysEx トランスポート
- [13-system-services.md](13-system-services.md) — SystemTask でのディスパッチ
- [18-updater.md](18-updater.md) — DFU モード（`mode dfu` で遷移）
- [20-diagnostics.md](20-diagnostics.md) — show コマンドのデータソース
