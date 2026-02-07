# UMI プロジェクト構成とファイル役割

Universal Musical Instruments — ワンソース・マルチターゲットオーディオフレームワーク

---

## ルートレベル

```
umi/
├── xmake.lua                    # メインビルド設定（ホストテスト・ARM・WASM・タスク定義）
├── CLAUDE.md                    # Claude Code向けプロジェクトルール
├── README.md                    # プロジェクト概要
├── compile_commands.json        # clangd用コンパイルデータベース
├── .clang-format                # コードフォーマット設定（LLVM base, 4-space, 120 char）
├── .clang-tidy                  # 静的解析設定
├── .clangd                      # clangd設定
├── .github/
│   ├── copilot-instructions.md  # GitHub Copilot向け指示
│   └── workflows/ci.yml         # CI/CDワークフロー
└── .refs/                       # カスタムxmakeパッケージリポジトリ（ARM組み込みツールチェーン）
```

---

## lib/ — コアライブラリ（プラットフォーム非依存）

### lib/umi/ — 統合メタパッケージ

```
lib/umi/
└── xmake.lua                    # 全ライブラリのインクルードパスを提供するメタパッケージ
```

### lib/umi/ — OSコア・カーネル

```
lib/umi/
├── core/                        # === 基本型・インターフェース ===
│   ├── audio_context.hh         # AudioContext構造体（process()に渡されるI/Oバッファ・イベント・タイミング情報）
│   ├── processor.hh             # ProcessorLike concept, PortDescriptor, ParamDescriptor
│   ├── event.hh                 # Event型（MIDI/Param/Button）, EventQueue（サンプル精度イベント処理）
│   ├── types.hh                 # 基本型定義（sample_t, port_id_t, param_id_t等）
│   ├── time.hh                  # 時刻型
│   ├── error.hh                 # Result<T>型, Error enum
│   ├── triple_buffer.hh         # ロックフリーTripleバッファ（ISR↔タスク間通信）
│   ├── irq.hh                   # 割り込み制御抽象
│   ├── app.hh                   # アプリケーションベース
│   └── ui/                      # UI関連
│       ├── ui_controller.hh     # UIコントローラ
│       ├── ui_map.hh            # UIマッピング
│       └── ui_view.hh           # UIビュー
│
├── kernel/                      # === RTOSカーネル実装（STM32F4向け） ===
│   ├── umi_kernel.hh            # ヘッダオンリーカーネル（O(1)スケジューラ、タスク管理、タイマー）
│   ├── umi_audio.hh             # オーディオモジュール
│   ├── umi_midi.hh              # MIDIモジュール
│   ├── umi_shell.hh             # シェルモジュール
│   ├── umi_monitor.hh           # モニタモジュール
│   ├── umi_startup.hh           # スタートアップ
│   ├── loader.hh/.cc            # アプリケーションローダー（.umia実行）
│   ├── app_header.hh            # アプリケーションヘッダ構造
│   ├── mpu_config.hh            # MPU設定（メモリ保護）
│   ├── protection.hh            # 保護機能
│   ├── syscall_handler.hh       # システムコールハンドラ
│   ├── driver.hh                # ドライバインターフェース
│   ├── coro.hh                  # コルーチンサポート
│   ├── assert.hh                # アサーション
│   ├── log.hh                   # ログ
│   ├── metrics.hh               # メトリクス
│   ├── fault_handler.hh         # フォルトハンドラ
│   ├── embedded_state_provider.hh # 状態プロバイダ
│   ├── shell_commands.hh        # シェルコマンド定義
│   ├── modules/
│   │   ├── audio_module.hh      # オーディオモジュール
│   │   └── usb_audio_module.hh  # USBオーディオモジュール
│   ├── port/cm4/                # Cortex-M4ポート
│   │   ├── cm4.hh               # Cortex-M4固有実装
│   │   ├── context.hh           # コンテキストスイッチ
│   │   ├── handlers.hh/.cc      # 割り込みハンドラ
│   │   └── switch.hh            # タスクスイッチ
│   └── syscall/
│       └── syscall_numbers.hh   # システムコール番号定義
│
├── backend/                     # === プラットフォームバックエンド ===
│   ├── cm/                      # --- Cortex-Mバックエンド ---
│   │   ├── cortex_m4.hh         # Cortex-M4抽象化
│   │   ├── common/
│   │   │   ├── irq.hh/.cc       # 割り込み管理
│   │   │   ├── nvic.hh          # NVIC制御
│   │   │   ├── scb.hh           # SCB制御
│   │   │   ├── systick.hh       # SysTickタイマー
│   │   │   ├── dwt.hh           # DWTサイクルカウンタ
│   │   │   ├── fault.hh         # フォルト処理
│   │   │   └── vector_table.hh  # ベクタテーブル
│   │   ├── platform/
│   │   │   ├── privilege.hh     # 特権レベル管理
│   │   │   ├── protection.hh    # メモリ保護
│   │   │   └── syscall.hh       # システムコールインターフェース
│   │   ├── drivers/
│   │   │   ├── systick_driver.hh # SysTickドライバ
│   │   │   └── uart_driver.hh   # UARTドライバ
│   │   ├── stm32f4/             # STM32F4固有ペリフェラル
│   │   │   ├── hw.hh            # ハードウェア定義
│   │   │   ├── gpio.hh          # GPIO
│   │   │   ├── rcc.hh           # クロック設定
│   │   │   ├── i2c.hh           # I2C
│   │   │   ├── i2s.hh           # I2S（オーディオ）
│   │   │   ├── exti.hh          # 外部割り込み
│   │   │   ├── power.hh         # 電源管理
│   │   │   ├── uid.hh           # ユニークID
│   │   │   ├── usb_otg.hh       # USB OTG
│   │   │   ├── usb_midi.hh      # USB MIDI
│   │   │   ├── cs43l22.hh       # CS43L22 DACドライバ
│   │   │   ├── pdm_mic.hh       # PDMマイク
│   │   │   └── irq_num.hh       # 割り込み番号
│   │   └── svc_handler.hh       # SVCハンドラ
│   └── wasm/                    # --- WASMバックエンド ---
│       ├── platform/
│       │   ├── privilege.hh     # 特権管理（スタブ）
│       │   ├── protection.hh    # メモリ保護（スタブ）
│       │   └── syscall.hh       # システムコール
│       ├── web_hal.hh           # Web HAL
│       ├── web_sim.hh           # Webシミュレーション
│       └── renode_hal.hh        # Renode HAL
│
├── adapter/                     # === プラットフォームアダプタ ===
│   ├── embedded.hh              # 組み込みアダプタヘッダ
│   ├── embedded_adapter.hh      # EmbeddedAdapter<Proc,Hw>テンプレート
│   ├── umim_adapter.hh          # UMIM WASMエクスポートマクロ（UMIM_EXPORT）
│   └── web/
│       └── web_adapter.hh       # Webアダプタ
│
├── app/                         # === アプリケーション実行環境 ===
│   ├── crt0.cc                  # アプリ起動コード（_start → main呼び出し）
│   ├── syscall.hh               # アプリ側システムコールラッパー
│   ├── umi_app.hh               # アプリケーションベース
│   ├── app.ld                   # アプリ用リンカスクリプト（.umia用）
│   └── xmake.lua                # アプリビルド設定
│
└── crypto/                      # === 暗号・署名検証 ===
    ├── ed25519.hh/.cc           # Ed25519署名検証
    ├── sha512.hh/.cc            # SHA-512ハッシュ
    └── public_key.hh            # 公開鍵型定義
```

