// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library - Standard CC Definitions
// MIDI 1.0 specification compliant CC types
#pragma once

#include "types.hh"

namespace umidi::cc {

// =============================================================================
// 14-bit Control Changes (0-31 MSB, 32-63 LSB)
// =============================================================================

using BankSelect = CC14Bit<0, "Bank Select">;
using ModulationWheel = CC14Bit<1, "Modulation Wheel">;
using BreathController = CC14Bit<2, "Breath Controller">;
using FootController = CC14Bit<4, "Foot Controller">;
using PortamentoTime = CC14Bit<5, "Portamento Time">;
using DataEntry = CC14Bit<6, "Data Entry">;
using ChannelVolume = CC14Bit<7, "Channel Volume">;
using Balance = CC14Bit<8, "Balance">;
using Pan = CC14Bit<10, "Pan">;
using Expression = CC14Bit<11, "Expression">;
using EffectControl1 = CC14Bit<12, "Effect Control 1">;
using EffectControl2 = CC14Bit<13, "Effect Control 2">;
using GeneralPurpose1 = CC14Bit<16, "General Purpose 1">;
using GeneralPurpose2 = CC14Bit<17, "General Purpose 2">;
using GeneralPurpose3 = CC14Bit<18, "General Purpose 3">;
using GeneralPurpose4 = CC14Bit<19, "General Purpose 4">;

// =============================================================================
// Switch Controllers (64-69)
// =============================================================================

using SustainPedal = SwitchCC<64, "Sustain Pedal">;
using PortamentoSwitch = SwitchCC<65, "Portamento">;
using Sostenuto = SwitchCC<66, "Sostenuto">;
using SoftPedal = SwitchCC<67, "Soft Pedal">;
using LegatoFootswitch = SwitchCC<68, "Legato Footswitch">;
using Hold2 = SwitchCC<69, "Hold 2">;

// =============================================================================
// Sound Controllers (70-79)
// =============================================================================

using SoundController1 = CC7Bit<70, "Sound Variation">;
using SoundController2 = CC7Bit<71, "Timbre/Harmonic Content">;
using SoundController3 = CC7Bit<72, "Release Time">;
using SoundController4 = CC7Bit<73, "Attack Time">;
using SoundController5 = CC7Bit<74, "Brightness">;
using SoundController6 = CC7Bit<75, "Decay Time">;
using SoundController7 = CC7Bit<76, "Vibrato Rate">;
using SoundController8 = CC7Bit<77, "Vibrato Depth">;
using SoundController9 = CC7Bit<78, "Vibrato Delay">;
using SoundController10 = CC7Bit<79, "Sound Controller 10">;

// =============================================================================
// General Purpose Controllers (7-bit) (80-83)
// =============================================================================

using GeneralPurpose5 = CC7Bit<80, "General Purpose 5">;
using GeneralPurpose6 = CC7Bit<81, "General Purpose 6">;
using GeneralPurpose7 = CC7Bit<82, "General Purpose 7">;
using GeneralPurpose8 = CC7Bit<83, "General Purpose 8">;

// =============================================================================
// Other Controllers
// =============================================================================

using PortamentoControl = CC7Bit<84, "Portamento Control">;
using HighResVelocityPrefix = CC7Bit<88, "High Resolution Velocity Prefix">;

// =============================================================================
// Effects Depth (91-95)
// =============================================================================

using Effects1Depth = CC7Bit<91, "Reverb Send Level">;
using Effects2Depth = CC7Bit<92, "Tremolo Depth">;
using Effects3Depth = CC7Bit<93, "Chorus Send Level">;
using Effects4Depth = CC7Bit<94, "Celeste Depth">;
using Effects5Depth = CC7Bit<95, "Phaser Depth">;

// =============================================================================
// Data Entry and Parameter Selection (96-101)
// =============================================================================

using DataIncrement = CC7Bit<96, "Data Increment">;
using DataDecrement = CC7Bit<97, "Data Decrement">;
using NrpnLsb = CC7Bit<98, "NRPN LSB">;
using NrpnMsb = CC7Bit<99, "NRPN MSB">;
using RpnLsb = CC7Bit<100, "RPN LSB">;
using RpnMsb = CC7Bit<101, "RPN MSB">;

// =============================================================================
// Channel Mode Messages (120-127)
// =============================================================================

using AllSoundOff = CC7Bit<120, "All Sound Off">;
using ResetAllControllers = CC7Bit<121, "Reset All Controllers">;
using LocalControl = SwitchCC<122, "Local Control">;
using AllNotesOff = CC7Bit<123, "All Notes Off">;
using OmniModeOff = CC7Bit<124, "Omni Mode Off">;
using OmniModeOn = CC7Bit<125, "Omni Mode On">;
using MonoModeOn = CC7Bit<126, "Mono Mode On">;
using PolyModeOn = CC7Bit<127, "Poly Mode On">;

// =============================================================================
// Standard RPN Definitions
// =============================================================================

using RpnPitchBendSensitivity = RPN<0x0000, "Pitch Bend Sensitivity">;
using RpnFineTuning = RPN<0x0001, "Fine Tuning">;
using RpnCoarseTuning = RPN<0x0002, "Coarse Tuning">;
using RpnTuningProgramSelect = RPN<0x0003, "Tuning Program Select">;
using RpnTuningBankSelect = RPN<0x0004, "Tuning Bank Select">;
using RpnModulationDepthRange = RPN<0x0005, "Modulation Depth Range">;
using RpnMPEConfiguration = RPN<0x0006, "MPE Configuration">;
using RpnNull = RPN<0x3FFF, "Null (Reset)">;

} // namespace umidi::cc
