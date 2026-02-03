// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include <cstdint>

namespace umi::fs {

// ============================================================================
// Forward declarations
// ============================================================================

struct FatFsVolume;

// ============================================================================
// Result codes
// ============================================================================

enum class FatResult : uint8_t {
    OK = 0,
    DISK_ERR = 1,
    INT_ERR = 2,
    NOT_READY = 3,
    NO_FILE = 4,
    NO_PATH = 5,
    INVALID_NAME = 6,
    DENIED = 7,
    EXIST = 8,
    INVALID_OBJECT = 9,
    WRITE_PROTECTED = 10,
    INVALID_DRIVE = 11,
    NOT_ENABLED = 12,
    NO_FILESYSTEM = 13,
    MKFS_ABORTED = 14,
    TIMEOUT = 15,
    LOCKED = 16,
    NOT_ENOUGH_CORE = 17,
    TOO_MANY_OPEN_FILES = 18,
    INVALID_PARAMETER = 19,
};

// ============================================================================
// File access mode flags
// ============================================================================

constexpr uint8_t FA_READ = 0x01;
constexpr uint8_t FA_WRITE = 0x02;
constexpr uint8_t FA_OPEN_EXISTING = 0x00;
constexpr uint8_t FA_CREATE_NEW = 0x04;
constexpr uint8_t FA_CREATE_ALWAYS = 0x08;
constexpr uint8_t FA_OPEN_ALWAYS = 0x10;
constexpr uint8_t FA_OPEN_APPEND = 0x30;

// ============================================================================
// Filesystem type codes
// ============================================================================

constexpr uint8_t FS_FAT12 = 1;
constexpr uint8_t FS_FAT16 = 2;
constexpr uint8_t FS_FAT32 = 3;

// ============================================================================
// File attribute bits
// ============================================================================

constexpr uint8_t AM_RDO = 0x01;
constexpr uint8_t AM_HID = 0x02;
constexpr uint8_t AM_SYS = 0x04;
constexpr uint8_t AM_DIR = 0x10;
constexpr uint8_t AM_ARC = 0x20;

// ============================================================================
// Sector size (fixed at 512)
// ============================================================================

constexpr uint32_t SECTOR_SIZE = 512;

// ============================================================================
// Type aliases (replacing FatFs custom types)
// ============================================================================

using LBA_t = uint32_t;
using FSIZE_t = uint32_t;

// ============================================================================
// Object ID and allocation information
// ============================================================================

struct FatObjId {
    FatFsVolume* fs; ///< Pointer to the hosting volume
    uint16_t id;     ///< Volume mount ID when this object was opened
    uint8_t attr;    ///< Object attribute
    uint8_t stat;    ///< Object chain status
    uint32_t sclust; ///< Object data start cluster (0: no data or root dir)
    FSIZE_t objsize; ///< Object size (valid when sclust != 0)
};

// ============================================================================
// Filesystem object (FATFS equivalent)
// ============================================================================

struct FatFsVolume {
    uint8_t fs_type;    ///< Filesystem type (0: not mounted)
    uint8_t pdrv;       ///< Physical drive number
    uint8_t ldrv;       ///< Logical drive number
    uint8_t n_fats;     ///< Number of FATs (1 or 2)
    uint8_t wflag;      ///< win[] status (b0: dirty)
    uint8_t fsi_flag;   ///< FSINFO control (b7: disabled, b0: dirty)
    uint16_t id;        ///< Volume mount ID
    uint16_t n_rootdir; ///< Number of root directory entries (FAT12/16)
    uint16_t csize;     ///< Cluster size [sectors]

    // LFN working buffer (static, FF_USE_LFN == 1)
    uint16_t* lfnbuf;

    // Write tracking
    uint32_t last_clst; ///< Last allocated cluster
    uint32_t free_clst; ///< Number of free clusters

    uint32_t n_fatent; ///< Number of FAT entries (clusters + 2)
    uint32_t fsize;    ///< Sectors per FAT
    LBA_t winsect;     ///< Current sector in win[]
    LBA_t volbase;     ///< Volume base sector
    LBA_t fatbase;     ///< FAT base sector
    LBA_t dirbase;     ///< Root directory base (sector for FAT12/16, cluster for FAT32)
    LBA_t database;    ///< Data base sector

    uint8_t win[SECTOR_SIZE]; ///< Disk access window
};

// ============================================================================
// File object (FIL equivalent)
// ============================================================================

struct FatFile {
    FatObjId obj;     ///< Object identifier
    uint8_t flag;     ///< File status flags
    uint8_t err;      ///< Abort flag (error code)
    FSIZE_t fptr;     ///< File read/write pointer
    uint32_t clust;   ///< Current cluster of fptr
    LBA_t sect;       ///< Current sector in buf[]
    LBA_t dir_sect;   ///< Sector containing the directory entry
    uint8_t* dir_ptr; ///< Pointer to the directory entry in win[]

    uint8_t buf[SECTOR_SIZE]; ///< File private data window
};

// ============================================================================
// Directory object (DIR equivalent)
// ============================================================================

struct FatDir {
    FatObjId obj;   ///< Object identifier
    uint32_t dptr;  ///< Current read/write offset
    uint32_t clust; ///< Current cluster
    LBA_t sect;     ///< Current sector
    uint8_t* dir;   ///< Pointer to directory item in win[]
    uint8_t fn[12]; ///< SFN {body[0-7], ext[8-10], status[11]}

    // LFN support (FF_USE_LFN == 1)
    uint32_t blk_ofs; ///< Offset of current entry block
};

// ============================================================================
// File information (FILINFO equivalent)
// ============================================================================

constexpr int LFN_BUF = 255;
constexpr int SFN_BUF = 12;

struct FatFileInfo {
    FSIZE_t fsize;             ///< File size
    uint16_t fdate;            ///< Modification date
    uint16_t ftime;            ///< Modification time
    uint8_t fattrib;           ///< Attributes
    char altname[SFN_BUF + 1]; ///< Alternative (short) name
    char fname[LFN_BUF + 1];   ///< Primary (long) name
};

} // namespace umi::fs