### lib/umidsp/ — DSPライブラリ

```
lib/umidsp/
├── include/
│   ├── umidsp.hh                # DSP総合ヘッダ
│   ├── core/
│   │   ├── constants.hh         # 数学定数（PI, TWO_PI等）
│   │   ├── interpolate.hh       # 補間関数（linear, cubic）
│   │   └── phase.hh             # 位相累積器
│   ├── filter/                  # フィルタ実装群
│   │   ├── filter.hh            # フィルタベース
│   │   ├── common.hh            # 共通定義
│   │   ├── one_pole.hh          # 1極フィルタ
│   │   ├── biquad.hh            # Biquadフィルタ
│   │   ├── svf.hh               # State Variable Filter
│   │   ├── k35.hh               # Korg35フィルタ
│   │   ├── moog.hh              # Moogラダーフィルタ
│   │   ├── obmoog.hh            # Oberheim Moogフィルタ
│   │   ├── sallenkey.hh         # Sallen-Keyフィルタ
│   │   ├── ssm2040.hh           # SSM2040フィルタ
│   │   ├── halfband.hh          # ハーフバンドフィルタ
│   │   ├── moving_average.hh    # 移動平均
│   │   ├── smoother.hh          # パラメータスムージング
│   │   └── slew_limiter.hh      # スルーリミッタ
│   ├── filter_ref/              # リファレンス実装（テスト・検証用）
│   ├── audio/
│   │   ├── synth/
│   │   │   ├── oscillator.hh    # オシレータ（Sine, Saw, Square, SawBL等）
│   │   │   └── envelope.hh      # エンベロープ（ADSR, Ramp）
│   │   └── rate/
│   │       ├── asrc.hh          # 非同期サンプルレート変換器
│   │       └── pi_controller.hh # PIレートコントローラ
│   └── synth/                   # レガシーシンセ（互換用）
│       ├── oscillator.hh        # 旧オシレータ
│       └── filter.hh            # 旧フィルタ
├── test/
│   └── test_dsp.cc              # DSPユニットテスト
└── xmake.lua                    # DSPライブラリビルド設定
```

