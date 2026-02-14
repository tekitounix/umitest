# USB Audio Library Redesign Plan (Modern + Compact)

## 目的
組み込み向けに **サイズ/速度/静的構成**を維持しつつ、**簡単で一貫した API** に再設計する。
UAC1/UAC2・Audio IN/OUT・MIDI・SyncMode・Mute/Volume・可変サンプルレートを**網羅的**に扱う。

## 前提 (現状の要約)
* 現在は `AudioInterface` テンプレートの組み合わせで構成が固定。
* ディスクリプタ生成/Control/Streaming が単一クラスに集中。
* STM32 FS OTG の FIFO 割り当てが固定。
* 「簡単に設定できる高レベル API」が未整備。

## 設計方針 (モダン + コンパクト)
* **設定は 1 つの Config で完結**させ、詳細はデフォルト化。
* **静的構成優先**（ビルド時確定）でサイズ増加を抑える。
* 重要な機能は **コンパイル時に有効/無効**できるようにする。
* UAC1/UAC2 の差分は **抽象レイヤで吸収**し、利用者 API は共通化。
* **安全ガード**を持たせて「危険な組み合わせ」を防ぐ。
* **MIDI は umidi との統合を前提**に設計し、USB MIDI 部分は最小限に保つ。

## 新 API 案 (ユーザー視点)

### 1) 設定モデル
```text
AudioDeviceConfig
  - uac_version: UAC1 | UAC2
  - uac_mode_switch: fixed | manual_switch | auto_switch
  - usb_speed: FS | HS
  - sync_mode: Adaptive | Async | Sync
  - streams:
      out: enabled, channels, bit_depth, sample_rates(list)
      in:  enabled, channels, bit_depth, sample_rates(list)
  - feature_unit:
      mute: on/off, default_mute
      volume: on/off, min_db, max_db, step_db, default_db
  - sample_rate_control: fixed | selectable
  - channel_map:
      config_bits, l_r_swap
  - defaults:
      sample_rate, sync_mode
  - midi:
      enabled, cables, ep_out, ep_in
      backend: umidi
  - policy:
      allow_96k_fs: on/off
      allow_duplex_high_rate: on/off
      safe_bandwidth_margin: percent
```

### 2) 使い方 (例)
```text
auto cfg = AudioDeviceConfig::Preset::Uac1_48k_FullDuplex_Midi();
cfg.feature_unit.volume.enable(-63, 0, 1);
cfg.sample_rate_control = selectable({44100, 48000});

UsbAudioDevice<Stm32FsHal> dev(cfg);
dev.init();
```

## コアアーキテクチャ (分割)
1. **Config Layer**
   * 構成情報の集約・デフォルト化・安全チェック。
2. **Descriptor Builder**
   * Config からディスクリプタを自動生成。
   * UAC1/UAC2 の差分を内部に隠蔽。
3. **Control Handler**
   * Mute/Volume/SampleRate/ClockSource を統一処理。
4. **Streaming Engine**
   * RingBuffer/ASRC/Feedback/SyncMode を共通 API で切替。
5. **HAL Adapter**
   * STM32 OTG などの差分吸収。

## 機能カバレッジ (実装対象)
* Audio OUT/IN (Simplex/Full Duplex)
* Feature Unit (Mute/Volume)
* Sample Rate Control
  * UAC1: Endpoint Sampling Frequency Control
  * UAC2: Clock Source Entity
* Sync Mode (Async/Adaptive/Sync)
* MIDI (USB MIDI 1.0, Bulk)
  * umidi との接続に必要な最小 API を提供
* Default/Policy options
  * 初期ミュート/音量/サンプルレート
  * 帯域安全マージン設定
  * UAC1/UAC2 の切り替えモード

## サイズ/性能対策
* **constexpr + template の最小限使用**
  * 高レベル API は薄く、下層は static に固定。
* **不要機能をビルド時に除外**
  * Feature Unit, MIDI, IN/OUT を構成でオフにできる。
* **動的メモリ不使用**
  * リングバッファやディスクリプタは静的領域。
* **umidi 依存は薄いアダプタ層で隔離**
  * USB MIDI は転送とパケット変換のみ、解釈は umidi に委譲。

## 安全ガード (コンフィグ検証)
* FS で 96k/24bit Duplex を禁止 or 警告。
* チャンネル数/ビット深度/同期モードから **帯域計算**して判定。
* UAC1 FS では Adaptive をデフォルトに。
* UAC1/UAC2 は固定/手動/自動の切り替え方式を選択可能にする。

## umidi 統合計画
* **USB MIDI アダプタ層を新設**
  * USB MIDI パケット ↔ umidi メッセージ変換
  * コールバック/キューは umidi に集約
* **Audio/MIDI Composite の Descriptor を統合管理**
  * Audio 側の IAD と MIDI インターフェースの関係を明示
* **ビルド構成で MIDI を on/off**
  * `midi.enabled` で USB MIDI インターフェースを生成

## UAC1/UAC2 切り替え計画
* **fixed:** ビルド時に固定（サイズ最小）
* **manual_switch:** GPIO 等の物理スイッチで切替、PID も切替
* **auto_switch:** 切替条件を定義（OS判定は避け、ユーザー操作/設定を推奨）
* どの方式でも **USB 再接続を伴う**（再Enumerate）

## 実装ステップ (計画)
1. **Config 層の新設**
   * `AudioDeviceConfig` と Preset を定義。
2. **Descriptor Builder の抽象化**
   * 既存 `AudioInterface` の descriptor 生成を分離。
3. **Control Handler の統合**
   * UAC1/UAC2 のサンプルレート制御と Feature Unit を共通化。
4. **Streaming Engine の整理**
   * SyncMode 切替と Feedback/ASRC 統合。
5. **umidi アダプタ実装**
   * USB MIDI 転送と umidi を接続。
6. **HAL Adapter の分離**
   * FIFO 割り当てポリシーを HAL 側に寄せる。
7. **プリセット & ドキュメント**
   * 典型構成（48k FS、96k HS、UAC2 Async）をプリセット化。
8. **実機テスト**
   * 44.1/48/96k、UAC1/UAC2、Sync/Adaptive/Async の確認。

## 移行方針
* **完全に新規移行**する。
* 既存 `AudioInterface` との下位互換は維持しない。
* MIDI 部分は umidi の API を前提に置き換える。

## 成果物
* 新 Config API + Descriptor Builder
* UAC1/UAC2 統合ハンドラ
* 代表的プリセット
* 実機テスト手順
