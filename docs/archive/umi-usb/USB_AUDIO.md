# USB Audio & MIDI Design Guidelines: Implementation Strategy

## 1. Executive Summary (結論と推奨構成)

USBデバイス開発における、Audio (UAC) および MIDI クラスの推奨構成は以下の通りである。

| 機能カテゴリ | ターゲット | 推奨構成 | 備考・制約 |
| :--- | :--- | :--- | :--- |
| **Audio (汎用)** | ゲーム機, 古いPC, スマホ | **UAC1 / 48kHz / Adaptive** | 互換性重視。96kHzはホスト実装依存で不安定になりやすい。 |
| **Audio (双方向)** | ヘッドセット, 通話用 | **UAC1 / 48kHz / Adaptive** | 同時入出力 (Full Duplex) が可能。 |
| **Audio (高音質)** | Win10/11, Mac, マニア層 | **UAC2 / 192kHz~ / Async** | 帯域・音質ともに制限なし。現代の標準。 |
| **MIDI** | 全ターゲット共通 | **MIDI 1.0 / Bulk転送** | 最も互換性が高く安定している。 |

> **推奨実装アーキテクチャ:**
> * **物理切り替え:** スイッチ等で「UAC1モード」と「UAC2モード」を切り替え、**PID (Product ID) を変更**する。
> * **複合デバイス:** AudioとMIDIを同居させる場合は、必ず **IAD (Interface Association Descriptor)** を記述する。

---

## 2. UAC1 (USB Audio Class 1.0) の詳細仕様

### 2.1 推奨設定と帯域制限 (重要)
USB Full Speed (12Mbps) の物理帯域制限により、**サンプリングレートと同時入出力にはトレードオフが存在する**。

#### A. ハイレゾ再生モード (96kHz / 24bit)
* **構成:** 再生 (DAC) **のみ**、または 録音 (ADC) **のみ** を推奨。
* **制約:** **24bit / 2ch の Duplex は FS 帯域上ほぼ不可**。
    * **理由:**
        * 1msあたりのデータ量: 96kHz × 3Byte × 2ch = **576 Bytes**
        * 往復 (Duplex) の場合: 576 × 2 = **1,152 Bytes**
        * FS Isochronous 限界: **1,023 Bytes/ms**
        * **結論:** 24bit/2ch Duplex は物理的にパケットに入り切らない。
* **補足:** 16bit/2ch なら 96kHz Duplex でも帯域上は成立するが、ホスト側の実装により安定性が落ちるケースがある。
* **実測メモ:** macOS + FS UAC1 の 96kHz Duplex は **物理限界に近く実用的ではない**。帯域/バッファ負荷が高く、overrun/underrun が発生しやすい。Simplexなら成立する可能性はあるが、ホスト側が feedback を十分に反映しない場合は不安定になり得る。
* **注意 (UAC1/FS Async):** macOS では feedback を見ても **OUT パケットサイズの変動が ±1 サンプル/フレーム程度に留まる**という観測がある。大きなフィードバック値を送っても追従しないケースがあるため、**ファームウェア側の補正は ±1 を前提**に設計するのが安全。
* **補足 (16bit):** 16bit/2ch なら 96kHz Duplex でも帯域上は成立する（約 768 B/ms）。FS では余裕が少ないため、安定性は環境依存。

#### B. 通常モード (44.1kHz / 48kHz)
* **構成:** 再生 ＋ 録音 (Full Duplex)。
* **制約:** チャンネル数や MIDI/Feedback の同居で FS 帯域制限に近づくため、構成に応じた帯域計算が必要。

### 2.2 転送モード：Adaptive
* **推奨:** **Adaptive (アダプティブ)**
    * **理由:** Windows標準ドライバ (`usbaudio.sys`) との親和性が高く、Mac/スマホでも安定して動作するため。Asynchronous はホスト実装により挙動差が出る場合がある。