### lib/umidi/ — MIDIライブラリ

```
lib/umidi/
├── include/
│   ├── umidi.hh                 # MIDI総合ヘッダ（UMP-Opt形式）
│   ├── core/
│   │   ├── ump.hh               # UMP32型（uint32_tベースの最適化MIDI表現）
│   │   ├── parser.hh            # インクリメンタルMIDIパーサ
│   │   ├── result.hh            # Result型
│   │   └── sysex_buffer.hh      # SysExバッファ
│   ├── messages/
│   │   ├── channel_voice.hh     # ノート、CC、ピッチベンド等
│   │   ├── system.hh            # システムメッセージ
│   │   ├── sysex.hh             # SysEx
│   │   └── utility.hh           # ユーティリティメッセージ
│   ├── cc/
│   │   ├── types.hh             # CC型定義
│   │   ├── standards.hh         # 標準CC番号
│   │   └── decoder.hh           # CCデコーダ
│   ├── codec/
│   │   └── decoder.hh           # テンプレート静的デコーダ
│   ├── protocol/                # UMI拡張プロトコル（SysEx over MIDI）
│   │   ├── umi_sysex.hh         # UMI SysExベース
│   │   ├── umi_transport.hh     # トランスポート層
│   │   ├── umi_state.hh         # 状態管理
│   │   ├── umi_object.hh        # オブジェクト転送
│   │   ├── umi_session.hh       # セッション管理
│   │   ├── umi_firmware.hh      # ファームウェア更新
│   │   ├── umi_auth.hh          # 認証
│   │   ├── umi_bootloader.hh    # ブートローダ
│   │   ├── commands.hh          # コマンド定義
│   │   ├── encoding.hh          # エンコーディング
│   │   ├── message.hh           # メッセージ構造
│   │   └── standard_io.hh       # 標準I/O（stdio over SysEx）
│   └── util/
│       └── convert.hh           # MIDI変換ユーティリティ
├── test/
│   ├── test_core.cc             # コアテスト
│   ├── test_messages.cc         # メッセージテスト
│   ├── test_protocol.cc         # プロトコルテスト
│   └── test_extended_protocol.cc # 拡張プロトコルテスト
└── xmake.lua                    # MIDIライブラリビルド設定
```

### lib/umiboot/ — ブートローダー・ファームウェア検証

```
lib/umiboot/
├── include/umiboot/
│   ├── boot.hh                  # ブートローダー総合ヘッダ
│   ├── auth.hh                  # HMAC-SHA256認証
│   ├── firmware.hh              # ファームウェアヘッダ検証（Ed25519署名）
│   ├── bootloader.hh            # ブートローダー本体
│   └── session.hh               # セッション管理（タイムアウト、フロー制御）
├── test/
│   ├── test_auth.cc             # 認証テスト
│   ├── test_firmware.cc         # ファームウェアテスト
│   └── test_session.cc          # セッションテスト
└── xmake.lua                    # ブートライブラリビルド設定
```

### lib/umisynth/ — シンセサイザー実装

