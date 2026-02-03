// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix
// Unicode conversion for FatFs (CP437 only)

#pragma once

#include <cstdint>

namespace umi::fs {

/// OEM code to Unicode conversion (CP437)
uint16_t ff_oem2uni(uint16_t oem, uint16_t cp);

/// Unicode to OEM code conversion (CP437)
uint16_t ff_uni2oem(uint32_t uni, uint16_t cp);

/// Unicode upper-case conversion
uint32_t ff_wtoupper(uint32_t uni);

} // namespace umi::fs