### 2.3 UAC1 必須ターゲット
* **PlayStation 5 / 4 / Switch** (これらはUAC2非対応)
* **Windows 7 / 8.1** (管理者権限がなくドライバを入れられない環境)
* **レガシーAndroid / 組み込み機器**

---

## 3. UAC2 (USB Audio Class 2.0) の詳細仕様

### 3.1 推奨設定：192kHz~ / Asynchronous
現代のハイエンド設計標準。USB High Speed (480Mbps) を使用するため、帯域制限は実質存在しない。

* **転送モード:** **Asynchronous (アシンクロナス)**
    * **理由:** デバイス側の高精度クロックをマスターとし、低ジッター再生を実現する。
* **実装要件:**
    * `Clock Source Entity` に対する `Sampling Frequency Control` として実装。
    * Windows 10 (Ver.1703以降) は標準ドライバ (`usbaudio2.sys`) で動作。

---

## 4. サンプリング周波数 実装リスト

ディスクリプタに記述すべき周波数の一覧と用途。
実装時は `Discrete` (離散値リスト) 形式での記述を推奨する。

### 4.1 音楽・メディア用途 (Standard / High-Res)
一般的なオーディオデバイスでサポートすべき周波数。

| 周波数 (Hz) | 用途 | UAC1 (FS) | UAC2 (HS) |
| :--- | :--- | :--- | :--- |
| **44,100** | CD, 音楽配信 | ◎ (必須) | ◎ |
| **48,000** | 動画, DVD, ゲーム機 | ◎ (必須) | ◎ |
| **88,200** | ハイレゾ (DSD制作系) | △ (再生のみ) | ◎ |
| **96,000** | ハイレゾ (スタジオ標準) | **△ (bit深度とch数に依存)** | ◎ |
| **176,400** | ハイレゾ, DoP DSD64 | × | ◎ |
| **192,000** | ハイレゾ, Blu-ray Audio | × | ◎ |
| **384,000** | 超ハイレゾ | × | ◎ |

### 4.2 通話・レガシー用途 (Voice / Legacy)
ヘッドセットや特定用途向けデバイスで必要となる低い周波数。

| 周波数 (Hz) | 用途 | 備考 |
| :--- | :--- | :--- |
| **8,000** | 電話音質 (Narrow band) | 通話アプリ、組み込み音声認識で負荷を下げる場合に必須。 |
| **16,000** | VoIP高音質 (Wide band) | 近年のWeb会議、音声認識エンジンの標準レート。 |
| **32,000** | FM放送, DAT, 旧世代ゲーム | レトロ互換や放送機器向け。音楽用DACでは通常不要。 |

---

## 5. USB MIDI の詳細と戦略

### 5.1 クラス構造と転送モード
MIDIは独立したクラスではなく、Audio Classの一部である。

* **クラス定義:**
    * **Parent:** Audio Class (0x01)
    * **Subclass:** **MIDI Streaming (0x03)**
    * *Note:* 実装時は必ず親となる `Audio Control (AC)` インターフェースとセットで記述する必要がある。
* **転送モード:** **Bulk (バルク) 転送**
    * Audio (Isochronous) と異なり、データ欠落が許されないためエラー訂正のあるBulkを使う。

### 5.2 MIDI 1.0 vs MIDI 2.0
現在は **USB MIDI 1.0** での実装が最も安全である。

| 規格 | 状況 | 推奨理由 |
| :--- | :--- | :--- |
| **USB MIDI 1.0** | **標準** | 枯れており、全てのDAW・OSでドライバレス動作する。パケット構造が単純(32bit)。 |
| **USB MIDI 2.0** | **過渡期** | 双方向通信によるネゴシエーション必須。OSやDAWの対応が進行中であり、トラブルの元になりやすい。 |

---

## 6. プラットフォーム別 互換性マトリクス