```
lib/umisynth/
├── include/umisynth/
│   └── synth.hh                 # PolySynth（Voice管理、ADSR、フィルタ、ProcessorLike準拠）
└── xmake.lua                    # シンセライブラリビルド設定
```

### lib/umiusb/ — USBデバイススタック

```
lib/umiusb/
├── include/
│   ├── umiusb.hh                # USB総合ヘッダ
│   ├── types.hh                 # USB基本型定義
│   ├── hal.hh                   # HAL抽象化Concept
│   ├── device.hh                # USBデバイスコア
│   ├── descriptor.hh            # コンパイル時ディスクリプタ生成
│   ├── audio_types.hh           # USB Audioクラス型（UAC2対応）
│   ├── audio_interface.hh       # USB Audioインターフェース
│   ├── audio_device.hh          # USB Audioデバイス
│   ├── umidi_adapter.hh         # MIDI over USBアダプタ
│   └── hal/
│       └── stm32_otg.hh         # STM32 OTG HAL実装
└── xmake.lua                    # USBライブラリビルド設定
```

### lib/umishell/ — シェル機能

```
lib/umishell/
└── include/umishell/
    ├── shell_core.hh            # シェルコア（コマンド解析・実行）
    └── shell_auth.hh            # シェル認証
```

### lib/umigui/ — GUI（スケルトン）

```
lib/umigui/
├── gui.hh                       # GUIコア
├── layout.hh                    # レイアウト
├── backend.hh                   # バックエンド抽象
├── canvas2d_backend.hh          # Canvas2Dバックエンド
├── framebuffer_backend.hh       # フレームバッファバックエンド
└── skin/                        # スキン
    ├── skin.hh                  # スキンベース
    ├── default/
    │   └── default_skin.hh      # デフォルトスキン
    └── base/
        ├── linear.hh            # リニアコントロール
        ├── rotary.hh            # ロータリーコントロール
        ├── meter.hh             # メーター
        ├── selector.hh          # セレクタ
        └── toggle.hh            # トグル
```

### lib/bsp/ — Board Support Package

```
lib/bsp/
├── io_types.hh                  # I/O型定義
├── stm32f4-disco/               # STM32F4-Discovery BSP
│   ├── hw_impl.hh               # ハードウェア実装（GPIO、クロック、I2S、USB初期化）
│   ├── io_mapping.hh            # I/Oマッピング
│   ├── startup.hh               # スタートアップ
│   └── syscalls.cc              # newlib用システムコールスタブ
└── stub/                        # テスト用スタブBSP
    └── hw_impl.hh               # スタブ実装
```

### lib/hal/ — Hardware Abstraction Layer

```
lib/hal/stm32/
├── gpio.hh                      # GPIO抽象
├── rcc.hh                       # クロック制御
└── uart.hh                      # UART抽象
```

---

## examples/ — サンプルアプリケーション

### examples/stm32f4_kernel/ — STM32F4カーネル

```
examples/stm32f4_kernel/
├── src/
│   ├── main.cc                  # カーネルメインエントリポイント
│   ├── kernel.hh/.cc            # カーネルインスタンス構築・初期化
│   ├── arch.hh/.cc              # アーキテクチャ固有処理
│   ├── mcu.hh/.cc               # MCU初期化・USB Audio OUT実装
│   └── bsp.hh                   # BSP定義
├── kernel.ld                    # カーネル用リンカスクリプト
└── xmake.lua                    # カーネルビルド設定（flash-kernelタスク含む）
```

### examples/synth_app/ — シンセアプリ（.umia）

```
examples/synth_app/
├── src/
│   └── main.cc                  # アプリメイン（PolySynth使用、syscall経由でプロセッサ登録）
└── xmake.lua                    # アプリビルド設定（.umia生成、flash-synth-appタスク）
```

### examples/headless_webhost/ — WASM Webビルド

```
examples/headless_webhost/
├── src/
│   ├── synth.hh                 # シンセ定義
│   ├── synth_processor.hh       # シンセプロセッサ
│   ├── synth_sim.cc             # UMI-OSバックエンド（カーネル全体シミュレーション）
│   └── umim_synth.cc            # UMIMバックエンド（軽量DSP-only）
├── web/
│   ├── index.html               # メインWebページ
│   └── synth_sim_worklet.js     # AudioWorklet
└── xmake.lua                    # WASMビルド設定
```

