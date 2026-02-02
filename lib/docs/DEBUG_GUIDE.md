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

# ターゲット MCU のパックをインストール
pyocd pack install <MCU>    # 例: stm32f407vg
```

---

## フラッシュ書き込み

### xmake によるフラッシュ

```bash
# Release ビルド & フラッシュ
xmake config -m release && xmake build <TARGET> && xmake flash -t <TARGET>
```

### pyOCD による書き込み

```bash
# フラッシュ書き込み
pyocd flash -t <MCU> build/<TARGET>/release/<TARGET>.bin

# チップ消去
pyocd erase -t <MCU> --chip

# リセット
pyocd reset -t <MCU>
```

---

## GDB デバッグ

### GDB サーバ起動と接続

```bash
# ターミナル1: GDBサーバ起動
pyocd gdbserver -t <MCU>

# ターミナル2: GDB接続
arm-none-eabi-gdb build/<TARGET>/release/<TARGET>
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
pyocd rtt -t <MCU>
```

RTT はデバッグ中のログ出力に最適です。UART と異なり追加の配線不要で高速です。

---

## AI 自動化向けデバッグ

AI エージェント（Claude Code 等）によるデバッグ自動化では、以下の問題が頻発します：

- **インタラクティブツールのハング**: GDB、Renode 等が入力待ちで停止
- **デバッガの不正終了**: プローブが占有されたまま残り、次回接続不可
- **タイムアウト不足**: 処理が終わらず永久待機

これらを回避するため、**pyOCD Python API** を使用した非インタラクティブなデバッグを推奨します。

### 安全なデバッグセッション

タイムアウトとクリーンアップを保証するコンテキストマネージャを使用します。

```python
from contextlib import contextmanager
from pyocd.core.helpers import ConnectHelper
import signal

class DebugTimeout(Exception):
    pass