| プラットフォーム | Audio: UAC1 (Adaptive) | Audio: UAC2 (Async) | MIDI (1.0 Bulk) | 備考 |
| :--- | :--- | :--- | :--- | :--- |
| **Windows 10/11** | ◎ | ◎ | △ | **マルチクライアント問題:** 古いAPIでは1つのアプリがMIDIを占有する。 |
| **Windows 7/8.1** | ◎ | ✕ (要ドライバ) | △ | 同上。UAC2には外部ドライバ必須。 |
| **macOS** | ◎ | ◎ | ◎ | Audio/MIDI共にOS標準サポートが最強。 |
| **iOS** | ◎ | ◎ | ◎ | CoreMIDIによりPC同等の安定性。 |
| **Android** | ◎ | △ (機種依存) | ◯ | AudioはSRC(48kHz強制変換)に注意。MIDIはレイテンシに機種差あり。 |
| **PS5 / Switch** | **◎ (必須)** | ✕ | ✕ | **注意:** ゲーム機は汎用USB-MIDIドライバを持たないため、通常は認識しない。 |

---

## 7. 実装戦略：複合デバイス (Composite Device)

### 7.1 IAD (Interface Association Descriptor) の推奨
Windows等のOSにおいて、Audio機能とMIDI機能を正しく分離・認識させるために、ディスクリプタ構造内で **IAD** の使用を推奨する。

**ディスクリプタ構造例:**
```text
[Configuration]
  |
  +-- [IAD] (Audio Function Group)
  |    |
  |    +-- [Interface 0] Audio Control (AC) ... 全体の司令塔
  |    +-- [Interface 1] Audio Streaming (AS) ... 音声出力 (Speaker)
  |    +-- [Interface 2] Audio Streaming (AS) ... 音声入力 (Mic) *48kHz以下の場合のみ
  |
  +-- [Interface 3] MIDI Streaming (MS) ... MIDI入出力
```

### 7.2 UAC1/2 物理切り替えフロー
互換性を最大化するため、ユーザーによる物理切り替えを実装する。

1.  **ハードウェア:**
    * GPIOスイッチ等でモードを選択。
2.  **ファームウェアフロー:**
    * **Boot:** GPIO状態を確認。
    * **Mode UAC1:** PID `0x1234`, Descriptor `UAC1_Table` をロード。
        * *構成例:* 96kHz Output Only + MIDI
    * **Mode UAC2:** PID `0x1235`, Descriptor `UAC2_Table` をロード。
        * *構成例:* 192kHz In/Out + MIDI
    * **USB Init:** 選択された構成で初期化。

---

## 8. 根拠とソース

1.  **UAC1帯域制限:** USB 2.0 Specification / Audio Data Formats 1.0 (FS Iso Limit 1023B/frame).
    * *Calculation:* 96kHz/24bit/2ch needs 576B/ms. 576B * 2 (Duplex) > 1023B (Limit).
2.  **Windows UAC2対応:** Microsoft Hardware Dev Center (Win10 1703以降).
3.  **USB MIDI Class:** USB Device Class Definition for MIDI Devices v1.0.
4.  **ゲーム機仕様:** PS5/Switch実機検証および市場製品（FiiO, Creative等）の実装事実に基づく。

---

## 9. USB Full Speed (FS) 帯域使用率と「80%ルール」

USB FS (12Mbps) の物理限界は **1,023 Bytes/ms** だが、製品設計ではこれを使ってはいけない。

### 9.1 実用限界の理由 (The 80% Rule)
以下のオーバーヘッドにより、使用率が **90%** を超えると音飛びや認識不良のリスクが激増する。

1.  **クロック同期の変動 (+1 Sample):** Adaptive/Async転送では、クロック補正のために一時的にデータ量が増える瞬間がある（例: 48kHz時は49サンプル送るフレームがある）。
2.  **ビットスタッフィング:** 物理層で「1」の連続を防ぐために同期用ビットが挿入され、実効転送時間が増大する。
3.  **ホストコントローラー負荷:** PC側の割り込み処理遅延により、満タンに近いパケットはドロップされやすい。