### examples/embedded/ — 組み込み基本例

```
examples/embedded/
├── example_app.cc               # 基本アプリケーション例
└── vector_table_example.cc      # ベクタテーブル例
```

### examples/renode_test/ — Renodeエミュレータテスト

```
examples/renode_test/
├── main.cc                      # テストメイン
├── startup.cc                   # スタートアップ
└── xmake.lua                    # Renodeテストビルド設定
```

---

## tests/ — ホストテスト

```
tests/
├── test_common.hh               # テストフレームワーク（最小実装、例外/RTTI不使用）
├── test_kernel.cc               # カーネルテスト（スケジューラ、タスク管理等）
├── test_audio.cc                # オーディオテスト（AudioContext、バッファ処理）
├── test_midi.cc                 # MIDIテスト（パーサ、UMP）
├── test_midi_lib.cc             # umidiライブラリテスト
├── test_umidi_comprehensive.cc  # umidi包括テスト
├── test_concepts.cc             # ProcessorLike等Conceptsテスト
├── test_loop_style.cc           # ループスタイルテスト
├── test_signature.cc            # Ed25519署名検証テスト
├── renode_test.cc               # Renodeテスト
├── bench_midi_format.cc         # MIDIフォーマットベンチマーク
├── bench_diode_ladder.cc        # Diode Ladderフィルタベンチマーク
├── bench_waveshaper.cc          # Waveshaperベンチマーク
├── bench_waveshaper_fast.cc     # Waveshaper高速版ベンチマーク
└── benchmark_span_renode.cc     # Span Renodeベンチマーク
```

---

## tools/ — ビルド・デバッグツール

```
tools/
├── keygen.py                    # Ed25519鍵ペア生成ツール
├── make_umia.py                 # .umiaファイル生成ツール（ヘッダ付与・署名）
├── set_sample_rate.py           # サンプルレート設定ツール
├── debug/                       # デバッグツール群
│   ├── check_audio.py           # オーディオ状態チェック
│   ├── check_audio_v2.py        # オーディオチェック v2
│   ├── debug_audio_now.py       # オーディオリアルタイムデバッグ
│   ├── monitor_audio.py         # オーディオモニタリング
│   ├── read_audio_stats.py      # オーディオ統計読み取り
│   ├── read_usb_desc.py         # USBディスクリプタ読み取り
│   ├── stm32_debug.py           # STM32デバッグ（pyOCD経由）
│   └── umi_debug.py             # UMI汎用デバッグ
├── python/
│   └── bench_waveshaper_plot.py # Waveshaperベンチマーク結果可視化
└── renode/                      # Renodeシミュレーション
    ├── *.resc                   # Renodeシナリオスクリプト
    ├── *.robot                  # Robot Frameworkテスト
    ├── *.repl                   # ペリフェラル定義
    ├── start_renode.sh          # Renode起動スクリプト
    ├── scripts/
    │   ├── audio_bridge.py      # オーディオブリッジ（Renode↔ホスト）
    │   ├── web_bridge.py        # Webブリッジ
    │   └── adc_injector.py      # ADCインジェクタ
    └── peripherals/             # カスタムRenodeペリフェラル
        ├── i2s_audio.py         # I2Sオーディオペリフェラル
        ├── midi_peripheral.py   # MIDIペリフェラル
        ├── control_peripheral.py # コントロールペリフェラル
        └── csharp/
            └── I2SAudioPeripheral.cs # I2Sペリフェラル（C#版）
```

---

## docs/ — ドキュメント

