# Control Change

Type-safe CC definitions and RPN/NRPN decoder.

## CC Types (cc/types.hh)

### 7-bit CC

```cpp
template <uint8_t CC, StringLiteral Name>
struct CC7Bit {
    static constexpr uint8_t number();
    static constexpr const char* name();
    static uint8_t parse(State&, uint8_t value);
    static uint8_t format(uint8_t value);
};
```

### 14-bit CC (MSB/LSB pair)

```cpp
template <uint8_t MSB, StringLiteral Name>
struct CC14Bit {
    static constexpr uint8_t number_msb();  // MSB CC number
    static constexpr uint8_t number_lsb();  // MSB + 32
    static uint16_t parse_msb(State&, uint8_t);
    static uint16_t parse_lsb(State&, uint8_t);
};
```

### Switch CC

```cpp
template <uint8_t CC, StringLiteral Name>
struct SwitchCC {
    static bool parse(State&, uint8_t value);  // >= 64 is on
    static uint8_t format(bool on);            // true=127, false=0
};
```

### RPN/NRPN

```cpp
template <uint16_t Number, bool IsNRPN = false, StringLiteral Name = "">
struct ParameterNumber {
    static constexpr uint16_t number();  // 14-bit parameter number
    static constexpr bool is_nrpn();
};

template <uint16_t Number, StringLiteral Name = "">
using RPN = ParameterNumber<Number, false, Name>;

template <uint16_t Number, StringLiteral Name = "">
using NRPN = ParameterNumber<Number, true, Name>;
```

## Standard CC Definitions (cc/standards.hh)

### 14-bit Controllers

```cpp
namespace umidi::cc {

using BankSelect      = CC14Bit<0, "Bank Select">;
using ModulationWheel = CC14Bit<1, "Modulation Wheel">;
using BreathController= CC14Bit<2, "Breath Controller">;
using FootController  = CC14Bit<4, "Foot Controller">;
using PortamentoTime  = CC14Bit<5, "Portamento Time">;
using DataEntry       = CC14Bit<6, "Data Entry">;
using ChannelVolume   = CC14Bit<7, "Channel Volume">;
using Balance         = CC14Bit<8, "Balance">;
using Pan             = CC14Bit<10, "Pan">;
using Expression      = CC14Bit<11, "Expression">;
using EffectControl1  = CC14Bit<12, "Effect Control 1">;
using EffectControl2  = CC14Bit<13, "Effect Control 2">;
```

### Switch Controllers

```cpp
using SustainPedal    = SwitchCC<64, "Sustain Pedal">;
using PortamentoSwitch= SwitchCC<65, "Portamento">;
using Sostenuto       = SwitchCC<66, "Sostenuto">;
using SoftPedal       = SwitchCC<67, "Soft Pedal">;
using LegatoFootswitch= SwitchCC<68, "Legato">;
using Hold2           = SwitchCC<69, "Hold 2">;
```

### Sound Controllers

```cpp
using SoundController1 = CC7Bit<70, "Sound Variation">;
using SoundController2 = CC7Bit<71, "Timbre/Harmonic Content">;
using SoundController3 = CC7Bit<72, "Release Time">;
using SoundController4 = CC7Bit<73, "Attack Time">;
using SoundController5 = CC7Bit<74, "Brightness">;
using SoundController6 = CC7Bit<75, "Decay Time">;
using SoundController7 = CC7Bit<76, "Vibrato Rate">;
using SoundController8 = CC7Bit<77, "Vibrato Depth">;
using SoundController9 = CC7Bit<78, "Vibrato Delay">;
using SoundController10= CC7Bit<79, "Undefined">;
```

### Effect Depths

```cpp
using ReverbSendLevel = CC7Bit<91, "Reverb Send">;
using TremoloDepth    = CC7Bit<92, "Tremolo Depth">;
using ChorusSendLevel = CC7Bit<93, "Chorus Send">;
using CelesteDepth    = CC7Bit<94, "Celeste Depth">;
using PhaserDepth     = CC7Bit<95, "Phaser Depth">;
```

### Channel Mode

