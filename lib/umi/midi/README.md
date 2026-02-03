# umidi - MIDI Library for Embedded

ARM Cortex-M向けに最適化されたMIDI 1.0/2.0処理ライブラリ。

## 特徴

- **UMP-Opt形式**: UMP32を単一`uint32_t`で格納、効率的な比較
- **ゼロアロケーション**: 静的メモリ管理のみ
- **ヘッダオンリー**: 依存関係なし

## クイックスタート

```cpp
#include <umidi.hh>

umidi::Parser parser;
umidi::UMP32 ump;

void on_midi_byte(uint8_t byte) {
    if (parser.parse(byte, ump) && ump.is_note_on()) {
        process_note(ump.note(), ump.velocity());
    }
}
```

## ビルド

```bash
xmake build umidi_test_core
xmake run umidi_test_core
```

## ドキュメント

- [docs/design.md](docs/design.md) - 設計思想
- [docs/PROTOCOL.md](docs/PROTOCOL.md) - UMI SysExプロトコル仕様