@contextmanager
def safe_debug_session(target, timeout=60):
    """
    タイムアウトとクリーンアップを保証するデバッグセッション

    Usage:
        with safe_debug_session('<MCU>', timeout=30) as target:
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
                pass
```

### デバッガクリーンアップ

セッション開始前に残留プロセスを処理します。

```python
import subprocess
import time

def cleanup_debugger():
    """残留デバッガプロセスを確認し、該当 PID のみ終了"""
    for proc in ['pyocd', 'openocd', 'arm-none-eabi-gdb']:
        result = subprocess.run(
            ['pgrep', '-fl', proc], capture_output=True, text=True
        )
        for line in result.stdout.strip().split('\n'):
            if not line:
                continue
            pid = line.split()[0]
            subprocess.run(['kill', pid], capture_output=True)
    time.sleep(0.5)

def verify_probe_available():
    """プローブが利用可能か確認"""
    result = subprocess.run(
        ['pyocd', 'list'],
        capture_output=True, text=True, timeout=10
    )
    return 'No available' not in result.stdout
```

### subprocess によるコマンド実行

CLI ツールを安全に呼び出す場合はタイムアウトを必ず指定します。

```python
import subprocess

def run_pyocd_command(args, timeout=30):
    """タイムアウト付きで pyOCD コマンドを実行"""
    try:
        result = subprocess.run(
            ['pyocd'] + args,
            capture_output=True, text=True, timeout=timeout
        )
        return result.stdout, result.returncode
    except subprocess.TimeoutExpired:
        return None, -1

def flash_firmware(firmware_path, target):
    """ファームウェアをフラッシュ"""
    _, rc = run_pyocd_command(['flash', '-t', target, firmware_path], timeout=60)
    return rc == 0
```

### Robot Framework によるテスト自動化

複雑なテストシナリオには Robot Framework を使用します。pyOCD Python API をキーワードとしてラップし、テストケースを記述します。

```robot
*** Settings ***
Library    robot_keywords.py
Test Timeout    120 seconds

*** Test Cases ***
Flash And Run
    [Timeout]    60 seconds
    Cleanup Debugger
    Connect Target    <MCU>
    Flash Firmware    build/<TARGET>/release/<TARGET>.bin
    Reset And Run
    Sleep    2s
    Verify Running
    [Teardown]    Disconnect Target
```

キーワードライブラリは pyOCD Python API をラップして実装します:
- `Connect Target`: `ConnectHelper.session_with_chosen_probe()` で接続
- `Flash Firmware`: `session.board.flash.program()` で書き込み
- `Disconnect Target`: `session.close()` で切断

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

with ConnectHelper.session_with_chosen_probe(target_override='<MCU>') as session:
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

def test_firmware_init(mcu, elf_path):
    """ファームウェア初期化の自動テスト"""
    with ConnectHelper.session_with_chosen_probe(target_override=mcu) as session:
        target = session.target
        target.reset_and_halt()

        # main まで実行
        target.set_breakpoint(target.elf.symbols['main'].address)
        target.resume()
        while target.get_state() != 'halted':
            time.sleep(0.01)

        # レジスタ/メモリ値を検証
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
brew install sox

# 録音（5秒）
sox -d test.wav trim 0 5

# 波形解析
sox test.wav -n stat
sox test.wav -n spectrogram -o spec.png
sox test.wav -n stats
```

### サンプルレート変更 (macOS)

macOS CoreAudio API を ctypes で呼び出してサンプルレートを変更できます。実装は `tools/set_sample_rate.py` を参照してください。

```bash
# サンプルレート変更
python3 tools/set_sample_rate.py <RATE>    # 例: 48000, 96000

# 音声再生テスト
afplay /System/Library/Sounds/Ping.aiff
say "one two three four five"
```

### デバッグ変数の確認

ファームウェアに `dbg_` プレフィックスのデバッグ変数を定義しておくと、実行中にメモリ読み取りで状態を確認できます。

```bash
# デバッグ変数のアドレスを取得
arm-none-eabi-nm build/<TARGET>/release/<TARGET>.elf | grep "dbg_"

# 再生中にデバッグ変数を読み取る例
say "test" & sleep 0.5 && pyocd cmd -c "read32 <ADDR>"
```

音声再生**中**にデバッグ変数を確認することが重要です。再生終了後はバッファが空になります。

### UAC Feedback 値の解釈

UAC1 10.14 フォーマット: `feedback = samples_per_ms * 16384`

| サンプルレート | Feedback 値 | 16進数 |
|----------------|-------------|--------|
| 44.1kHz | 44.1 × 16384 ≈ 722,534 | 0x0B0666 |
| 48kHz | 48 × 16384 = 786,432 | 0x0C0000 |
| 96kHz | 96 × 16384 = 1,572,864 | 0x180000 |

実際のレートは PLL 設定により若干異なります。

---

## MIDI デバッグ

### sendmidi / receivemidi

```bash
brew install sendmidi receivemidi

# MIDI 受信モニタ
receivemidi dev "<DEVICE_NAME>"

# MIDI 送信
sendmidi dev "<DEVICE_NAME>" on 60 100    # Note On C4
sendmidi dev "<DEVICE_NAME>" off 60 0     # Note Off C4
```

---

## Renode シミュレーション

実機なしでのテスト・デバッグが可能です。

### 基本実行（手動）

```bash
renode <RESC_FILE>    # 例: tools/renode/synth.resc
```

### インタラクティブモード（手動のみ）

> **警告**: AI エージェントではインタラクティブモードを使用しないでください。

```bash
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
arm-none-eabi-gdb build/<TARGET>/release/<TARGET>
(gdb) target remote :3333
```

### Robot Framework による自動テスト（AI 向け）

Renode は Robot Framework と統合されており、非インタラクティブなテストが可能です。

```robot
*** Settings ***
Resource    ${RENODEKEYWORDS}
Suite Setup    Setup
Suite Teardown    Teardown
Test Timeout    60 seconds

*** Keywords ***
Setup
    Execute Command    mach create
    Execute Command    machine LoadPlatformDescription @<PLATFORM_REPL>
    Execute Command    sysbus LoadELF @build/<TARGET>/release/<TARGET>

Teardown
    Execute Command    mach clear

*** Test Cases ***
Should Boot Successfully
    [Timeout]    30 seconds
    Execute Command    cpu Step 10000
    ${pc}=    Execute Command    cpu PC
    Should Not Be Equal    ${pc}    0x0
```

実行方法：

```bash
renode-test <ROBOT_FILE>
```

---

## トラブルシューティング

### デバッガが接続できない

```bash
# 接続確認
pyocd list

# ターゲットが見つからない場合はパックをインストール
pyocd pack install <MCU>

# リセット
pyocd reset -t <MCU>
```

### デバッガが占有されたまま（AI 自動化でよくある問題）

前回のセッションが正常終了しなかった場合に発生します。

> **注意**: `pkill -f <pattern>` は使用しないこと。パターンのサブストリングマッチにより、エディタや関係ないスクリプト等、無関係なプロセスまで巻き込んで終了させる危険があります。必ず `pgrep -fl` で確認してから `kill <PID>` で個別に終了してください。

```bash
# 残留プロセスを確認
pgrep -fl pyocd
pgrep -fl openocd
pgrep -fl arm-none-eabi-gdb

# 該当 PID を個別に終了
kill <PID>

# 少し待ってから再接続
sleep 1
pyocd list
```

### フラッシュ書き込みエラー

```bash
# チップ消去後に再試行
pyocd erase -t <MCU> --chip
pyocd flash -t <MCU> <FIRMWARE>.bin
```

### USB Audio が認識されない

1. USB ケーブル確認（データ対応か）
2. Console.app でエラー確認
3. USB ディスクリプタの検証

```bash
log show --predicate 'subsystem == "com.apple.usb"' --last 5m
```

### AI エージェントがハングした場合

インタラクティブツールを誤って起動した場合（`pkill -f` は誤爆の危険があるため使用禁止）：

```bash
# 残留プロセスを確認してから個別に終了
pgrep -fl pyocd
pgrep -fl openocd
pgrep -fl arm-none-eabi-gdb
pgrep -fl renode

# 該当 PID のみ終了
kill <PID>
```

---

## USB デバイス詳細確認

### USB デバイス一覧と基本情報

```bash
# 接続中の全 USB デバイスを表示
system_profiler SPUSBDataType

# 特定デバイスをフィルタ（例: UMI デバイス）
system_profiler SPUSBDataType | grep -A 15 "UMI\|Daisy"
```

### USB ディスクリプタの詳細取得（pyusb）

pyusb を使うとインターフェース、エンドポイント、クラス情報をプログラマティックに取得できます。

```python
import usb.core, usb.util

dev = usb.core.find(idVendor=0x1209, idProduct=0x000b)
if dev is None:
    print("Device not found"); exit(1)

print(f"{dev.manufacturer} {dev.product}  VID:PID={dev.idVendor:04x}:{dev.idProduct:04x}")
for cfg in dev:
    for intf in cfg:
        name = ""
        try:
            name = usb.util.get_string(dev, intf.iInterface) if intf.iInterface else ""
        except: pass
        print(f"  IF{intf.bInterfaceNumber} alt={intf.bAlternateSetting} "
              f"class={intf.bInterfaceClass} sub={intf.bInterfaceSubClass} \"{name}\"")
        for ep in intf:
            d = "IN" if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN else "OUT"
            print(f"    EP 0x{ep.bEndpointAddress:02x} {d} maxPacket={ep.wMaxPacketSize}")
```

**依存**: `pip install pyusb` (libusb が必要: `brew install libusb`)

### USB クラスの読み方

| Interface Class | SubClass | 意味 |
|-----------------|----------|------|
| 1 | 1 | Audio Control |
| 1 | 2 | Audio Streaming |
| 1 | 3 | MIDI Streaming |

### USB ログ確認

```bash
# macOS USB サブシステムのログ（直近5分）
log show --predicate 'subsystem == "com.apple.usb"' --last 5m

# リアルタイムで監視
log stream --predicate 'subsystem == "com.apple.usb"'
```

---

## MIDI デバイスの詳細確認と制御

### MIDI デバイス一覧（mido / python-rtmidi）

```bash
python3 -c "
import mido
print('Outputs:', mido.get_output_names())
print('Inputs:', mido.get_input_names())
"
```

**依存**: `pip install mido python-rtmidi`

### CoreMIDI デバイス一覧（低レベル）

mido が使えない環境では ctypes で CoreMIDI を直接呼べます。

```python
import ctypes, ctypes.util
from ctypes import c_uint32, c_void_p, c_int32, byref

cm = ctypes.cdll.LoadLibrary(ctypes.util.find_library('CoreMIDI'))
cf = ctypes.cdll.LoadLibrary(ctypes.util.find_library('CoreFoundation'))
cm.MIDIGetNumberOfDevices.restype = c_uint32
cm.MIDIGetDevice.restype = c_uint32
cm.MIDIObjectGetStringProperty.restype = c_int32
cf.CFStringGetCStringPtr.restype = ctypes.c_char_p
cf.CFStringGetCStringPtr.argtypes = [c_void_p, c_uint32]
kMIDIPropertyName = c_void_p.in_dll(cm, 'kMIDIPropertyName')

for i in range(cm.MIDIGetNumberOfDevices()):
    dev = cm.MIDIGetDevice(i)
    ref = c_void_p()
    if cm.MIDIObjectGetStringProperty(dev, kMIDIPropertyName, byref(ref)) == 0 and ref.value:
        s = cf.CFStringGetCStringPtr(ref, 0)
        print(f"  Device {i}: {s.decode() if s else '<unknown>'}")
```

### MIDI 送信（ノート、CC 等）

```python
import mido, time

with mido.open_output('<DEVICE_NAME>') as port:
    # Note On/Off
    port.send(mido.Message('note_on', note=60, velocity=100))
    time.sleep(0.5)
    port.send(mido.Message('note_off', note=60, velocity=0))

    # Control Change
    port.send(mido.Message('control_change', control=1, value=64))

    # Program Change
    port.send(mido.Message('program_change', program=5))
```

### MIDI 受信モニタ（タイムアウト付き）

```python
import mido, time

with mido.open_input('<DEVICE_NAME>') as port:
    start = time.time()
    while time.time() - start < 5:  # 5秒間モニタ
        msg = port.poll()
        if msg:
            print(msg)
        time.sleep(0.01)
```

### MIDI ラウンドトリップテスト

デバイスに MIDI を送り、応答を確認する統合テストパターン:

```python
import mido, time, threading

received = []

def listen(port_name, duration):
    with mido.open_input(port_name) as port:
        start = time.time()
        while time.time() - start < duration:
            msg = port.poll()
            if msg:
                received.append(msg)
            time.sleep(0.005)

# 受信スレッド開始
t = threading.Thread(target=listen, args=('<DEVICE_NAME>', 3))
t.start()
time.sleep(0.2)

# 送信
with mido.open_output('<DEVICE_NAME>') as port:
    port.send(mido.Message('note_on', note=60, velocity=100))
    time.sleep(0.5)
    port.send(mido.Message('note_off', note=60, velocity=0))

t.join()
print(f"Received {len(received)} messages: {received}")
```

---

## オーディオストリーミングテスト

### オーディオデバイス一覧

```bash
# 全オーディオデバイスの詳細
system_profiler SPAudioDataType

# JSON 形式で取得（スクリプト向け）
system_profiler SPAudioDataType -json
```

### sox によるオーディオ出力テスト

USB Audio デバイスへテスト信号を送信:

```bash
# サイン波 440Hz を出力（2秒）
sox -n -t coreaudio "<DEVICE_NAME>" synth 2 sine 440 gain -6

# ホワイトノイズを出力（1秒）
sox -n -t coreaudio "<DEVICE_NAME>" synth 1 noise gain -12

# スイープ（20Hz→20kHz、5秒）
sox -n -t coreaudio "<DEVICE_NAME>" synth 5 sine 20:20000 gain -6
```

### sox によるオーディオ録音と解析

USB Audio デバイスからの入力を録音して解析:

```bash
# 録音（3秒）
sox -t coreaudio "<DEVICE_NAME>" /tmp/rec.wav trim 0 3

# 録音結果の統計情報
sox /tmp/rec.wav -n stats

# スペクトログラム生成
sox /tmp/rec.wav -n spectrogram -o /tmp/spec.png

# RMS レベル確認（無音検出に使える）
sox /tmp/rec.wav -n stat 2>&1 | grep "RMS"
```

### MIDI 送信 + オーディオ録音の統合テスト

シンセにノートを送り、出力音声を自動録音して検証:

```bash
# 1. バックグラウンドで録音開始
sox -t coreaudio "<DEVICE_NAME>" /tmp/synth_test.wav trim 0 4 &
SOX_PID=$!
sleep 0.5

# 2. MIDI ノートを送信
python3 -c "
import mido, time
with mido.open_output('<DEVICE_NAME>') as port:
    for note in [60, 64, 67, 72]:
        port.send(mido.Message('note_on', note=note, velocity=100))
        time.sleep(0.6)
        port.send(mido.Message('note_off', note=note, velocity=0))
        time.sleep(0.2)
"

# 3. 録音完了を待機
wait $SOX_PID

# 4. 結果を解析
sox /tmp/synth_test.wav -n stats
```

### 音声出力の自動検証

録音データが無音でないことを確認するスクリプト:

```python
import subprocess, sys

def verify_audio_not_silent(wav_path, threshold_db=-60):
    """録音が無音でないことを検証"""
    result = subprocess.run(
        ['sox', wav_path, '-n', 'stats'],
        capture_output=True, text=True, timeout=10
    )
    for line in result.stderr.split('\n'):
        if 'RMS lev dB' in line and 'Overall' not in line:
            parts = line.split()
            for p in parts:
                try:
                    db = float(p)
                    if db > threshold_db:
                        print(f"PASS: Audio detected (RMS = {db} dB)")
                        return True
                except ValueError:
                    continue
    print(f"FAIL: Audio is silent (below {threshold_db} dB)")
    return False

if not verify_audio_not_silent('/tmp/synth_test.wav'):
    sys.exit(1)
```

### afplay による簡易再生テスト

```bash
# macOS 標準のオーディオ再生（デフォルト出力デバイスを使用）
afplay /System/Library/Sounds/Ping.aiff

# say コマンドで音声合成テスト
say "audio test"
```

---

## 関連ドキュメント

- [TESTING.md](TESTING.md) - テスト戦略
- [CODING_STYLE.md](CODING_STYLE.md) - コーディングスタイル
- [LIBRARY_STRUCTURE.md](LIBRARY_STRUCTURE.md) - ライブラリ構造規約