```cpp
using AllSoundOff     = CC7Bit<120, "All Sound Off">;
using ResetAllControllers = CC7Bit<121, "Reset All Controllers">;
using LocalControl    = SwitchCC<122, "Local Control">;
using AllNotesOff     = CC7Bit<123, "All Notes Off">;
using OmniModeOff     = CC7Bit<124, "Omni Mode Off">;
using OmniModeOn      = CC7Bit<125, "Omni Mode On">;
using MonoModeOn      = CC7Bit<126, "Mono Mode On">;
using PolyModeOn      = CC7Bit<127, "Poly Mode On">;
```

### Standard RPNs

```cpp
using RpnPitchBendSensitivity = RPN<0x0000, "Pitch Bend Sensitivity">;
using RpnFineTuning           = RPN<0x0001, "Fine Tuning">;
using RpnCoarseTuning         = RPN<0x0002, "Coarse Tuning">;
using RpnTuningProgramSelect  = RPN<0x0003, "Tuning Program Select">;
using RpnTuningBankSelect     = RPN<0x0004, "Tuning Bank Select">;
using RpnModulationDepthRange = RPN<0x0005, "Modulation Depth Range">;
using RpnNull                 = RPN<0x3FFF, "Null (Reset)">;
```

### Pitch Bend Sensitivity Helper

```cpp
namespace pitch_bend_sensitivity {
    struct Value {
        uint8_t semitones;
        uint8_t cents;
    };

    Value parse(uint16_t rpn_value);
    uint16_t format(uint8_t semitones, uint8_t cents = 0);
}
```

## RPN/NRPN Decoder (cc/decoder.hh)

State machine for decoding RPN/NRPN message sequences.

```cpp
class ParameterNumberDecoder {
public:
    struct Result {
        uint16_t parameter_number;  // 14-bit parameter number
        uint16_t value;             // 14-bit value
        bool is_nrpn;               // true if NRPN, false if RPN
        bool complete;              // true if complete message
    };

    /// Decode CC message
    /// @param channel MIDI channel (0-15)
    /// @param cc_num CC number
    /// @param value CC value
    /// @return Decode result
    Result decode(uint8_t channel, uint8_t cc_num, uint8_t value);

    /// Check if specific RPN/NRPN is selected on channel
    template <typename T>
    bool is_selected(uint8_t channel) const;

    /// Get currently selected parameter on channel
    uint16_t get_selected(uint8_t channel) const;
    bool is_nrpn_selected(uint8_t channel) const;

    /// Reset decoder state
    void reset();
    void reset_channel(uint8_t channel);
};
```

### Usage Example

```cpp
umidi::cc::ParameterNumberDecoder decoder;

void on_cc(uint8_t ch, uint8_t cc, uint8_t val) {
    auto result = decoder.decode(ch, cc, val);

    if (result.complete) {
        if (result.parameter_number == 0x0000 && !result.is_nrpn) {
            // Pitch Bend Sensitivity
            auto pbs = umidi::cc::pitch_bend_sensitivity::parse(result.value);
            set_pitch_bend_range(pbs.semitones, pbs.cents);
        }
    }
}
```

## Template Static Decoder (codec/decoder.hh)

Compile-time optimized decoder - only instantiates code for used message types.

```cpp
template <ChannelConfig Config = default_config, typename... MessageTypes>
class Decoder {
public:
    template <typename T>
    static constexpr bool is_supported();

    Result<bool> decode_byte(uint8_t byte, UMP32& out);
    void reset();
    void set_group(uint8_t g);
};
```

### Pre-configured Decoders

```cpp
// Full MIDI 1.0 decoder
using FullDecoder = Decoder<default_config,
    message::NoteOn, message::NoteOff,
    message::ControlChange, message::ProgramChange,
    message::PitchBend, message::ChannelPressure, message::PolyPressure,
    message::TimingClock, message::Start, message::Continue, message::Stop
>;

// Minimal synth decoder (notes only)
using SynthDecoder = Decoder<default_config,
    message::NoteOn, message::NoteOff
>;

// Notes + expression decoder
using NoteCCDecoder = Decoder<default_config,
    message::NoteOn, message::NoteOff, message::ControlChange, message::PitchBend
>;

// Single channel decoder
template <uint8_t Channel, typename... MessageTypes>
using SingleChannelDecoder = Decoder<single_channel_config(Channel), MessageTypes...>;
```
