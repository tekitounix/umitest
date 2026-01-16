// SPDX-License-Identifier: MIT
// UMI-OS - Embedded adapter public header

#pragma once

#include "adapter.hh"

// This header provides the public API for running audio processors
// on embedded MCU platforms.
//
// Usage:
//
// #include <umi/embedded.hh>
// #include "my_synth.hh"
// #include "my_hw.hh"
//
// int main() {
//     MySynth synth;
//     umi::embedded::Adapter<MySynth, MyHw> adapter{synth};
//     
//     MyKernel kernel;
//     adapter.run(kernel);  // Never returns
// }
//
// Or for simple single-synth apps:
//
// int main() {
//     MySynth synth;
//     umi::embedded::run<MySynth, MyHw>(synth);  // Never returns
// }
