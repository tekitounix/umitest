# UMI デバッグガイド

## 概要

本ドキュメントは、UMI プロジェクトの開発時におけるデバッグ手順をまとめたものです。
macOS 環境でコマンドラインベースで完結する手法を中心に解説します。

**注意**: AI エージェントによる自動デバッグを行う場合は、[AI 自動化向けデバッグ](#ai-自動化向けデバッグ) セクションを参照してください。インタラクティブなツールはハングの原因となります。

---

## デバッグツール

### 必要なツール

| ツール | 用途 | インストール |
|--------|------|--------------|
| pyOCD | フラッシュ、GDBサーバ、RTT | `pip install pyocd` |
| arm-none-eabi-gdb | ARMデバッグ | `brew install arm-none-eabi-gdb` |

pyOCD でフラッシュ書き込み、デバッガ接続、RTT すべて対応可能です。

### ターゲット設定

```bash
# 接続確認
pyocd list

# STM32F4-Discovery の場合
pyocd pack install stm32f407vg
```

---

## フラッシュ書き込み

### xmake によるフラッシュ

```bash
# Release ビルド & フラッシュ
xmake config -m release && xmake build stm32f4_synth && xmake flash -t stm32f4_synth
```

### pyOCD による書き込み

```bash
# フラッシュ書き込み
pyocd flash -t stm32f407vg build/stm32f4_synth/release/stm32f4_synth.bin

# チップ消去
pyocd erase -t stm32f407vg --chip

# リセット
pyocd reset -t stm32f407vg
```

---

## GDB デバッグ

### GDB サーバ起動と接続

```bash
# ターミナル1: GDBサーバ起動
pyocd gdbserver -t stm32f407vg

# ターミナル2: GDB接続
arm-none-eabi-gdb build/stm32f4_synth/release/stm32f4_synth
(gdb) target remote :3333
(gdb) monitor reset halt
(gdb) load
(gdb) continue
```

### よく使う GDB コマンド

```gdb
# ブレークポイント
break main
break process
break *0x08001234

# 実行制御
continue
step
next
finish

# 変数・メモリ
print variable
x/10xw 0x20000000

# レジスタ
info registers
info registers pc sp

# バックトレース
backtrace
frame 2

# リセット
monitor reset halt
```

---

## RTT (Real-Time Transfer)

```bash
# RTT 出力を表示
pyocd rtt -t stm32f407vg
```

RTT はデバッグ中のログ出力に最適です。UART と異なり追加の配線不要で高速です。

---

## AI 自動化向けデバッグ

AI エージェント（Claude Code 等）によるデバッグ自動化では、以下の問題が頻発します：

- **インタラクティブツールのハング**: GDB、Renode 等が入力待ちで停止
- **デバッガの不正終了**: ST-Link 等が占有されたまま残り、次回接続不可
- **タイムアウト不足**: 処理が終わらず永久待機

これらを回避するため、**pyOCD Python API** を使用した非インタラクティブなデバッグを推奨します。

### 安全なデバッグセッション

タイムアウトとクリーンアップを保証するコンテキストマネージャを使用します。

```python
# tools/debug/session.py
from contextlib import contextmanager
from pyocd.core.helpers import ConnectHelper
import signal

class DebugTimeout(Exception):
    """デバッグセッションのタイムアウト"""
    pass

@contextmanager
def safe_debug_session(target='stm32f407vg', timeout=60):
    """
    タイムアウトとクリーンアップを保証するデバッグセッション

    Args:
        target: ターゲットデバイス名
        timeout: タイムアウト秒数

    Usage:
        with safe_debug_session('stm32f407vg', timeout=30) as target:
            target.reset_and_halt()
            pc = target.read_core_register('pc')
    """
    session = None

    def alarm_handler(signum, frame):
        raise DebugTimeout(f"Debug session timed out after {timeout}s")

    old_handler = signal.signal(signal.SIGALRM, alarm_handler)
    signal.alarm(timeout)

    try:
        session = ConnectHelper.session_with_chosen_probe(
            target_override=target,
            options={'connect_mode': 'under-reset'}
        )
        session.open()
        yield session.target
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, old_handler)
        if session:
            try:
                session.close()
            except Exception:
                pass  # クローズ時のエラーは無視
```

### デバッガクリーンアップ

セッション開始前に残留プロセスを処理します。

```python
# tools/debug/cleanup.py
import subprocess
import time

def cleanup_debugger():
    """残留デバッガプロセスを終了"""
    subprocess.run(['pkill', '-f', 'pyocd'], capture_output=True)
    subprocess.run(['pkill', '-f', 'openocd'], capture_output=True)
    subprocess.run(['pkill', '-f', 'arm-none-eabi-gdb'], capture_output=True)
    time.sleep(0.5)

def verify_probe_available():
    """プローブが利用可能か確認"""
    result = subprocess.run(
        ['pyocd', 'list'],
        capture_output=True,
        text=True,
        timeout=10
    )
    return 'No available' not in result.stdout

def ensure_clean_state():
    """デバッグ開始前のクリーンアップ"""
    cleanup_debugger()
    if not verify_probe_available():
        raise RuntimeError("Debug probe not available after cleanup")
```

### subprocess によるコマンド実行

CLI ツールを安全に呼び出す場合はタイムアウトを必ず指定します。

```python
# tools/debug/commands.py
import subprocess
from typing import Optional, Tuple

def run_pyocd_command(args: list, timeout: int = 30) -> Tuple[Optional[str], int]:
    """
    タイムアウト付きで pyOCD コマンドを実行

    Returns:
        (stdout, return_code) - タイムアウト時は (None, -1)
    """
    try:
        result = subprocess.run(
            ['pyocd'] + args,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        return result.stdout, result.returncode
    except subprocess.TimeoutExpired:
        return None, -1

def flash_firmware(firmware_path: str, target: str = 'stm32f407vg') -> bool:
    """ファームウェアをフラッシュ（タイムアウト60秒）"""
    stdout, rc = run_pyocd_command(
        ['flash', '-t', target, firmware_path],
        timeout=60
    )
    return rc == 0

def reset_target(target: str = 'stm32f407vg') -> bool:
    """ターゲットをリセット"""
    stdout, rc = run_pyocd_command(['reset', '-t', target], timeout=10)
    return rc == 0
```

### Robot Framework によるテスト自動化

複雑なテストシナリオには Robot Framework を使用します。

```robot
# tests/hardware/flash_test.robot
*** Settings ***
Library    ../../tools/debug/robot_keywords.py
Test Timeout    120 seconds

*** Test Cases ***
Flash And Run Synth
    [Documentation]    ファームウェアをフラッシュして動作確認
    [Timeout]    60 seconds
    Cleanup Debugger
    Connect Target    stm32f407vg
    Flash Firmware    build/stm32f4_synth/release/stm32f4_synth.bin
    Reset And Run
    Sleep    2s
    Verify Running
    [Teardown]    Disconnect Target

Memory Read Test
    [Documentation]    メモリ読み取りテスト
    [Timeout]    30 seconds
    Cleanup Debugger
    Connect Target    stm32f407vg
    ${data}=    Read Memory    0x08000000    16
    Should Not Be Empty    ${data}
    [Teardown]    Disconnect Target
```

```python
# tools/debug/robot_keywords.py
from robot.api.deco import keyword
from pyocd.core.helpers import ConnectHelper
import subprocess

class RobotKeywords:
    ROBOT_LIBRARY_SCOPE = 'TEST'

    def __init__(self):
        self.session = None

    @keyword
    def cleanup_debugger(self):
        """残留デバッガプロセスをクリーンアップ"""
        subprocess.run(['pkill', '-f', 'pyocd'], capture_output=True)
        subprocess.run(['pkill', '-f', 'openocd'], capture_output=True)

    @keyword
    def connect_target(self, target):
        """ターゲットに接続"""
        self.session = ConnectHelper.session_with_chosen_probe(
            target_override=target,
            options={'connect_mode': 'under-reset'}
        )
        self.session.open()

    @keyword
    def disconnect_target(self):
        """ターゲットから切断"""
        if self.session:
            self.session.close()
            self.session = None

    @keyword
    def flash_firmware(self, path):
        """ファームウェアをフラッシュ"""
        if not self.session:
            raise RuntimeError("Not connected")
        self.session.target.reset_and_halt()
        self.session.board.flash.program(path)

    @keyword
    def reset_and_run(self):
        """リセットして実行開始"""
        if not self.session:
            raise RuntimeError("Not connected")
        self.session.target.reset()

    @keyword
    def read_memory(self, address, length):
        """メモリを読み取り"""
        if not self.session:
            raise RuntimeError("Not connected")
        addr = int(address, 16) if isinstance(address, str) else address
        return self.session.target.read_memory_block8(addr, int(length))

    @keyword
    def verify_running(self):
        """ターゲットが実行中か確認"""
        if not self.session:
            raise RuntimeError("Not connected")
        state = self.session.target.get_state()
        if state != 'running':
            raise AssertionError(f"Expected running, got {state}")
```

### 手法の比較

| 手法 | タイムアウト | クリーンアップ | AI向け適性 | 用途 |
|------|------------|--------------|-----------|------|
| pyOCD Python API | ◎ 自前実装 | ◎ try/finally | ◎ 最適 | 汎用デバッグ |
| Robot Framework | ◎ 組み込み | ◎ Teardown | ○ | 定型テスト |
| subprocess | ◎ 引数指定 | ○ 自動終了 | ○ | CLI 呼び出し |
| GDB インタラクティブ | × | × | × 使用禁止 | 手動のみ |

### AI エージェント向けのベストプラクティス

1. **インタラクティブツールを避ける**: GDB、Renode CLI は使用しない
2. **必ずタイムアウトを設定**: 最大でも 60-120 秒
3. **セッション前にクリーンアップ**: `cleanup_debugger()` を実行
4. **コンテキストマネージャを使用**: `with safe_debug_session() as target:`
5. **エラー時も確実に解放**: `try/finally` または Teardown

---

## Python スクリプトによるデバッグ（手動用）

pyOCD は Python ライブラリとしても使用でき、複雑なデバッグ自動化が可能です。

> **注意**: AI エージェントによる自動化では、上記の [AI 自動化向けデバッグ](#ai-自動化向けデバッグ) セクションのラッパーを使用してください。

### 基本的な使用例

```python
from pyocd.core.helpers import ConnectHelper

# ターゲットに接続
with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
    target = session.target

    # リセット & 停止
    target.reset_and_halt()

    # メモリ読み書き
    data = target.read_memory_block8(0x20000000, 256)
    target.write_memory_block8(0x20000100, [0x01, 0x02, 0x03])

    # レジスタアクセス
    pc = target.read_core_register('pc')
    target.write_core_register('r0', 0x12345678)

    # 実行再開
    target.resume()
```

### 自動テスト例

```python
from pyocd.core.helpers import ConnectHelper
import time

def test_audio_init():
    """オーディオ初期化の自動テスト"""
    with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
        target = session.target
        target.reset_and_halt()

        # main まで実行
        target.set_breakpoint(target.elf.symbols['main'].address)
        target.resume()
        while target.get_state() != 'halted':
            time.sleep(0.01)

        # 特定のレジスタ/メモリ値を検証
        # ...

        target.remove_breakpoint(target.elf.symbols['main'].address)
```

---

## USB デバッグ

### USB デバイス確認

```bash
system_profiler SPUSBDataType
```

### USB ログ確認

```bash
# Console.app で USB 関連ログ確認
log show --predicate 'subsystem == "com.apple.usb"' --last 5m
```

---

## オーディオデバッグ

### デバイス確認

```bash
system_profiler SPAudioDataType
```

### SoX による録音・解析

```bash
# インストール
brew install sox

# 録音（5秒）
sox -d test.wav trim 0 5

# 波形解析
sox test.wav -n stat
sox test.wav -n spectrogram -o spec.png
sox test.wav -n stats
```

### Audio MIDI Setup

```bash
open /Applications/Utilities/Audio\ MIDI\ Setup.app
```

---

## MIDI デバッグ

### sendmidi / receivemidi

```bash
# インストール
brew install sendmidi receivemidi

# MIDI 受信モニタ
receivemidi dev "UMI USB MIDI"

# MIDI 送信
sendmidi dev "UMI USB MIDI" on 60 100   # Note On C4
sendmidi dev "UMI USB MIDI" off 60 0    # Note Off C4
```

---

## Renode シミュレーション

実機なしでのテスト・デバッグが可能です。

### 基本実行（手動）

```bash
renode tools/renode/synth.resc
```

### インタラクティブモード（手動のみ）

> **警告**: AI エージェントではインタラクティブモードを使用しないでください。

```bash
renode tools/renode/synth_interactive.resc

# Renode CLI
(machine-0) sysbus.cpu PC
(machine-0) sysbus.cpu Step 100
(machine-0) sysbus ReadDoubleWord 0x40000000
```

### GDB 接続（手動のみ）

> **警告**: AI エージェントでは GDB 接続を使用しないでください。

```bash
# Renode 側で GDB サーバ起動
(machine-0) machine StartGdbServer 3333

# GDB 接続
arm-none-eabi-gdb build/stm32f4_synth/release/stm32f4_synth
(gdb) target remote :3333
```

### Robot Framework による自動テスト（AI 向け）

Renode は Robot Framework と統合されており、非インタラクティブなテストが可能です。

```robot
# tests/simulation/synth_test.robot
*** Settings ***
Resource    ${RENODEKEYWORDS}
Suite Setup    Setup
Suite Teardown    Teardown
Test Timeout    60 seconds

*** Keywords ***
Setup
    Execute Command    mach create
    Execute Command    machine LoadPlatformDescription @platforms/boards/stm32f4_discovery.repl
    Execute Command    sysbus LoadELF @build/stm32f4_synth/release/stm32f4_synth

Teardown
    Execute Command    mach clear

*** Test Cases ***
Should Boot Successfully
    [Documentation]    ファームウェアが正常に起動することを確認
    [Timeout]    30 seconds
    Execute Command    cpu Step 10000
    ${pc}=    Execute Command    cpu PC
    Should Not Be Equal    ${pc}    0x0

Memory Should Be Initialized
    [Documentation]    メモリが初期化されていることを確認
    Execute Command    cpu Step 5000
    ${value}=    Execute Command    sysbus ReadDoubleWord 0x20000000
    Should Not Be Empty    ${value}
```

実行方法：

```bash
# Renode の Robot テスト実行
renode-test tests/simulation/synth_test.robot
```

### 詳細
- [SIMULATION.md](SIMULATION.md) - シミュレーション詳細

---

## トラブルシューティング

### デバッガが接続できない

```bash
# 接続確認
pyocd list

# ターゲットが見つからない場合はパックをインストール
pyocd pack install stm32f407vg

# リセット
pyocd reset -t stm32f407vg
```

### デバッガが占有されたまま（AI 自動化でよくある問題）

前回のセッションが正常終了しなかった場合に発生します。

```bash
# 残留プロセスを強制終了
pkill -f pyocd
pkill -f openocd
pkill -f arm-none-eabi-gdb

# 少し待ってから再接続
sleep 1
pyocd list
```

Python スクリプトから：

```python
from tools.debug.cleanup import ensure_clean_state
ensure_clean_state()  # クリーンアップ + 確認
```

### フラッシュ書き込みエラー

```bash
# チップ消去後に再試行
pyocd erase -t stm32f407vg --chip
pyocd flash -t stm32f407vg firmware.bin
```

### USB Audio が認識されない

1. USB ケーブル確認（データ対応か）
2. Console.app でエラー確認
3. USB ディスクリプタの検証

```bash
log show --predicate 'subsystem == "com.apple.usb"' --last 5m
```

### AI エージェントがハングした場合

インタラクティブツールを誤って起動した場合：

```bash
# すべての関連プロセスを終了
pkill -f pyocd
pkill -f openocd
pkill -f arm-none-eabi-gdb
pkill -f renode

# Claude Code の場合はセッションを再起動
```

---

## USB オーディオテスト

### サンプルレート変更 (macOS CoreAudio)

macOS CoreAudio API を ctypes で呼び出してサンプルレートを変更できます。

```python
#!/usr/bin/env python3
"""macOS CoreAudio sample rate setter using ctypes"""

import ctypes
import sys

# Load CoreAudio and CoreFoundation frameworks
coreaudio = ctypes.CDLL('/System/Library/Frameworks/CoreAudio.framework/CoreAudio')
cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')

# Define function signatures
coreaudio.AudioObjectGetPropertyDataSize.argtypes = [
    ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint32)
]
coreaudio.AudioObjectGetPropertyDataSize.restype = ctypes.c_int32

coreaudio.AudioObjectGetPropertyData.argtypes = [
    ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p
]
coreaudio.AudioObjectGetPropertyData.restype = ctypes.c_int32

coreaudio.AudioObjectSetPropertyData.argtypes = [
    ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
    ctypes.c_uint32, ctypes.c_void_p
]
coreaudio.AudioObjectSetPropertyData.restype = ctypes.c_int32

# CoreAudio constants (FourCC codes)
kAudioHardwarePropertyDevices = 0x64657623        # 'dev#'
kAudioObjectSystemObject = 1
kAudioDevicePropertyNominalSampleRate = 0x6e737274  # 'nsrt'
kAudioObjectPropertyScopeGlobal = 0x676c6f62      # 'glob'
kAudioObjectPropertyScopeOutput = 0x6f757470      # 'outp'
kAudioObjectPropertyElementMain = 0
kAudioObjectPropertyName = 0x6c6e616d             # 'lnam'

class AudioObjectPropertyAddress(ctypes.Structure):
    _fields_ = [
        ('mSelector', ctypes.c_uint32),
        ('mScope', ctypes.c_uint32),
        ('mElement', ctypes.c_uint32),
    ]

def get_device_name(dev_id):
    """デバイス名を取得"""
    addr = AudioObjectPropertyAddress(
        kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, 0)
    name_ref = ctypes.c_void_p()
    size = ctypes.c_uint32(8)
    if coreaudio.AudioObjectGetPropertyData(
            dev_id, ctypes.byref(addr), 0, None,
            ctypes.byref(size), ctypes.byref(name_ref)) != 0:
        return None
    cf.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
    buf = ctypes.create_string_buffer(256)
    cf.CFStringGetCString(name_ref, buf, 256, 0x08000100)  # UTF-8
    cf.CFRelease(name_ref)
    return buf.value.decode('utf-8')

def get_all_devices():
    """全オーディオデバイスIDを取得"""
    addr = AudioObjectPropertyAddress(
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, 0)
    size = ctypes.c_uint32()
    coreaudio.AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject, ctypes.byref(addr), 0, None, ctypes.byref(size))
    num_devices = size.value // 4
    devices = (ctypes.c_uint32 * num_devices)()
    coreaudio.AudioObjectGetPropertyData(
        kAudioObjectSystemObject, ctypes.byref(addr), 0, None, ctypes.byref(size), devices)
    return list(devices)

def get_sample_rate(dev_id):
    """現在のサンプルレートを取得"""
    addr = AudioObjectPropertyAddress(
        kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, 0)
    rate = ctypes.c_double()
    size = ctypes.c_uint32(8)
    if coreaudio.AudioObjectGetPropertyData(
            dev_id, ctypes.byref(addr), 0, None, ctypes.byref(size), ctypes.byref(rate)) != 0:
        return None
    return rate.value

def set_sample_rate(dev_id, rate):
    """サンプルレートを設定（Output scope を優先）"""
    for scope in [kAudioObjectPropertyScopeOutput, kAudioObjectPropertyScopeGlobal]:
        addr = AudioObjectPropertyAddress(kAudioDevicePropertyNominalSampleRate, scope, 0)
        new_rate = ctypes.c_double(float(rate))
        result = coreaudio.AudioObjectSetPropertyData(
            ctypes.c_uint32(dev_id), ctypes.byref(addr), 0, None, 8, ctypes.byref(new_rate))
        if result == 0:
            actual = get_sample_rate(dev_id)
            if actual and abs(actual - rate) < 100:
                return True
    return False

def find_device(name_pattern):
    """名前でデバイスを検索"""
    for dev_id in get_all_devices():
        name = get_device_name(dev_id)
        if name and name_pattern in name:
            return dev_id, name
    return None, None

# 使用例
if __name__ == "__main__":
    dev_id, name = find_device("UMI")
    if dev_id:
        print(f"Found: {name} (ID: {dev_id})")
        print(f"Current rate: {get_sample_rate(dev_id):.0f} Hz")
        if set_sample_rate(dev_id, 96000):
            print(f"Changed to: {get_sample_rate(dev_id):.0f} Hz")
```

**使用例**:
```bash
# デバイス一覧
python3 -c "
import ctypes
# ... (上記コードを使用)
for dev_id in get_all_devices():
    name = get_device_name(dev_id)
    rate = get_sample_rate(dev_id)
    if name and rate:
        print(f'[{dev_id}] {name}: {rate:.0f} Hz')
"

# サンプルレート変更
python3 -c "
# ... (上記コードを使用)
dev_id, _ = find_device('UMI')
set_sample_rate(dev_id, 96000)
"
```

**注意事項**:
- デバイスが音声再生中の場合、変更が失敗することがあります
- 変更失敗時は数秒待ってから再試行してください
- Music アプリ等がデバイスを使用中だと変更できません

### 音声再生テスト

```bash
# システムサウンドを再生
afplay /System/Library/Sounds/Ping.aiff
afplay /System/Library/Sounds/Submarine.aiff

# 音声合成で再生テスト（長めのテスト向け）
say "one two three four five six seven eight nine ten"
```

### デバッグ変数の確認（再生中に実行）

音声再生**中**にデバッグ変数を確認することが重要です。再生終了後はバッファが空になります。

```bash
# 再生中にデバッグ変数を読み取る例
say "one two three four five" & sleep 0.5 && pyocd cmd -c "read32 0x20000e0c; read32 0x20000e28"
```

### 主要デバッグ変数

| アドレス | 変数名 | 説明 |
|----------|--------|------|
| 0x20000000 | dbg_current_sample_rate | 現在のサンプルレート |
| 0x20000e0c | dbg_underrun | Underrun カウント |
| 0x20000e10 | dbg_streaming | ストリーミング状態 (0/1) |
| 0x20000e14 | dbg_overrun | Overrun カウント |
| 0x20000e18 | dbg_usb_rx_count | USB 受信パケット数 |
| 0x20000e1c | dbg_feedback | Feedback 値 (10.14 format) |
| 0x20000e20 | dbg_actual_rate | 実際の I2S レート |
| 0x20000e28 | dbg_out_buf_level | 出力バッファレベル |
| 0x20000e30 | dbg_sample_rate_change_count | サンプルレート変更回数 |
| 0x20000e38 | dbg_sr_set_cur | SET CUR リクエスト回数 |

**アドレスの取得方法**:
```bash
arm-none-eabi-nm build/stm32f4_kernel/release/stm32f4_kernel.elf | grep "dbg_"
```

### テスト手順例

#### 1. 基本動作確認 (48kHz)

```bash
# デバイスを48kHzに設定
python3 tools/set_sample_rate.py 48000

# カウンタをリセットして再生テスト
pyocd cmd -c "write32 0x20000e0c 0" && say "test" & sleep 0.5 && pyocd cmd -c "read32 0x20000e0c; read32 0x20000e28"

# 期待値: underrun ≈ 0-50, buf_level ≈ 40-80
```

#### 2. 高サンプルレートテスト (96kHz)

```bash
# デバイスを96kHzに設定
python3 tools/set_sample_rate.py 96000

# 再生中にfeedbackとバッファを確認
say "one two three four five" & sleep 0.5 && pyocd cmd -c "read32 0x20000e1c; read32 0x20000e28; read32 0x20000e0c"

# feedback = 0x180xxx (96.x samples/ms)
# buf_level > 30 なら正常
```

#### 3. サンプルレート切り替えテスト

```bash
# 48kHz → 96kHz → 48kHz と切り替え
python3 tools/set_sample_rate.py 48000 && sleep 3
python3 tools/set_sample_rate.py 96000 && sleep 3
python3 tools/set_sample_rate.py 48000

# 変更回数を確認
pyocd cmd -c "read32 0x20000e30"
```

### Feedback 値の解釈

UAC1 10.14 フォーマット: `feedback = samples_per_ms * 16384`

| サンプルレート | Feedback 値 | 16進数 |
|----------------|-------------|--------|
| 44.1kHz | 44.1 × 16384 ≈ 722,534 | 0x0B0666 |
| 48kHz | 48 × 16384 = 786,432 | 0x0C0000 |
| 96kHz | 96 × 16384 = 1,572,864 | 0x180000 |

実際のレートは I2S PLL 設定により若干異なります：
- 48kHz → 47,991 Hz (feedback ≈ 0x0BFF6C)
- 96kHz → 96,028 Hz (feedback ≈ 0x1801CA)

---

## 関連ドキュメント

- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) - 実装計画
- [SIMULATION.md](SIMULATION.md) - Renode シミュレーション
- [ARCHITECTURE.md](../specs/ARCHITECTURE.md) - アーキテクチャ
