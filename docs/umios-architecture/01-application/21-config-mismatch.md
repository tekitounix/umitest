# 21: コンフィグ不整合の処理

## 概要

アプリケーションの `AppConfig` がボードの物理的能力と一致しない場合の動作を定義する。

基本方針: **不整合は致命的エラーではない。部分適用して動作を継続する。**

これにより、ひとつの `constexpr AppConfig` を複数のボードで共有でき、
ボードごとにアプリケーションを書き分ける必要がなくなる。

---

## 不整合のパターン

### パターン1: ハードウェア入力の不在

アプリが `input_id=5` のノブにパラメータをマッピングしたが、ボードにはノブが2つしかない。

```
AppConfig:
  inputs[5] = { .input_id = 5, .mode = PARAM_AND_EVENT, ... }
  input_mapping[5] = { .param_id = CUTOFF, ... }

Board:
  num_knobs = 2  (input_id 0, 1 のみ)
```

### パターン2: パラメータIDの範囲超過

`ParamMapEntry.param_id` が Processor の `params()` で宣言された範囲を超えている。

### パターン3: ハードウェア出力の不在

アプリが RGB LED 4個を期待しているが、ボードには2個しかない。

### パターン4: オーディオチャネル数の不一致

アプリが4ch入力を期待しているが、ボードは2ch。

### パターン5: MIDIポートの不在

アプリが UART MIDI を期待しているが、ボードには USB MIDI しかない。

---

## BoardCapability

ボード層（`board/bsp.hh`）が提供するコンパイル時定数。
既存の `BoardSpec` concept を拡張し、入出力の物理構成を宣言する。

```cpp
// board/<target>/board/bsp.hh に追加

namespace umi::board {

struct Capability {
    // --- 入力 ---
    uint8_t num_knobs;          ///< アナログノブ数
    uint8_t num_buttons;        ///< ボタン数（エンコーダのクリック含む）
    uint8_t num_encoders;       ///< ロータリーエンコーダ数
    uint8_t num_cv_inputs;      ///< CV入力数

    // --- 出力 ---
    uint8_t num_leds;           ///< 単色LED数
    uint8_t num_rgb_leds;       ///< RGB LED数
    uint8_t num_cv_outputs;     ///< CV出力数

    // --- オーディオ ---
    uint8_t audio_channels_in;  ///< オーディオ入力チャネル数
    uint8_t audio_channels_out; ///< オーディオ出力チャネル数

    // --- MIDI ---
    bool has_usb_midi;
    bool has_uart_midi;

    // --- ストレージ ---
    bool has_sdcard;
    bool has_qspi;

    /// 指定 input_id がこのボードで有効かどうか
    /// input_id の割り当て規則:
    ///   0 ~ num_knobs-1:                       ノブ
    ///   num_knobs ~ num_knobs+num_buttons-1:   ボタン
    ///   以降:                                   エンコーダ、CV
    constexpr bool has_input(uint8_t input_id) const noexcept {
        return input_id < total_inputs();
    }

    constexpr uint8_t total_inputs() const noexcept {
        return num_knobs + num_buttons + num_encoders + num_cv_inputs;
    }
};

} // namespace umi::board
```

### 各ボードの宣言例

```cpp
// board/daisy_pod/board/bsp.hh
namespace umi::board {
inline constexpr Capability capability = {
    .num_knobs       = 2,
    .num_buttons     = 2,
    .num_encoders    = 1,
    .num_cv_inputs   = 0,
    .num_leds        = 1,    // Seed LED
    .num_rgb_leds    = 2,
    .num_cv_outputs  = 0,
    .audio_channels_in  = 2,
    .audio_channels_out = 2,
    .has_usb_midi    = true,
    .has_uart_midi   = true,
    .has_sdcard      = true,
    .has_qspi        = true,
};
} // namespace umi::board

// board/stm32f4_disco/board/bsp.hh
namespace umi::board {
inline constexpr Capability capability = {
    .num_knobs       = 0,
    .num_buttons     = 1,    // User button (PA0)
    .num_encoders    = 0,
    .num_cv_inputs   = 0,
    .num_leds        = 4,    // LD3-LD6
    .num_rgb_leds    = 0,
    .num_cv_outputs  = 0,
    .audio_channels_in  = 2,
    .audio_channels_out = 2,
    .has_usb_midi    = true,
    .has_uart_midi   = false,
    .has_sdcard      = false,
    .has_qspi        = false,
};
} // namespace umi::board
```

---

## SetAppConfig の不整合処理

### 処理フロー

