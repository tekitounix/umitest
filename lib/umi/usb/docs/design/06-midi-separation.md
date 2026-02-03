# 06 — MIDI 完全分離

> AudioInterface から MIDI Streaming を完全に分離し、独立した USB MIDI デバイスを構築可能にする。
> MIDI の EventRouter 連携と MIDI 2.0 対応は [05-midi-integration.md](05-midi-integration.md) を参照。

---

## 1. 現状の問題

AudioInterface に以下が直接組み込まれている:

- `MidiProcessor` メンバー (CIN パケットパース)
- `send_midi()` メソッド群
- MIDI EP の configure/ディスクリプタ生成
- `on_rx` 内の MIDI 分岐処理

この結合により:
- MIDI 単体デバイス (キーボード、コントローラー) が作れない
- AudioInterface のテンプレートが肥大化
- MIDI 2.0 対応が AudioInterface 全体に波及する

---

## 2. 分離後のクラス構造

```
UsbMidiClass<Hal>              // MIDI 単体デバイス
AudioClass<Hal, Config>        // Audio 単体デバイス (MIDI なし)
CompositeAudioMidiClass<Hal, Config>  // Audio + MIDI (IAD で結合)
```

---

## 3. UsbMidiClass の責務

```cpp
template<HalConcept Hal>
class UsbMidiClass {
public:
    // Class concept を満たす
    void on_configured(uint8_t config_value);
    void on_rx(uint8_t ep, const uint8_t* data, uint16_t len);
    void on_tx_complete(uint8_t ep);
    bool handle_class_request(const SetupPacket& setup, uint8_t* buf, uint16_t& len);
    bool handle_set_interface(uint8_t interface, uint8_t alt_setting);

    // MIDI 固有
    void set_raw_input_queue(RawInputQueue* queue);  // EventRouter 連携
    bool send_ump(const umidi::UMP32& ump);           // UMP → USB MIDI パケット

    // MIDI 2.0
    uint8_t current_alt_setting() const;  // 0=MIDI1.0, 1=MIDI2.0

    // ディスクリプタ
    static constexpr auto interface_descriptors();
    static constexpr auto string_descriptors();

private:
    umidi::Parser parser_;
    RawInputQueue* raw_input_queue_ = nullptr;
    uint8_t alt_setting_ = 0;  // MIDI 2.0 Alt Setting
};
```

---

## 4. CompositeAudioMidiClass

IAD で Audio Control + Audio Streaming + MIDI Streaming をグループ化する。

```cpp
template<HalConcept Hal, typename AudioConfig, typename MidiConfig = DefaultMidiConfig>
class CompositeAudioMidiClass {
    AudioClass<Hal, AudioConfig> audio_;
    UsbMidiClass<Hal> midi_;

    // Class concept の各メソッドで audio_ / midi_ にインターフェイス番号で委譲
};
```

AC Header の `baInterfaceNr` に MIDI Streaming インターフェイスを含める。

---

## 5. インターフェイス番号の管理

```
CompositeAudioMidiClass:
  Interface 0: Audio Control (AC)
  Interface 1: Audio Streaming OUT (AS)
  Interface 2: Audio Streaming IN (AS)  ← duplex 時のみ
  Interface N: MIDI Streaming (MS)
```

インターフェイス番号はテンプレートパラメータまたは Config から決定し、コンパイル時に確定する。

---

## 6. 移行計画

1. `midi/usb_midi_class.hh` に `UsbMidiClass` を新設
   - MidiProcessor のパース処理を移植
   - RawInputQueue への UMP32 投入 (EventRouter 連携)
   - UMP → CIN 逆変換 (送信)
2. `audio/audio_class.hh` に `AudioClass` を新設 (AudioInterface のリネーム/リファクタ)
   - MIDI 関連コードを除去
3. `composite_class.hh` に `CompositeAudioMidiClass` を新設
   - IAD 生成
   - インターフェイス番号の自動割り当て
   - リクエストのディスパッチ
4. 既存の `AudioInterface` を deprecated エイリアスとして残す (移行期間)
5. `umidi_adapter.hh` を非推奨化 (deprecated 警告)

---

## 7. テスト

| テスト | 内容 |
|--------|------|
| MIDI 分離テスト | UsbMidiClass 単体の送受信 |
| コンポジットテスト | CompositeAudioMidiClass のリクエスト委譲 |
| ディスクリプタ検証 | IAD + AC Header のバイト列を static_assert |
| 実機テスト | 各 OS での MIDI デバイス認識 |
