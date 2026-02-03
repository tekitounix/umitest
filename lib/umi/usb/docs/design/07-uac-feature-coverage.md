# 07 — UAC 機能網羅計画

> UAC 1.0/2.0 においてクラスコンプライアントなオーディオインターフェイス・電子楽器に必要な機能の実装計画。
> 仕様の詳細は [UAC_SPEC_REFERENCE.md](../UAC_SPEC_REFERENCE.md) を参照。

---

## 1. 優先度分類

| 優先度 | 基準 |
|--------|------|
| P0 (必須) | クラスコンプライアントに最低限必要 / 既に部分実装あり |
| P1 (重要) | オーディオインターフェイスや電子楽器として一般的 |
| P2 (推奨) | プロ用途や特殊デバイスで必要 |
| P3 (将来) | ニッチ用途、現時点では不要 |

---

## 2. エンティティ実装計画

| エンティティ | UAC1 | UAC2 | 優先度 | 計画 |
|-------------|------|------|--------|------|
| Input Terminal | ✅ | ✅ | — | 実装済み |
| Output Terminal | ✅ | ✅ | — | 実装済み |
| Feature Unit | ✅ (UAC1) | ❌ | **P0** | UAC2 にも追加。Mute + Volume |
| Clock Source | — | ✅ | — | 実装済み |
| Clock Selector | — | ❌ | P1 | 内部/外部クロック切り替え |
| Selector Unit | ❌ | ❌ | P1 | 入力ソース選択 (Line/Mic/S/PDIF) |
| Mixer Unit | ❌ | ❌ | P2 | マルチ入力ミキサー |
| Interrupt EP | ❌ | ❌ | P1 | 状態変化通知 |
| Processing Unit | ❌ | ❌ | P3 | Up/Down Mix |
| Effect Unit | — | ❌ | P3 | Reverb/Chorus |
| Extension Unit | ❌ | ❌ | P3 | ベンダー拡張 |
| Clock Multiplier | — | ❌ | P3 | 特殊用途 |
| Sample Rate Converter | — | ❌ | P3 | 特殊用途 |

---

## 3. Feature Unit コントロール拡張

| コントロール | 現状 | 優先度 | 計画 |
|-------------|------|--------|------|
| Mute | ✅ (UAC1) | **P0** | UAC2 にも追加 |
| Volume | ✅ (UAC1) | **P0** | UAC2 にも追加 |
| Input Gain | ❌ | P1 | UAC2 のマイク入力ゲイン |
| Bass/Mid/Treble | ❌ | P2 | ヘッドフォンアンプ等 |
| AGC | ❌ | P2 | マイク入力 |
| Phase Inverter | ❌ | P2 | プロ用 |
| Delay | ❌ | P3 | 特殊用途 |
| Graphic EQ | ❌ | P3 | 特殊用途 |
| Loudness | ❌ | P3 | コンシューマー用 |

---

## 4. Terminal Types 拡張

現在は USB Streaming, Speaker, Microphone のみ。

| Terminal Type | 値 | 優先度 | 用途 |
|--------------|-----|--------|------|
| Headphones | 0x0302 | P1 | ヘッドフォン出力 |
| Line In | 0x0501 | P1 | ライン入力 |
| Line Out | 0x0603 | P1 | ライン出力 |
| S/PDIF In | 0x0502 | P2 | デジタル入力 |
| S/PDIF Out | 0x0605 | P2 | デジタル出力 |
| Synthesizer | 0x0703 | P1 | シンセサイザー |
| Instrument | 0x0710 | P1 | 電子楽器 |
| Headset | 0x0402 | P2 | ヘッドセット |

descriptor.hh の `terminal` 名前空間に追加。

---

## 5. 同期モード

| モード | 現状 | 優先度 | 備考 |
|--------|------|--------|------|
| Async + Explicit FB | ✅ | — | 実装済み、推奨 |
| Adaptive | ❌ | P1 | SOF 追従。シンプルな実装が可能 |
| Synchronous | ❌ | P2 | SOF 同期。用途限定的 |
| Async + Implicit FB | ❌ | P3 | Windows 非対応 |