```
SetAppConfig(config)
    │
    ▼
OS: inactive buffer に config をコピー (既存動作)
    │
    ▼
OS: BoardCapability と照合 ─── 検証フェーズ (新規)
    │
    ├─ inputs[i] の input_id が has_input() == false
    │   → mode を DISABLED に上書き
    │   → disabled_inputs ビットにセット
    │
    ├─ input_mapping[i] が無効な input_id を参照
    │   → param_id を 0xFF (unmapped) に上書き
    │   → disabled_inputs ビットにセット
    │
    ├─ param_mapping[cc].param_id >= MAX_PARAMS
    │   → param_id を 0xFF に上書き (既存のバウンドチェックと同等)
    │
    └─ (その他のフィールドはそのまま適用)
    │
    ▼
OS: ブロック境界で active/inactive スワップ (既存動作)
    │
    ▼
OS: 戻り値 AppConfigResult を返す (新規)
```

### AppConfigResult

```cpp
/// SetAppConfig の適用結果
struct AppConfigResult {
    int32_t error;              ///< 0 = OK, 負値 = SyscallError
    uint16_t disabled_inputs;   ///< ビットマスク: 無効化された input_id (最大16)
    uint16_t _reserved;
};
```

SetAppConfig syscall の戻り値は従来通り `int32_t` (0=OK) だが、
詳細な適用結果が必要な場合は別途 `get_config_result()` syscall で取得する。
これにより既存の syscall ABI を変えずに済む。

```cpp
// syscall_nr.hh — Group 2 の空き番号を使用
inline constexpr uint32_t get_config_result = 26;

// syscall.hh
inline AppConfigResult get_config_result() noexcept {
    AppConfigResult result;
    call(nr::get_config_result,
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&result)));
    return result;
}
```

### 戻り値の使用パターン

```cpp
// パターン A: 結果を無視する（最も一般的）
// 存在しない入力は暗黙的に DISABLED — アプリは何もしなくてよい
umi::syscall::set_app_config(&SYNTH_CONFIG);

// パターン B: 結果を確認する（開発時・デバッグ時）
umi::syscall::set_app_config(&SYNTH_CONFIG);
auto result = umi::syscall::get_config_result();
if (result.disabled_inputs != 0) {
    // 例: "inputs 2,3,4 disabled (not available on this board)"
    // SysEx経由のdiagnosticログに出力
}

// パターン C: ボード能力に応じて構成を切り替える
auto cap = umi::syscall::get_board_capability();
if (cap.num_knobs >= 4) {
    umi::syscall::set_app_config(&FULL_CONFIG);
} else {
    umi::syscall::set_app_config(&MINIMAL_CONFIG);
}
```

---

## get_board_capability syscall

アプリケーションが実行時にボードの物理構成を照会する。

```cpp
// syscall_nr.hh — Group 4 (Info) の空き番号を使用
inline constexpr uint32_t get_board_capability = 41;

// syscall.hh
inline BoardCapability get_board_capability() noexcept {
    BoardCapability cap;
    call(nr::get_board_capability,
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&cap)));
    return cap;
}
```

カーネル側の実装は単純:
コンパイル時に確定している `umi::board::capability` を、アプリの提供するバッファにコピーするだけ。

```cpp
// kernel syscall handler
case nr::get_board_capability: {
    auto* dst = reinterpret_cast<BoardCapability*>(r0);
    if (is_app_writable(dst, sizeof(BoardCapability))) {
        *dst = umi::board::capability;
        result = 0;
    } else {
        result = static_cast<int32_t>(SyscallError::ACCESS_DENIED);
    }
    break;
}
```

---

## パラメータのデフォルト値フォールバック

存在しない入力にマッピングされたパラメータは、
EventRouter から値が書き込まれることがない。
したがって **SharedParamState の該当スロットは ParamDescriptor の default_value のまま残る**。

このフォールバックは自然に成立する:

1. `register_proc` 時に OS が `Processor::params()` を読み取る
2. 各 `ParamDescriptor::default_value` で `SharedParamState::values[]` を初期化する
3. 存在しない入力は DISABLED → EventRouter が値を書き込まない → default_value が維持される

アプリ側は `process()` で `ctx.params.values[PARAM_CUTOFF]` を読むだけでよく、
その値がノブから来たのかデフォルト値なのかを区別する必要はない。

---

## WASMバックエンドでの扱い

WASM バックエンドではハードウェア入力が存在しないため、
`BoardCapability` は全入力が0になる:

```cpp
// platform/wasm での BoardCapability
inline constexpr Capability capability = {
    .num_knobs = 0, .num_buttons = 0, .num_encoders = 0,
    .num_cv_inputs = 0, .num_leds = 0, .num_rgb_leds = 0,
    .num_cv_outputs = 0,
    .audio_channels_in = 2, .audio_channels_out = 2,
    .has_usb_midi = true,   // Web MIDI API
    .has_uart_midi = false, .has_sdcard = false, .has_qspi = false,
};
```

