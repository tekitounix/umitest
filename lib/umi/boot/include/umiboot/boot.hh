// SPDX-License-Identifier: MIT
// umi_boot - Bootloader and Firmware Update Library
//
// Header-only C++23 library for embedded bootloader functionality.
// Features:
// - HMAC-SHA256 challenge-response authentication
// - Firmware header validation with Ed25519 signatures
// - A/B partition management with auto-rollback
// - Session timeout and sliding window flow control
//
// Usage:
//   #include <umi_boot/boot.hh>
//   using namespace umiboot;
//
// Requirements:
//   - C++23 compiler (std::span)
//   - No exceptions, no RTTI
//
#pragma once

#include "auth.hh"
#include "firmware.hh"
#include "bootloader.hh"
#include "session.hh"
