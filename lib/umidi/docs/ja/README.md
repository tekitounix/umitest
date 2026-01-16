# umidi - UMI-OS MIDIライブラリ

ARM Cortex-M向けに最適化された高性能MIDI 1.0/2.0処理ライブラリ。

## 特徴

- **UMP-Opt形式**: UMP32を単一の`uint32_t`として格納し効率的な比較を実現
- **シングルマスク型チェック**: `is_note_on()`, `is_cc()`が1オペレーションで完了
- **40%のメモリ削減**: 8バイト/イベント（従来の20バイトから）
- **ゼロコピーパース**: インクリメンタルなUMP構築
- **ヒープ割り当てなし**: 完全な静的メモリ管理

## クイックスタート

```cpp
#include <umidi/umidi.hh>

umidi::Parser parser;
umidi::UMP32 ump;

void process_byte(uint8_t byte) {
    if (parser.parse(byte, ump)) {
        if (ump.is_note_on()) {
            uint8_t ch = ump.channel();
            uint8_t note = ump.note();
            uint8_t vel = ump.velocity();
            // Note On処理
        }
    }
}
```

## ディレクトリ構成

```
lib/umidi/
├── README.md             # このファイル
├── xmake.lua             # ビルド設定
├── include/umidi/        # ヘッダーファイル
│   ├── umidi.hh          # メインヘッダー
│   ├── core/             # コア型
│   ├── messages/         # メッセージラッパー
│   └── protocol/         # UMI SysExプロトコル
├── test/                 # ユニットテスト
├── examples/             # サンプルコード
└── docs/                 # ドキュメント
```

## 独立性

umidiはUMI-OSコアから**完全に独立**しています:
- UMI-OSの型に依存しない
- 独自の名前空間`umidi::`を使用
- 単体でビルド・使用可能

## 要件

- C++23 (`std::expected`)
- 例外なし (`-fno-exceptions`互換)
- ヒープ割り当てなし
- リトルエンディアンターゲット (ARM, x86)

## ライセンス

MIT License