Adaptive モードは ASRC を使わず SOF に同期するため、Strategy 分離 ([04](04-isr-decoupling.md)) 後に
AdaptiveSyncStrategy として注入可能にする。

---

## 6. マルチチャンネル

| 項目 | 現状 | 優先度 | 計画 |
|------|------|--------|------|
| Stereo (2ch) | ✅ | — | 実装済み |
| Mono (1ch) | ✅ | — | 実装済み |
| 4ch | ❌ | P1 | Front L/R + Rear L/R or 4×独立 |
| 6ch+ | ❌ | P2 | サラウンド、マルチトラック |
| Channel Cluster | ❌ | P1 | UAC2 の空間チャンネル定義 |

チャンネル数はテンプレートパラメータで既に可変。
Channel Cluster (wChannelConfig / bmChannelConfig) のディスクリプタ生成を追加する必要がある。

---

## 7. サンプルレート / ビット深度

| 項目 | 現状 | 優先度 | 計画 |
|------|------|--------|------|
| 複数サンプルレート | ✅ | — | 実装済み (Alt Setting / Clock Range) |
| 16/24bit PCM | ✅ | — | 実装済み |
| 32bit PCM | ❌ | P1 | SubslotSize=4, BitResolution=32 |
| 32bit Float | ❌ | P2 | IEEE 754 float |
| DSD | ❌ | P3 | ニッチ |

---

## 8. ディスクリプタビルダー拡張

### 8-1. 新規追加するビルダー

descriptor.hh に以下を追加:

```cpp
namespace desc {

// --- Audio Control ---

// UAC2 Feature Unit
constexpr auto Uac2FeatureUnit(uint8_t unit_id, uint8_t source_id,
                                uint32_t controls_ch0, uint32_t controls_ch1, ...);

// Selector Unit (UAC1/2 共通)
constexpr auto SelectorUnit(uint8_t unit_id,
                             std::span<const uint8_t> source_ids,
                             uint8_t string_index = 0);

// Clock Selector (UAC2)
constexpr auto ClockSelector(uint8_t id,
                              std::span<const uint8_t> clock_source_ids,
                              uint8_t controls = 0, uint8_t string_index = 0);

// Mixer Unit (UAC1/2)
constexpr auto MixerUnit(uint8_t unit_id, ...);

// Interrupt Endpoint
constexpr auto InterruptEndpoint(uint8_t address, uint16_t interval);

// --- Terminal Types ---
namespace terminal {
    constexpr uint16_t Headphones      = 0x0302;
    constexpr uint16_t LineIn          = 0x0501;
    constexpr uint16_t DigitalIn       = 0x0502;
    constexpr uint16_t LineOut         = 0x0603;
    constexpr uint16_t DigitalOut      = 0x0605;
    constexpr uint16_t SpdifOut        = 0x0605;
    constexpr uint16_t Synthesizer     = 0x0703;
    constexpr uint16_t Instrument      = 0x0710;
    constexpr uint16_t Headset         = 0x0402;
}

}  // namespace desc
```

### 8-2. リクエストハンドラ拡張

AudioClass に以下のリクエストハンドラを追加:

| リクエスト | 対象 | 現状 | 計画 |
|-----------|------|------|------|
| GET/SET CUR Feature Unit | UAC2 | ❌ | P0: Mute/Volume |
| GET/SET CUR Selector Unit | UAC1/2 | ❌ | P1 |
| GET/SET CUR Clock Selector | UAC2 | ❌ | P1 |
| GET/SET CUR Mixer Unit | UAC1/2 | ❌ | P2 |
| Interrupt EP 送信 | AC | ❌ | P1 |

---

## 9. テスト

| テスト | 内容 |
|--------|------|
| UAC2 FU テスト | Volume/Mute の GET/SET CUR |
| Selector/Clock テスト | 入力/クロック選択の切り替え |
| Interrupt EP テスト | 状態変化通知の送信 |
| ディスクリプタ検証 | 新規ビルダーのバイト列を static_assert |
| 実機テスト (各OS) | Windows/macOS/Linux でのクラスコンプライアンス |