### 9.2 USB FS 入出力構成の安全性判定マトリクス (UAC1/UAC2共通)

この判定は、**USB Full Speed (12Mbps)** で動作する全てのオーディオデバイスに適用される。

**基準:** 16bit / 48kHz (1ch = 96 Bytes/ms)
**安全ライン:** 使用率 **80% (約820 Bytes)** 以下を推奨。

| 構成 (In / Out) | 合計ch | 使用量 | 使用率 | 判定 | 評価 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **2 In / 2 Out** | 4ch | 384 B | 37% | **◎** | 安全。一般的。 |
| **4 In / 4 Out** | 8ch | 768 B | 75% | **◎** | **推奨上限 (Safe Limit)。** |
| **2 In / 6 Out** | 8ch | 768 B | 75% | **◎** | DJ機器等で実績あり。 |
| **6 In / 4 Out** | 10ch | 960 B | **94%** | **×** | **危険 (Risky)。** ノイズリスク高。 |
| **6 In / 6 Out** | 12ch | 1152 B | 112% | **×** | **物理不可 (Impossible)。** |

---

## 10. STM32 ハードウェア実装ガイド

USB帯域だけでなく、マイコン内部の「オーディオ端子不足」と「メモリ不足」に注意する。

### 10.1 ペリフェラル選定 (I2S vs SAI)

* **STM32F407 (I2Sのみ搭載):**
    * **限界:** **4 In / 4 Out** (I2S2 + I2S3 使用時)。
    * **注意:** TDMモードが使えないため、6ch以上の入力は物理的に困難。多チャンネルには不向き。
* **STM32F446 / F7 / H7 (SAI搭載):**
    * **推奨:** **SAI (Serial Audio Interface)** を使用。
    * **メリット:** TDMモードにより、1本のピンで 4ch / 8ch を多重伝送可能。
    * *構成例:* SAI1(8ch ADC) + SAI2(2ch DAC) = 8 In / 2 Out が容易。

### 10.2 USB FIFO メモリ割当 (F407の例)
STM32F407の USB FS FIFO は合計 **1.25 KB (1280 Bytes)**。
多チャンネル時はデフォルト設定では溢れるため、手動調整が必須。

**推奨設定例 (4 In / 4 Out 時):**
* RX FIFO: 256 B (共通受信用)
* TX EP0: 64 B (制御用)
* **TX EP1 (Audio In): 800 B** (重要: データ768B + マージン)
* TX EP2 (MIDI In): 64 B
* *合計:* 1184 Bytes ( < 1280 Bytes OK )

---

## 11. USB Audio ライブラリ再設計（汎用化）方針

この章は、UAC1/UAC2 の一般的な機能を網羅しつつ、設定を簡潔化した **汎用 USB Audio ライブラリ** の設計と実装計画をまとめる。

### 11.1 設計目標
* **設定を最小化:** 主要項目のみで構成できる（プリセット + オプション）。
* **UAC1/UAC2 両対応:** 同一 API で差分を吸収。
* **機能網羅:** mute/volume、可変サンプルレート、duplex、MIDI 併設。
* **実用的な範囲に限定:** FS帯域・ホスト依存を踏まえた安全な構成を優先。

### 11.2 設定モデル（高レベル API）
**基本方針:** “AudioDeviceConfig 1つ”で完結。詳細はデフォルト値で自動設定。

```text
AudioDeviceConfig
  - uac_version: UAC1 | UAC2
  - speed: FS | HS
  - sync_mode: Adaptive | Async | Sync
  - in/out:
      channels, bit_depth, sample_rates (list)
  - features:
      mute: on/off
      volume: on/off + dB range/step
      sample_rate_control: fixed | selectable
  - midi: off | on (cables, endpoints)
  - policy:
      allow_96k_fs: on/off (危険回避フラグ)
      duplex_policy: full | simplex_only
```