```
docs/
├── README.md                    # ドキュメントインデックス
├── MEMO.md                      # 開発メモ
├── NOMENCLATURE.md              # 命名規則・用語集
├── structure.md                 # 構造概要
├── event_state.md               # イベント・ステート設計
│
├── refs/                        # === リファレンス ===
│   ├── ARCHITECTURE.md          # 全体アーキテクチャ
│   ├── CONCEPTS.md              # C++ Conceptsガイド
│   ├── SECURITY.md              # セキュリティモデル
│   ├── UMIDSP_GUIDE.md          # DSPガイド
│   ├── UMIM.md                  # UMIM（WASM Module）仕様
│   ├── UMIM_NATIVE_SPEC.md      # UMIMネイティブ仕様
│   ├── UMIP.md                  # UMIP（Processor Interface）仕様
│   ├── UMIC.md                  # UMIC仕様
│   ├── API_APPLICATION.md       # アプリケーションAPI
│   ├── API_BSP.md               # BSP API
│   ├── API_DSP.md               # DSP API
│   ├── API_KERNEL.md            # カーネルAPI
│   └── API_UI.md                # UI API
│
├── dev/                         # === 開発ガイド ===
│   ├── CODING_STYLE.md          # コーディングスタイル詳細
│   ├── DEBUG_GUIDE.md           # デバッグガイド（→ lib/docs/guides/DEBUGGING_GUIDE.md に移動）
│   ├── GUIDELINE.md             # 開発ガイドライン
│   ├── TESTING.md               # テスト戦略
│   ├── SIMULATION.md            # シミュレーション手順
│   ├── LIBRARY_PACKAGING.md     # ライブラリパッケージング（→ lib/docs/standards/LIBRARY_SPEC.md に統合）
│   └── UAC2_DUPLEX_INVESTIGATION.md # UAC2デュプレックス調査
│
├── umi-kernel/                  # === カーネル設計 ===
│   ├── OVERVIEW.md              # カーネル概要
│   ├── ARCHITECTURE.md          # カーネルアーキテクチャ
│   ├── DESIGN_DECISIONS.md      # 設計判断
│   ├── BOOT_SEQUENCE.md         # ブートシーケンス
│   ├── MEMORY.md                # メモリマップ
│   └── spec/                    # 仕様書
│       ├── kernel.md            # カーネル仕様
│       ├── application.md       # アプリケーション仕様
│       ├── memory-protection.md # メモリ保護仕様
│       └── system-services.md   # システムサービス仕様
│
├── umi-sysex/                   # === SysExプロトコル ===
│   ├── UMI_SYSEX_OVERVIEW.md    # SysEx概要
│   ├── UMI_SYSEX_CONCEPT_MODEL.md # コンセプトモデル
│   ├── UMI_SYSEX_DATA.md        # データ仕様
│   └── UMI_SYSEX_TRANSPORT.md   # トランスポート層
│
├── umi-usb/                     # === USB Audio ===
│   ├── USB_AUDIO.md             # USB Audio設計
│   └── USB_AUDIO_REDESIGN_PLAN.md # USB Audio再設計計画
│
├── dsp/                         # === DSP設計資料 ===
│   ├── tb303/                   # TB-303再現
│   │   ├── vcf/                 # VCFフィルタ設計・解析
│   │   └── vco/                 # VCOオシレータ・Waveshaper設計
│   └── vafilter/
│       └── VAFILTER_DESIGN.md   # Virtual Analogフィルタ設計
│
├── hw_io/                       # === ハードウェアI/O処理設計 ===
│   ├── README.md                # 共通方針（ISR/処理ループ分離、周期設計）
│   ├── button.md                # ボタン入力処理
│   ├── encoder.md               # ロータリーエンコーダー入力処理
│   ├── potentiometer.md         # ポテンショメータ（ADC）入力処理
│   └── midi_uart.md             # Legacy MIDI over UART I/O処理
│
└── archive/                     # 旧設計文書アーカイブ
```

---

## 設計上の特徴

| 特徴 | 説明 |
|------|------|
| **ワンソース・マルチターゲット** | 同一の`process(AudioContext&)`コードが組み込み・WASM・プラグインで動作 |
| **Conceptsベース型システム** | vtableなし、ゼロオーバーヘッドのProcessorLike concept |
| **OS/アプリ完全分離** | カーネル(0x08000000〜)とアプリ(.umia, 0x08060000〜)をMPUで隔離 |
| **サンプル精度イベント処理** | EventにバッファI内サンプル位置を持ち、正確なタイミングで処理 |
| **リアルタイム安全性** | process()内はヒープ・例外・ブロッキング同期禁止 |
| **ヘッダオンリー設計** | 大部分がヘッダオンリーで組み込み環境でのインライン展開を最適化 |
| **C++23** | Concepts, std::span, constexpr活用、例外/RTTI不使用 |
