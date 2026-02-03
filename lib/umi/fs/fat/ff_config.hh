// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include <cstdint>

namespace umi::fs::config {

// ============================================================================
// Function Configurations
// ============================================================================

constexpr int FS_READONLY = 0;  ///< 0: Read/Write, 1: Read-only
constexpr int FS_MINIMIZE = 0;  ///< 0: Full API
constexpr int USE_FIND = 0;     ///< 0: Disable f_findfirst/f_findnext
constexpr int USE_MKFS = 0;     ///< 0: Disable f_mkfs
constexpr int USE_FASTSEEK = 0; ///< 0: Disable fast seek
constexpr int USE_EXPAND = 0;   ///< 0: Disable f_expand
constexpr int USE_CHMOD = 0;    ///< 0: Disable f_chmod/f_utime
constexpr int USE_LABEL = 0;    ///< 0: Disable f_getlabel/f_setlabel
constexpr int USE_FORWARD = 0;  ///< 0: Disable f_forward
constexpr int USE_STRFUNC = 0;  ///< 0: Disable string functions

// ============================================================================
// Locale and Namespace
// ============================================================================

constexpr uint16_t CODE_PAGE = 437; ///< US ASCII
constexpr int USE_LFN = 1;          ///< 0: SFN only, 1: LFN with static BSS buffer
constexpr int MAX_LFN = 255;        ///< Max LFN length in UTF-16 code units
constexpr int LFN_UNICODE = 0;      ///< 0: ANSI/OEM (TCHAR = char)
constexpr int LFN_BUF = 255;
constexpr int SFN_BUF = 12;
constexpr int FS_RPATH = 0; ///< 0: Disable relative path

// ============================================================================
// Drive/Volume
// ============================================================================

constexpr int VOLUMES = 1;         ///< Single volume (SD card)
constexpr int STR_VOLUME_ID = 0;   ///< 0: No string volume IDs
constexpr int MULTI_PARTITION = 0; ///< 0: Single partition per drive
constexpr uint32_t MIN_SS = 512;
constexpr uint32_t MAX_SS = 512; ///< Fixed sector size
constexpr int LBA64 = 0;         ///< 0: 32-bit LBA
constexpr int USE_TRIM = 0;      ///< 0: No TRIM

// ============================================================================
// System
// ============================================================================

constexpr int FS_TINY = 0;  ///< 0: Normal buffer configuration
constexpr int FS_EXFAT = 0; ///< 0: No exFAT
constexpr int FS_NORTC = 1; ///< 1: No RTC — fixed timestamp
constexpr int NORTC_MON = 1;
constexpr int NORTC_MDAY = 1;
constexpr int NORTC_YEAR = 2025;
constexpr int FS_NOFSINFO = 0;
constexpr int FS_LOCK = 0;      ///< 0: No file lock
constexpr int FS_REENTRANT = 0; ///< 0: Not reentrant

} // namespace umi::fs::config