### 11.3 機能サポート一覧（UAC1/2の一般的機能）
* **Audio OUT/IN (Simplex/Full Duplex)**
* **Mute/Volume (Feature Unit)**
* **Sample Rate Control**
  * UAC1: Endpoint Sampling Frequency Control
  * UAC2: Clock Source Entity Control
* **Sync Mode**
  * UAC1 FS: Adaptive を推奨、Async はホスト依存
  * UAC2 HS: Async を推奨
* **MIDI (USB MIDI 1.0, Bulk)**

### 11.4 内部アーキテクチャ（モジュール分割）
* **Config → Descriptor Builder**
  * 設定からディスクリプタを生成（UAC1/UAC2の差分吸収）
* **Control Request Handler**
  * Feature Unit / Sampling Frequency Control / Clock Source の統一処理
* **Streaming Engine**
  * RingBuffer / ASRC / Feedback / Endpoint scheduling
* **HAL Adapter**
  * STM32 OTG / 他 MCU の USB HAL 差分吸収

### 11.5 実装方針（簡略化のためのルール）
* **プリセット優先:** 典型構成は “プリセット + 1〜2項目変更”で足りるようにする。
* **安全ガード:** FS で 96k/24bit Duplex のような構成は警告 or 自動調整。
* **同期モードの自動選択:** UAC1 FS は Adaptive、UAC2 HS は Async を既定。

### 11.6 実装計画（段階的）
1. **設定モデルの導入**
   * `AudioDeviceConfig` を新設し、既存テンプレート構成をラップ。
2. **ディスクリプタ自動生成**
   * UAC1/UAC2 の共通化・分岐を抽象化。
3. **Control Request 統合**
   * UAC1/2 の Sample Rate / Mute / Volume を単一ハンドラで処理。
4. **Streaming エンジン整理**
   * Sync/Adaptive/Async の実装を共通 API で切り替え。
5. **検証 & 実機テスト**
   * 48k FS, 96k FS, 96k HS での安定性を比較評価。

### 11.7 期待される効果
* **設定が明確化され、誤構成を防止**
* **UAC1/2 を同じ設計思想で扱える**
* **ホスト依存の罠（96k FS など）を事前に回避可能**

---

## 12. 再設計要点（設定オプションと切替方針の要約）

詳細な設計・実装計画は `docs/development/USB_AUDIO_REDESIGN_PLAN.md` を参照。

### 12.1 設定オプション（追加・拡張）
* **UAC1/UAC2 切替方式:** fixed / manual_switch / auto_switch
* **Feature Unit 初期値:** default_mute / default_db
* **サンプルレート制御:** fixed / selectable（UAC1: EP、UAC2: Clock Source）
* **チャンネルマップ:** config_bits / L-R swap
* **デフォルト値:** sample_rate / sync_mode
* **ポリシー:** safe_bandwidth_margin（帯域安全マージン）
* **MIDI:** umidi 統合（USB MIDI は薄いアダプタ層）

### 12.2 切替方針（UAC1/UAC2）
* **fixed:** ビルド時固定（サイズ最小）
* **manual_switch:** GPIO等の物理スイッチで切替、PIDも切替
* **auto_switch:** 切替条件を定義（OS判定は避け、ユーザー操作/設定を推奨）
* **注意:** いずれも USB 再接続を伴う（再Enumerate）


メモ
市販のUAC1デバイスはAdaptiveが多いがその仕組みはハードウェア実装であることが多い
ASRCで実装すれば良さそうだが安定した実装例がみつからない
UAC1のAsyncはサンプルレート高速切り替え時にグリッチが発生し、収束するまで数秒かかる
UAC1のAsyncでmacOSでLogic Pro使用時にAudio MIDI Setupの方でサンプルレート変更を行うと数秒間音がグリッチする問題がある
