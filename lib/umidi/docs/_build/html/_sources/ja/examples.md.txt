# 使用例

このページではumidiライブラリの実践的な使用例を紹介します。

## 基本的なMIDIパース

MIDIバイトストリームをパースしてメッセージを処理:

```cpp
#include <umidi/core/ump.hh>
#include <umidi/core/parser.hh>

umidi::Parser parser;
umidi::UMP32 ump;

void process_midi_byte(uint8_t byte) {
    if (parser.parse(byte, ump)) {
        if (ump.is_note_on()) {
            synth.note_on(ump.channel(), ump.note(), ump.velocity());
        } else if (ump.is_note_off()) {
            synth.note_off(ump.channel(), ump.note());
        } else if (ump.is_cc()) {
            params.set_cc(ump.cc_number(), ump.cc_value());
        }
    }
}
```

## 型安全なメッセージラッパー

コンパイル時の型安全性のためにメッセージラッパーを使用:

```cpp
#include <umidi/messages/channel_voice.hh>

using namespace umidi::message;

void handle_note(const NoteOn& msg) {
    printf("Note %d on channel %d, velocity %d\n",
           msg.note(), msg.channel(), msg.velocity());
}

// メッセージ作成
auto note = NoteOn::create(0, 60, 100);  // チャンネル0、中央C、ベロシティ100
handle_note(note);
```

## メッセージディスパッチ

型に基づいてハンドラーにディスパッチ:

```cpp
#include <umidi/messages/channel_voice.hh>

class Synthesizer {
public:
    void process(const umidi::UMP32& ump) {
        umidi::message::dispatch(ump, [this](auto&& msg) {
            handle(msg);
        });
    }

private:
    void handle(const umidi::message::NoteOn& msg) {
        voice_on(msg.note(), msg.velocity());
    }

    void handle(const umidi::message::NoteOff& msg) {
        voice_off(msg.note());
    }

    void handle(const umidi::message::ControlChange& msg) {
        if (msg.controller() == 1) {  // モジュレーションホイール
            set_modulation(msg.value());
        }
    }

    // その他のメッセージ型用のキャッチオール
    template<typename T>
    void handle(const T&) {}
};
```

## MIDI出力

UMP32をMIDI 1.0バイトに変換:

```cpp
#include <umidi/core/parser.hh>  // Serializerを含む

umidi::UMP32 ump = umidi::UMP32::note_on(0, 60, 100);

uint8_t buffer[3];
size_t len = umidi::Serializer::serialize(ump, buffer);

midi_send(buffer, len);  // 3バイト送信: 0x90 0x3C 0x64
```

## SysEx通信

双方向通信用のUMI SysExプロトコル:

```cpp
#include <umidi/protocol/message.hh>
#include <umidi/protocol/standard_io.hh>

using namespace umidi::protocol;

StandardIO<512, 512> stdio;

// ホストにテキスト送信
void print(const char* text) {
    stdio.write_stdout(
        reinterpret_cast<const uint8_t*>(text),
        strlen(text),
        [](const uint8_t* data, size_t len) {
            midi_send_sysex(data, len);
        }
    );
}

// stdin用コールバック設定
stdio.set_stdin_callback(
    [](const uint8_t* data, size_t len, void*) {
        process_command(data, len);
    },
    nullptr
);
```

## 割り込み安全な使用

リアルタイムオーディオコールバック用:

```cpp
// すべての操作がロックフリーで決定論的
void audio_callback(uint8_t* midi_in, size_t midi_len,
                    float* audio_out, size_t frames) {
    // MIDIパース（バイトあたり一定時間）
    for (size_t i = 0; i < midi_len; ++i) {
        umidi::UMP32 ump;
        if (parser_.parse(midi_in[i], ump)) {
            // メッセージ処理（一定時間）
            if (ump.is_note_on()) {
                synth_.note_on(ump.note(), ump.velocity());
            }
        }
    }

    // オーディオ生成
    synth_.render(audio_out, frames);
}
```