この場合、全 `InputConfig` が DISABLED になり、パラメータは:
- MIDI CC 経由で操作される（ParamMapping は input_id に依存しない）
- Web UI 経由で `send_param_request` syscall により直接設定される
- default_value のまま（UI もコントローラもない場合）

つまり、同じ AppConfig を WASM に適用しても、ノブ関連が無視されるだけで正常に動作する。

---

## Pluginバックエンドでの扱い

VST3/AU/CLAP プラグインバックエンドでは、
ホスト DAW がパラメータ自動化を提供するため、ハードウェア入力は不要。

ホストからのパラメータ変更は `send_param_request` 相当の経路で
`SharedParamState` に直接書き込まれる。
`InputConfig` / `InputParamMapping` は全て無視される（WASM と同様）。

---

## 設計判断

### なぜ致命的エラーにしないのか

1. **One-source multi-target の原則**: 同一のアプリバイナリが異なるボードで動作すべき
2. **Graceful degradation**: ノブがなくても MIDI CC でパラメータ制御は可能
3. **開発体験**: ボードが手元にない状態でもアプリ開発を進められる
4. **フォールバックの自然さ**: default_value が使われるだけで、未定義動作にならない

### なぜ AppConfigResult を別 syscall にするのか

1. **責務の分離**: 設定の適用と診断情報の照会は別の操作。`setsockopt`/`getsockopt`、`ioctl` の SET/GET と同じ原則。設定 syscall に診断情報を混ぜる OS API は一般的に存在しない
2. **大半のアプリは結果を確認しない**: 別 syscall なら呼ばなければコスト0。不要な情報を常に返すのは無駄であり、API の意味も曖昧になる
3. **r0+r1 パターンは避けるべき**: `get_time_usec` は 64bit 値を返すという避けられない理由があるが、`set_app_config` は本質的に成功/失敗を返す syscall であり例外にする理由がない
4. **外部からの照会**: 適用結果がカーネル内に保持されるため、OS 側のシェルや診断ツール（SysEx 経由）からもカーネル内部データとして参照できる。アプリが結果を無視しても、開発者がログで不整合を確認できる

### なぜ BoardCapability を Concept にしないのか

`BoardCapability` はアプリ側から **実行時に** 読み取る構造体であり、
コンパイル時のインターフェース契約（Concept）とは目的が異なる。
ボード層では `constexpr` 定数として定義されるが、syscall 経由でアプリに渡すため
POD 構造体である必要がある。

---

## 不整合パターンの完全マトリクス

| 不整合 | OS の処理 | SharedParamState | アプリへの影響 |
|--------|-----------|------------------|---------------|
| 存在しない input_id のマッピング | InputConfig.mode → DISABLED | default_value 維持 | ノブが効かないだけ |
| param_id > MAX_PARAMS | param_id → 0xFF (unmapped) | 変化なし | CC が効かないだけ |
| audio_channels_in 不足 | 不足チャネルはゼロバッファ | — | 無音入力 |
| audio_channels_out 超過 | 超過チャネルは無視 | — | 出力されないだけ |
| UART MIDI なし | UART 関連ルートは到達しない | — | USB MIDI のみで動作 |
| SD カードなし | FS syscall が NOT_FOUND を返す | — | FS 機能不可 |
| QSPI なし | アプリロード不可（カーネルのみ） | — | フォールバックシンセ |

全てのケースで **アプリは crash せず、可能な範囲で動作を継続する**。

---

## 実装優先度

| 項目 | 優先度 | 理由 |
|------|--------|------|
| `BoardCapability` 構造体定義 | **高** | 各ボード BSP に追加するだけ |
| `SetAppConfig` の検証フェーズ | **高** | 既存の inactive buffer コピー後に追加 |
| `get_board_capability` syscall | **中** | アプリの適応パターンで必要 |
| `get_config_result` syscall | **低** | デバッグ用、初期実装では省略可 |

---

## 関連ドキュメント

| # | ドキュメント | 関連 |
|---|-------------|------|
| 04 | [param-system](04-param-system.md) | ParamDescriptor, ParamMapping, SharedParamState |
| 06 | [syscall](../03-port/06-syscall.md) | syscall ABI, Group 2 (Configuration), Group 4 (Info) |
| 08 | [backend-adapters](08-backend-adapters.md) | WASM / Plugin バックエンドの差異 |
| 10 | [shared-memory](10-shared-memory.md) | SharedParamState のレイアウトと初期化 |
| 13 | [system-services](../04-services/13-system-services.md) | EventRouter の役割 |
