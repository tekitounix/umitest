# umi

# 定義
- OS: KernelとDriverによって実装されたファームウェアとしてバイナリ配布されるもの
- Kernel: OSの中核機能ライブラリ
- Kernel-Core: KernelのうちHW非依存のロジック部分
- Kernel-Port: HW依存部分

例えばI2Cペリフェラルにおいて
- BSP: ピン番号やクロックなどの設定値の集合、ボード依存
- MMIO: レジスタ操作そのもの、HW依存
- HAL: レジスタ操作の抽象レイヤ、ここで使い方は統一されない　APIはHW非依存、PortはHW依存
- Driver: 実際の手続きと設定、ここで使い方を統一する。APIはHW非依存、PortはHW依存。単体呼び出しでは成り立たないことが多くOS実装依存。
- Device Server Task: Driverを呼び出して操作する

マイコンでベアメタルプログラミングする場合はタイミングまで直接制御することが多いのでHALでそのまま書くのが一般的。一方OSではタイミング管理をOSに実装するのでドライバは単純呼び出しで動作する。

# UMI-OS Spec

- syscall & shared abi
- MPU Layout
- Event/IPC
- .umia Format
    - Processor/Controller

# UMI-OS Bootloader

- umiboot

# UMIOS Library

UMIOSを構成するためのライブラリ群

- umios_kernel_core
    - scheduler
    - syscall
    - loader
- umios_kernel_port
    - cm4
    - esp32
    - wasm
- umia: 非特権タスクとして動作する機能
    - umiupdater
    - umishell
- umihal
    - umiusb
- umimmio
- bsp: 設定+初期化手続き


# UMI-APP SDK

OSやHW非依存の独立ライブラリ

- umidi
- umidsp
- umigui
- umiui
- umicoro: coroutine runtime library
- umism: state machine template library
- umiusb: usb class descriptor template

# UMIM

umi-moduleフォーマット

単体機能するヘッドレスアプリケーション

umip+umic+

# UMIU

umi-unitフォーマット

umip+umicで構成される

# UMI-Driver
- audio_device.hh
- usb_audio_device.hh

# UMI-HAL

OS非依存のレジスタ操作抽象化レイヤ。
- gpio_hal.hh
- usb_hal.hh

# UMI-Port

- mcu
  - stm32
      - hal_mmio
        - gpio.hh
        - ...
      - driver
        - gpio.hh
        - ...
      - kernel
        - port.cc
- device
  - audio_codec
    - cs43l22.hh


stack は固定サイズにする
実行中に境界は動かさない

カーネルがロード時に MPU ガードを張る
stack 下端
heap 上端
最小サイズの no-access リージョンを 1 枚置く

heap アロケータ
境界チェック
使用量とピーク計測のみ

heap と stack の衝突は MPU で即 Fault
遅検出にしない。

一時的大容量は stack に置かない
scratch arena や固定 pool を使う。

Fault 時は OS がログを取る
レジスタ
Fault ステータス
heap stack 使用量

heap 用 syscall は不要
固定領域なら MemMap も不要。必要なのはロード時設定だけ。

一文で
スタック境界は固定して MPU で守り、ヒープはその内側で管理し、動かさない。可変にしたい容量は stack ではなく scratch に逃がす。


Fault ハンドリング

MPU ガードに触れる
スタックオーバーフロー等で MemManageFault。

Fault ハンドラに入る
OS 用スタックで実行。

ログ保存
レジスタと Fault 状態を固定バッファへ。

現在タスクを死亡マーク
復帰対象から外す。

復帰 PC をトランポリンに差し替え
アプリ文脈を捨てるため。

PendSV をセット
安全なスケジューラ遷移を予約。

Fault から例外復帰
アプリには戻らずトランポリンへ。

トランポリン実行
割り込み状態を正規化。

PendSV ハンドラでスケジューラ実行
死亡タスクを破棄。

アイドルまたは次アプリへ

``` C++
void fault_handler(...) {
  save_log();
  mark_current_task_dead();
  set_return_pc(trampoline_entry);
  trigger_pendsv();
}

void trampoline_entry(void) {
  disable_interrupts();
  schedule();
}
```

---

# 検討メモ（旧 MEMOMEMO.md より統合）

## タスク優先度の検討

- Server Task の優先度は User Task と同等でよいか？
- それとも User Task と同列の新しいタスク階層が必要か？

## アーキテクチャの役割分担（暫定）

- Controller: ターゲット非依存の領域を担当
- App: Config で要求を表明
- OS: ケイパビリティベースで要求に応答
- App は最低限のヘッドレス動作を実装
	- ヘッドレス部分 + 互換機能が、あらゆるターゲットで動作することを目標

## ライセンス管理・認証

- ライセンス管理の仕組みを作れないか？
- アプリケーション認証の仕組みに統合できないか？
- 超コンパクトで堅牢、常にバックアップされるミニマルな認証サーバー案

## 組み込み環境のI/O設計方針

- 組み込み環境のみを想定
	- 動的なポート変更は不要
- USBデバイス/拡張ボード接続でI/Oが変化しても、メモリ資源は有限
	- 静的確保した範囲内で動作する設計にする
- 拡張デバイス接続時は span で渡すポート数を変化させる必要
