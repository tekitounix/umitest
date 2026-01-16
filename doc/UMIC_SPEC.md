# UMI-Controller (UMIC) 仕様書

**バージョン:** 2.0.0-draft
**ステータス:** ドラフト

## 概要

UMI-Controller (UMIC) は、UIロジック/インタラクション状態を管理するControllerの仕様です。

- **オプション** - モードを持たないモジュールでは不要
- **ハードウェア非依存** - 実I/Oはアダプタ/カーネル層が担当

## いつ必要か

**モジュールがUI状態（モード）を持つ場合**に必要。

| 機能 | UMIC必要 | 備考 |
|------|----------|------|
| パラメータ選択/ページ切替 | ✓ | UI状態 |
| MIDI Learn（モジュール内管理） | ✓ | マッピング状態 |
| プリセット管理（モジュール内） | ✓ | 一覧/選択状態 |
| 専用ハードウェアUI | ✓ | モード遷移 |
| DSP処理のみ | ✗ | UMIP単体でOK |
| 汎用UI（ホスト/カーネル提供） | ✗ | モジュールは関与しない |

### ホスト/カーネルが管理する場合

DAWやヘッドレス対応カーネルでは:

```
┌─────────────────────────────────────────────┐
│  ホスト / カーネル                            │
│  - 汎用UI（パラメータ選択）                   │
│  - MIDI Learn                               │
│  - プリセット管理                            │
│  - params[] を読んで自動生成                 │
└─────────────────────────────────────────────┘
                    │
                    ▼
         ┌──────────────────┐
         │  UMIP (ヘッドレス) │
         │  params[] 公開    │
         │  UMIC不要         │
         └──────────────────┘
```

## MVCにおける位置づけ

UMICは**Controller**に相当。

```
┌─────────────────────────────────────────────┐
│  アプリケーション層                           │
│  ┌─────────────────┐  ┌─────────────────┐   │
│  │  UMIP (Model)   │←─│  UMIC (Ctrl)    │   │
│  │  DSP処理        │  │  UIロジック      │   │
│  │  パラメータ値    │  │  UI状態         │   │
│  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────┘
```

## Controllerインターフェース

### 最小

```cpp
struct Controller {
    Processor& proc;

    explicit Controller(Processor& p) : proc(p) {}
};
```

Processorのパラメータ（メンバ変数）には `proc.` で直接アクセス。

### オプションメソッド

```cpp
struct Controller {
    Processor& proc;
    explicit Controller(Processor& p) : proc(p) {}

    // イベント処理（SysEx, Program Change等）
    void on_events(std::span<const Event> events);

    // UI入力
    void on_encoder(int id, int delta);
    void on_button(int id, bool pressed);

    // 定期更新（メーター、スムージング等）
    void update(float delta_time);

    // 状態保存
    size_t save_state(std::span<uint8_t> buffer);
    bool load_state(std::span<const uint8_t> data);
};
```

すべてのメソッドはオプション。必要なものだけ実装。

## パラメータアクセス

ControllerはProcessorのメンバ変数に直接アクセス。

```cpp
void on_encoder(int id, int delta) {
    proc.volume += delta * 0.01f;  // 直接アクセス
}
```

## イベント処理

### 処理順序

```
MIDI IN → on_events() → process()
              │              │
           SysEx等       Note/CC
```

同じイベントリストがUMIC→UMIPの順で渡される。各層は必要なイベントのみ処理。

### 実装例

```cpp
void SynthController::on_events(std::span<const Event> events) {
    for (const auto& e : events) {
        if (e.type != EventType::Midi) continue;

        if (e.midi.is_sysex()) {
            handle_sysex(e);
        } else if (e.midi.is_program_change()) {
            load_preset(e.midi.program());
        } else if (midi_learn_active && e.midi.is_cc()) {
            cc_mapping[e.midi.cc_number()] = selected_param;
            midi_learn_active = false;
        }
        // Note, CC等はUMIPで処理
    }
}
```

## UI入力

ハードウェア入力を抽象化。

```cpp
void SynthController::on_encoder(int id, int delta) {
    // selected_param に応じてメンバに直接アクセス
    switch (selected_param) {
        case 0: proc.cutoff += delta * 10.0f; break;
        case 1: proc.resonance += delta * 0.01f; break;
    }
}

void SynthController::on_button(int id, bool pressed) {
    if (id == LEARN_BUTTON && pressed) {
        midi_learn_active = true;
    }
}
```

## 実装例

### 最小のController

```cpp
struct VolumeController {
    Volume& proc;

    explicit VolumeController(Volume& p) : proc(p) {}
};
```

### UI状態を持つController

```cpp
struct SynthController {
    Synth& proc;

    // UI状態
    int page = 0;
    int selected_param = 0;
    bool midi_learn_active = false;
    std::array<int, 128> cc_mapping{};

    explicit SynthController(Synth& p) : proc(p) {
        cc_mapping[74] = 0;  // CC74 → Cutoff
    }

    void on_events(std::span<const Event> events);
    void on_encoder(int id, int delta);
    void on_button(int id, bool pressed);
};
```

## ライセンス

CC0 1.0 Universal (パブリックドメイン)

---

**UMI-OS プロジェクト**
