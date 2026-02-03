// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix
// FAT12/16/32 implementation for the UMI framework
// Implemented from Microsoft FAT specification

#include <cstring>

#include "ff.hh"
#include "ff_unicode.hh"

namespace umi::fs {

// ============================================================================
// Internal constants
// ============================================================================

static constexpr uint32_t SS_VAL = 512;

// Name status flags (index 11 of fn[])
static constexpr uint8_t NSFLAG = 11;
static constexpr uint8_t NS_LOSS = 0x01;
static constexpr uint8_t NS_LFN = 0x02;
static constexpr uint8_t NS_DOT = 0x20;
static constexpr uint8_t NS_NOLFN = 0x40;
static constexpr uint8_t NS_NONAME = 0x80;

// Internal file flags
static constexpr uint8_t FA_SEEKEND = 0x20;
static constexpr uint8_t FA_MODIFIED = 0x40;
static constexpr uint8_t FA_DIRTY = 0x80;

// Attribute masks
static constexpr uint8_t AM_LFN = 0x0F;
static constexpr uint8_t AM_VOL = 0x08;

// Directory entry constants
static constexpr uint32_t DIR_ENTRY_SIZE = 32;
static constexpr uint8_t DDEM = 0xE5;  // Deleted directory entry marker
static constexpr uint8_t RDDEM = 0x05; // Replacement for 0xE5 as first char
static constexpr uint8_t LLEF = 0x40;  // Last Long Entry Flag

// LFN character offsets within a directory entry
static constexpr uint8_t LFN_OFFSETS[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
static constexpr int LFN_CHARS_PER_ENTRY = 13;

// BPB field offsets
static constexpr int BPB_BYTS_PER_SEC = 11;
static constexpr int BPB_SEC_PER_CLUS = 13;
static constexpr int BPB_RSVD_SEC_CNT = 14;
static constexpr int BPB_NUM_FATS = 16;
static constexpr int BPB_ROOT_ENT_CNT = 17;
static constexpr int BPB_TOT_SEC16 = 19;
static constexpr int BPB_FAT_SZ16 = 22;
static constexpr int BPB_TOT_SEC32 = 32;
static constexpr int BPB_FAT_SZ32 = 36;
static constexpr int BPB_ROOT_CLUS32 = 44;
static constexpr int BPB_FSINFO32 = 48;
static constexpr int BS_FILESYSTYPE = 54;   // FAT12/16
static constexpr int BS_FILESYSTYPE32 = 82; // FAT32

// Directory entry field offsets
static constexpr int DIR_NAME = 0;
static constexpr int DIR_ATTR = 11;
static constexpr int DIR_NTRES = 12;
static constexpr int DIR_CRT_TIME = 14;
static constexpr int DIR_CRT_DATE = 16;
static constexpr int DIR_FST_CLUS_HI = 20;
static constexpr int DIR_WRT_TIME = 22;
static constexpr int DIR_WRT_DATE = 24;
static constexpr int DIR_FST_CLUS_LO = 26;
static constexpr int DIR_FILE_SIZE = 28;

// LFN entry field offsets
static constexpr int LDIR_ORD = 0;
static constexpr int LDIR_ATTR = 11;
static constexpr int LDIR_TYPE = 12;
static constexpr int LDIR_CHKSUM = 13;
static constexpr int LDIR_FST_CLUS_LO = 26;

// FSInfo offsets
static constexpr int FSI_LEAD_SIG = 0;
static constexpr int FSI_STRUC_SIG = 484;
static constexpr int FSI_FREE_COUNT = 488;
static constexpr int FSI_NXT_FREE = 492;
static constexpr int FSI_TRAIL_SIG = 508;

// ExCvt table for CP437 upper-case conversion (0x80-0xFF)
static constexpr uint8_t EXCVT[] = {
    0x80, 0x9A, 0x45, 0x41, 0x8E, 0x41, 0x8F, 0x80, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x8E, 0x8F, 0x90, 0x92, 0x92,
    0x4F, 0x99, 0x4F, 0x55, 0x55, 0x59, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0x41, 0x49, 0x4F, 0x55, 0xA5, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
    0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
    0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

// ============================================================================
// Little-endian load/store helpers
// ============================================================================

static inline uint16_t ld16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static inline uint32_t ld32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static inline void st16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

static inline void st32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

// ============================================================================
// Disk I/O wrappers
// ============================================================================

uint8_t FatFs::disk_initialize(uint8_t /*pdrv*/) noexcept {
    if (!diskio || !diskio->initialize)
        return STA_NOINIT;
    return diskio->initialize(diskio->context);
}

uint8_t FatFs::disk_status(uint8_t /*pdrv*/) noexcept {
    if (!diskio || !diskio->status)
        return STA_NOINIT;
    return diskio->status(diskio->context);
}

DiskResult FatFs::disk_read(uint8_t /*pdrv*/, uint8_t* buff, LBA_t sector, uint32_t count) noexcept {
    if (!diskio || !diskio->read)
        return DiskResult::NOTRDY;
    return diskio->read(diskio->context, buff, sector, count);
}

DiskResult FatFs::disk_write(uint8_t /*pdrv*/, const uint8_t* buff, LBA_t sector, uint32_t count) noexcept {
    if (!diskio || !diskio->write)
        return DiskResult::NOTRDY;
    return diskio->write(diskio->context, buff, sector, count);
}

DiskResult FatFs::disk_ioctl(uint8_t /*pdrv*/, uint8_t cmd, void* buff) noexcept {
    if (!diskio || !diskio->ioctl)
        return DiskResult::NOTRDY;
    return diskio->ioctl(diskio->context, cmd, buff);
}

// ============================================================================
// get_fattime — fixed timestamp (FS_NORTC=1)
// ============================================================================

uint32_t FatFs::get_fattime() noexcept {
    return (static_cast<uint32_t>(config::NORTC_YEAR - 1980) << 25) | (static_cast<uint32_t>(config::NORTC_MON) << 21) |
           (static_cast<uint32_t>(config::NORTC_MDAY) << 16);
}

// ============================================================================
// Window buffer management
// ============================================================================

FatResult FatFs::sync_window(FatFsVolume* fs) noexcept {
    if (fs->wflag) {
        // Write back dirty window
        if (disk_write(fs->pdrv, fs->win, fs->winsect, 1) != DiskResult::OK) {
            return FatResult::DISK_ERR;
        }
        fs->wflag = 0;
        // Mirror to second FAT if within FAT area
        if (fs->winsect - fs->fatbase < fs->fsize) {
            for (uint8_t nf = 1; nf < fs->n_fats; nf++) {
                if (disk_write(fs->pdrv, fs->win, fs->winsect + fs->fsize * nf, 1) != DiskResult::OK) {
                    return FatResult::DISK_ERR;
                }
            }
        }
    }
    return FatResult::OK;
}

FatResult FatFs::move_window(FatFsVolume* fs, LBA_t sect) noexcept {
    if (sect != fs->winsect) {
        auto res = sync_window(fs);
        if (res != FatResult::OK)
            return res;
        if (disk_read(fs->pdrv, fs->win, sect, 1) != DiskResult::OK) {
            fs->winsect = 0xFFFFFFFF;
            return FatResult::DISK_ERR;
        }
        fs->winsect = sect;
    }
    return FatResult::OK;
}

// ============================================================================
// Cluster / sector conversion
// ============================================================================

LBA_t FatFs::clst2sect(FatFsVolume* fs, uint32_t clst) noexcept {
    if (clst < 2 || clst >= fs->n_fatent)
        return 0;
    return fs->database + static_cast<LBA_t>(clst - 2) * fs->csize;
}

// ============================================================================
// FAT access
// ============================================================================

uint32_t FatFs::get_fat(FatObjId* obj, uint32_t clst) noexcept {
    auto* fs = obj->fs;
    if (clst < 2 || clst >= fs->n_fatent)
        return 1; // error value

    switch (fs->fs_type) {
    case FS_FAT12: {
        uint32_t byte_ofs = clst + (clst / 2); // 1.5 bytes per entry
        LBA_t sect = fs->fatbase + (byte_ofs / SS_VAL);
        uint32_t ofs = byte_ofs % SS_VAL;

        if (move_window(fs, sect) != FatResult::OK)
            return 1;
        uint32_t val = fs->win[ofs];

        // May cross sector boundary
        if (ofs == SS_VAL - 1) {
            if (move_window(fs, sect + 1) != FatResult::OK)
                return 1;
            val |= static_cast<uint32_t>(fs->win[0]) << 8;
        } else {
            val |= static_cast<uint32_t>(fs->win[ofs + 1]) << 8;
        }

        return (clst & 1) ? (val >> 4) : (val & 0xFFF);
    }
    case FS_FAT16: {
        LBA_t sect = fs->fatbase + (clst * 2 / SS_VAL);
        uint32_t ofs = (clst * 2) % SS_VAL;
        if (move_window(fs, sect) != FatResult::OK)
            return 1;
        return ld16(&fs->win[ofs]);
    }
    case FS_FAT32: {
        LBA_t sect = fs->fatbase + (clst * 4 / SS_VAL);
        uint32_t ofs = (clst * 4) % SS_VAL;
        if (move_window(fs, sect) != FatResult::OK)
            return 1;
        return ld32(&fs->win[ofs]) & 0x0FFFFFFF;
    }
    default:
        return 1;
    }
}

FatResult FatFs::put_fat(FatFsVolume* fs, uint32_t clst, uint32_t val) noexcept {
    if (clst < 2 || clst >= fs->n_fatent)
        return FatResult::INT_ERR;

    switch (fs->fs_type) {
    case FS_FAT12: {
        uint32_t byte_ofs = clst + (clst / 2);
        LBA_t sect = fs->fatbase + (byte_ofs / SS_VAL);
        uint32_t ofs = byte_ofs % SS_VAL;

        if (move_window(fs, sect) != FatResult::OK)
            return FatResult::DISK_ERR;

        if (clst & 1) {
            fs->win[ofs] = static_cast<uint8_t>((fs->win[ofs] & 0x0F) | ((val & 0x0F) << 4));
        } else {
            fs->win[ofs] = static_cast<uint8_t>(val & 0xFF);
        }
        fs->wflag = 1;

        // Second byte — may cross sector boundary
        if (ofs == SS_VAL - 1) {
            if (move_window(fs, sect + 1) != FatResult::OK)
                return FatResult::DISK_ERR;
            ofs = 0;
        } else {
            ofs++;
        }

        if (clst & 1) {
            fs->win[ofs] = static_cast<uint8_t>((val >> 4) & 0xFF);
        } else {
            fs->win[ofs] = static_cast<uint8_t>((fs->win[ofs] & 0xF0) | ((val >> 8) & 0x0F));
        }
        fs->wflag = 1;
        break;
    }
    case FS_FAT16: {
        LBA_t sect = fs->fatbase + (clst * 2 / SS_VAL);
        uint32_t ofs = (clst * 2) % SS_VAL;
        if (move_window(fs, sect) != FatResult::OK)
            return FatResult::DISK_ERR;
        st16(&fs->win[ofs], static_cast<uint16_t>(val));
        fs->wflag = 1;
        break;
    }
    case FS_FAT32: {
        LBA_t sect = fs->fatbase + (clst * 4 / SS_VAL);
        uint32_t ofs = (clst * 4) % SS_VAL;
        if (move_window(fs, sect) != FatResult::OK)
            return FatResult::DISK_ERR;
        val = (ld32(&fs->win[ofs]) & 0xF0000000) | (val & 0x0FFFFFFF);
        st32(&fs->win[ofs], val);
        fs->wflag = 1;
        break;
    }
    default:
        return FatResult::INT_ERR;
    }
    return FatResult::OK;
}

// ============================================================================
// Chain operations
// ============================================================================

FatResult FatFs::remove_chain(FatObjId* obj, uint32_t clst, uint32_t pclst) noexcept {
    auto* fs = obj->fs;
    if (clst < 2 || clst >= fs->n_fatent)
        return FatResult::INT_ERR;

    // If previous cluster specified, terminate it
    if (pclst != 0) {
        uint32_t eoc = (fs->fs_type == FS_FAT12) ? 0xFFF : (fs->fs_type == FS_FAT16) ? 0xFFFF : 0x0FFFFFFF;
        auto res = put_fat(fs, pclst, eoc);
        if (res != FatResult::OK)
            return res;
    }

    // Walk and free the chain
    uint32_t limit = fs->n_fatent;
    while (clst >= 2 && clst < fs->n_fatent && limit > 0) {
        uint32_t nxt = get_fat(obj, clst);
        if (nxt == 1)
            return FatResult::INT_ERR;
        auto res = put_fat(fs, clst, 0);
        if (res != FatResult::OK)
            return res;
        if (fs->free_clst < fs->n_fatent - 2) {
            fs->free_clst++;
            fs->fsi_flag |= 1;
        }
        clst = nxt;
        limit--;
    }
    return FatResult::OK;
}

uint32_t FatFs::create_chain(FatObjId* obj, uint32_t clst) noexcept {
    auto* fs = obj->fs;
    uint32_t ncl;

    if (clst == 0) {
        // Start a new chain — scan from last_clst hint
        ncl = fs->last_clst;
        if (ncl == 0 || ncl >= fs->n_fatent)
            ncl = 1;
    } else {
        // Extend existing chain
        uint32_t cs = get_fat(obj, clst);
        if (cs < 2)
            return 1; // error
        if (cs < fs->n_fatent)
            return cs; // already has next
        ncl = clst;    // start scan from current
    }

    // Scan for free cluster
    uint32_t scl = ncl;
    for (;;) {
        ncl++;
        if (ncl >= fs->n_fatent) {
            ncl = 2;
            if (ncl > scl)
                return 0; // no free cluster
        }
        uint32_t cs = get_fat(obj, ncl);
        if (cs == 0)
            break; // free
        if (cs == 1)
            return 1; // error
        if (ncl == scl)
            return 0; // full scan, no free
    }

    // Mark new cluster as end-of-chain
    uint32_t eoc = (fs->fs_type == FS_FAT12) ? 0xFFF : (fs->fs_type == FS_FAT16) ? 0xFFFF : 0x0FFFFFFF;
    auto res = put_fat(fs, ncl, eoc);
    if (res != FatResult::OK)
        return 1;

    // Link from previous cluster
    if (clst != 0) {
        res = put_fat(fs, clst, ncl);
        if (res != FatResult::OK)
            return 1;
    }

    fs->last_clst = ncl;
    if (fs->free_clst <= fs->n_fatent - 2) {
        fs->free_clst--;
        fs->fsi_flag |= 1;
    }

    return ncl;
}

// ============================================================================
// Directory operations (low level)
// ============================================================================

FatResult FatFs::dir_sdi(FatDir* dp, uint32_t ofs) noexcept {
    auto* fs = dp->obj.fs;
    dp->dptr = ofs;

    uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;

    if (dp->obj.sclust == 0 && fs->fs_type != FS_FAT32) {
        // FAT12/16 root directory — fixed area
        if (ofs / DIR_ENTRY_SIZE >= fs->n_rootdir)
            return FatResult::INT_ERR;
        dp->sect = fs->dirbase + ofs / SS_VAL;
        dp->clust = 0;
    } else {
        // Cluster chain directory
        uint32_t clst = dp->obj.sclust;
        if (clst == 0) {
            // FAT32 root
            clst = static_cast<uint32_t>(fs->dirbase);
        }
        // Walk to the cluster containing ofs
        uint32_t ic = ofs / csz;
        while (ic > 0) {
            clst = get_fat(&dp->obj, clst);
            if (clst < 2 || clst >= fs->n_fatent)
                return FatResult::INT_ERR;
            ic--;
        }
        dp->clust = clst;
        dp->sect = clst2sect(fs, clst) + (ofs % csz) / SS_VAL;
    }

    dp->dir = fs->win + (ofs % SS_VAL);
    return FatResult::OK;
}

FatResult FatFs::dir_next(FatDir* dp, int stretch) noexcept {
    auto* fs = dp->obj.fs;
    uint32_t ofs = dp->dptr + DIR_ENTRY_SIZE;

    if (dp->obj.sclust == 0 && fs->fs_type != FS_FAT32) {
        // FAT12/16 root directory — fixed size
        if (ofs / DIR_ENTRY_SIZE >= fs->n_rootdir)
            return FatResult::NO_FILE;
        // Advance sector when crossing boundary
        if (ofs % SS_VAL == 0) {
            dp->sect++;
        }
    } else {
        // Cluster-based directory
        uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;
        if (ofs % csz == 0) {
            // Crossed cluster boundary
            uint32_t clst = get_fat(&dp->obj, dp->clust);
            if (clst < 2)
                return FatResult::INT_ERR;
            if (clst >= fs->n_fatent) {
                if (!stretch)
                    return FatResult::NO_FILE;
                clst = create_chain(&dp->obj, dp->clust);
                if (clst == 0)
                    return FatResult::DENIED;
                if (clst == 1)
                    return FatResult::INT_ERR;
                // Clear new cluster
                auto res = dir_clear(fs, clst);
                if (res != FatResult::OK)
                    return res;
            }
            dp->clust = clst;
            dp->sect = clst2sect(fs, clst);
        } else if ((ofs % SS_VAL) == 0) {
            dp->sect++;
        }
    }

    dp->dptr = ofs;
    dp->dir = fs->win + (ofs % SS_VAL);
    return FatResult::OK;
}

FatResult FatFs::dir_alloc(FatDir* dp, uint32_t n_ent) noexcept {
    auto res = dir_sdi(dp, 0);
    if (res != FatResult::OK)
        return res;

    uint32_t n = 0;
    for (;;) {
        res = move_window(dp->obj.fs, dp->sect);
        if (res != FatResult::OK)
            return res;

        if (dp->dir[DIR_NAME] == 0x00 || dp->dir[DIR_NAME] == DDEM) {
            if (++n == n_ent)
                break;
        } else {
            n = 0;
        }

        res = dir_next(dp, 1);
        if (res != FatResult::OK) {
            if (res == FatResult::NO_FILE)
                res = FatResult::DENIED;
            return res;
        }
    }

    // dp now points to the last entry of the block; rewind to first
    if (n_ent > 1) {
        dp->dptr -= (n_ent - 1) * DIR_ENTRY_SIZE;
        res = dir_sdi(dp, dp->dptr);
    }
    return res;
}

FatResult FatFs::dir_read(FatDir* dp, int vol) noexcept {
    auto* fs = dp->obj.fs;

    while (dp->sect) {
        auto res = move_window(fs, dp->sect);
        if (res != FatResult::OK)
            return res;

        uint8_t b = dp->dir[DIR_NAME];
        if (b == 0x00)
            return FatResult::NO_FILE; // end of directory

        if (b != DDEM) {
            uint8_t attr = dp->dir[DIR_ATTR];
            // If LFN entry, handle it
            if constexpr (config::USE_LFN != 0) {
                if (attr == AM_LFN) {
                    // LFN entry
                    if (b & LLEF) {
                        // First LFN entry in sequence (last in directory order)
                        dp->blk_ofs = dp->dptr;
                    }
                    // Pick LFN characters
                    pick_lfn(fs->lfnbuf, dp->dir);
                    res = dir_next(dp, 0);
                    if (res != FatResult::OK)
                        return (res == FatResult::NO_FILE) ? FatResult::NO_FILE : res;
                    continue;
                }
            }
            // Regular entry (SFN)
            if (!vol && (attr & AM_VOL)) {
                // Volume label — skip unless looking for it
            } else {
                // Found a valid entry
                break;
            }
        }

        // Move to next entry
        auto res2 = dir_next(dp, 0);
        if (res2 != FatResult::OK)
            return (res2 == FatResult::NO_FILE) ? FatResult::NO_FILE : res2;
    }

    if (!dp->sect)
        return FatResult::NO_FILE;
    return FatResult::OK;
}

// ============================================================================
// LFN helpers
// ============================================================================

int FatFs::cmp_lfn(const uint16_t* lfnbuf, uint8_t* dir) noexcept {
    if (dir[LDIR_ATTR] != AM_LFN)
        return 0;

    uint8_t ord = dir[LDIR_ORD];
    int idx = ((ord & 0x1F) - 1) * LFN_CHARS_PER_ENTRY;

    for (int i = 0; i < LFN_CHARS_PER_ENTRY; i++) {
        uint16_t uc = ld16(&dir[LFN_OFFSETS[i]]);
        uint16_t lc = lfnbuf[idx + i];
        if (uc == 0xFFFF || lc == 0) {
            // Past end of name — if both at end, matches; if only one, no match
            if (uc != 0xFFFF && lc == 0) {
                // Check remaining LFN chars are 0xFFFF padding
                // This is fine — they should be padding
            }
            if (lc == 0 && uc == 0)
                return 1;
            if (lc == 0 && uc == 0xFFFF)
                return 1;
            if (lc != 0 || (uc != 0 && uc != 0xFFFF))
                return 0;
            return 1;
        }
        // Case-insensitive compare
        if (ff_wtoupper(uc) != ff_wtoupper(lc))
            return 0;
    }
    return 1;
}

int FatFs::pick_lfn(uint16_t* lfnbuf, uint8_t* dir) noexcept {
    if (dir[LDIR_ATTR] != AM_LFN)
        return 0;

    uint8_t ord = dir[LDIR_ORD];
    int idx = ((ord & 0x1F) - 1) * LFN_CHARS_PER_ENTRY;

    if (ord & LLEF) {
        // Last LFN entry — determine total length and null-terminate
        int total = idx + LFN_CHARS_PER_ENTRY;
        if (total <= config::MAX_LFN) {
            lfnbuf[total] = 0; // will be shortened below
        }
    }

    for (int i = 0; i < LFN_CHARS_PER_ENTRY; i++) {
        uint16_t uc = ld16(&dir[LFN_OFFSETS[i]]);
        if (idx + i <= config::MAX_LFN) {
            if (uc == 0 || uc == 0xFFFF) {
                lfnbuf[idx + i] = 0;
                if (uc == 0)
                    break;
            } else {
                lfnbuf[idx + i] = uc;
            }
        }
    }
    return 1;
}

void FatFs::put_lfn(const uint16_t* lfn, uint8_t* dir, uint8_t ord, uint8_t sum) noexcept {
    std::memset(dir, 0, DIR_ENTRY_SIZE);
    dir[LDIR_ORD] = ord;
    dir[LDIR_ATTR] = AM_LFN;
    dir[LDIR_TYPE] = 0;
    dir[LDIR_CHKSUM] = sum;
    st16(&dir[LDIR_FST_CLUS_LO], 0);

    int idx = ((ord & 0x1F) - 1) * LFN_CHARS_PER_ENTRY;
    bool past_end = false;

    for (int i = 0; i < LFN_CHARS_PER_ENTRY; i++) {
        uint16_t wc;
        if (past_end) {
            wc = 0xFFFF;
        } else {
            wc = lfn[idx + i];
            if (wc == 0) {
                past_end = true;
                wc = 0; // null terminator goes once
            }
        }
        st16(&dir[LFN_OFFSETS[i]], wc);
        if (!past_end && wc == 0)
            past_end = true;
    }
}

uint8_t FatFs::sum_sfn(const uint8_t* dir) noexcept {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = static_cast<uint8_t>(((sum >> 1) + ((sum & 1) << 7)) + dir[i]);
    }
    return sum;
}

void FatFs::gen_numname(uint8_t* dst, const uint8_t* src, const uint16_t* /*lfn*/, uint32_t seq) noexcept {
    std::memcpy(dst, src, 11);

    // Generate ~N suffix
    char num[8];
    int nlen = 0;
    num[nlen++] = '~';

    // Convert seq to decimal
    char digits[8];
    int dlen = 0;
    uint32_t s = seq;
    do {
        digits[dlen++] = static_cast<char>('0' + s % 10);
        s /= 10;
    } while (s > 0);

    // Reverse digits
    for (int i = dlen - 1; i >= 0; i--) {
        num[nlen++] = digits[i];
    }

    // Find insert position — as far right as possible in 8-char body
    int pos = 8 - nlen;
    if (pos < 1)
        pos = 1;

    for (int i = 0; i < nlen && pos + i < 8; i++) {
        dst[pos + i] = static_cast<uint8_t>(num[i]);
    }
}

// ============================================================================
// create_name — parse path component into SFN + LFN
// ============================================================================

FatResult FatFs::create_name(FatDir* dp, const char** path) noexcept {
    const char* p = *path;
    uint8_t* sfn = dp->fn;
    uint16_t* lfn_buf = dp->obj.fs->lfnbuf;
    int lfn_idx = 0;
    uint8_t ns_flag = 0;

    // Skip leading separators
    while (*p == '/' || *p == '\\')
        p++;

    // Collect characters until separator or end
    while (*p && *p != '/' && *p != '\\') {
        if (lfn_idx < config::MAX_LFN) {
            lfn_buf[lfn_idx++] = static_cast<uint8_t>(*p);
        }
        p++;
    }
    lfn_buf[lfn_idx] = 0;
    *path = p;

    if (lfn_idx == 0)
        return FatResult::INVALID_NAME;

    // Initialize SFN to spaces
    std::memset(sfn, ' ', 11);
    sfn[NSFLAG] = 0;

    // Check for dot entries
    if (lfn_idx == 1 && lfn_buf[0] == '.') {
        sfn[0] = '.';
        sfn[NSFLAG] = NS_DOT;
        return FatResult::OK;
    }
    if (lfn_idx == 2 && lfn_buf[0] == '.' && lfn_buf[1] == '.') {
        sfn[0] = '.';
        sfn[1] = '.';
        sfn[NSFLAG] = NS_DOT;
        return FatResult::OK;
    }

    // Generate SFN from LFN
    // Find the last dot for extension split
    int last_dot = -1;
    for (int i = lfn_idx - 1; i >= 0; i--) {
        if (lfn_buf[i] == '.') {
            last_dot = i;
            break;
        }
    }

    bool need_lfn = false;
    int si = 0; // SFN body index
    int body_end = (last_dot >= 0) ? last_dot : lfn_idx;

    // Fill body (up to 8 chars)
    for (int i = 0; i < body_end && si < 8; i++) {
        uint16_t wc = lfn_buf[i];
        if (wc == ' ' || wc == '.') {
            need_lfn = true;
            continue;
        }
        // Convert to OEM upper-case
        uint16_t oem;
        if (wc < 0x80) {
            char c = static_cast<char>(wc);
            // Check for invalid SFN characters
            if (c == '+' || c == ',' || c == ';' || c == '=' || c == '[' || c == ']') {
                need_lfn = true;
                c = '_';
            }
            if (c >= 'a' && c <= 'z') {
                need_lfn = true;
                c = static_cast<char>(c - 'a' + 'A');
            }
            oem = static_cast<uint8_t>(c);
        } else {
            oem = EXCVT[wc - 0x80];
            if (oem != wc)
                need_lfn = true;
        }
        sfn[si++] = static_cast<uint8_t>(oem);
    }
    if (body_end > 8)
        need_lfn = true;
    if (si == 0) {
        sfn[NSFLAG] = NS_NONAME;
        return FatResult::INVALID_NAME;
    }

    // Fill extension (up to 3 chars)
    if (last_dot >= 0) {
        int ei = 0;
        for (int i = last_dot + 1; i < lfn_idx && ei < 3; i++) {
            uint16_t wc = lfn_buf[i];
            if (wc == ' ' || wc == '.') {
                need_lfn = true;
                continue;
            }
            uint16_t oem;
            if (wc < 0x80) {
                char c = static_cast<char>(wc);
                if (c >= 'a' && c <= 'z') {
                    need_lfn = true;
                    c = static_cast<char>(c - 'a' + 'A');
                }
                oem = static_cast<uint8_t>(c);
            } else {
                oem = EXCVT[wc - 0x80];
                if (oem != wc)
                    need_lfn = true;
            }
            sfn[8 + ei++] = static_cast<uint8_t>(oem);
        }
        if (lfn_idx - (last_dot + 1) > 3)
            need_lfn = true;
    }

    // Check case mismatches — if any lower case chars exist, we need LFN
    // Also check if name was truncated
    if (body_end > 8 || (last_dot >= 0 && lfn_idx - (last_dot + 1) > 3)) {
        need_lfn = true;
        ns_flag |= NS_LOSS;
    }

    if constexpr (config::USE_LFN != 0) {
        if (need_lfn) {
            ns_flag |= NS_LFN;
            ns_flag |= NS_LOSS; // need numeric tail
        }
    }

    sfn[NSFLAG] = ns_flag;
    return FatResult::OK;
}

// ============================================================================
// dir_find — find an entry matching dp->fn / lfn_buf
// ============================================================================

FatResult FatFs::dir_find(FatDir* dp) noexcept {
    auto* fs = dp->obj.fs;
    auto res = dir_sdi(dp, 0);
    if (res != FatResult::OK)
        return res;

    uint8_t ord = 0xFF;
    uint8_t sum = 0;

    do {
        res = move_window(fs, dp->sect);
        if (res != FatResult::OK)
            return res;

        uint8_t b = dp->dir[DIR_NAME];
        if (b == 0x00)
            return FatResult::NO_FILE;

        if (b == DDEM) {
            ord = 0xFF;
        } else {
            uint8_t attr = dp->dir[DIR_ATTR];
            if constexpr (config::USE_LFN != 0) {
                if (attr == AM_LFN) {
                    // LFN entry
                    if (dp->fn[NSFLAG] & NS_NOLFN) {
                        // SFN-only search, skip LFN
                    } else {
                        if (b & LLEF) {
                            sum = dp->dir[LDIR_CHKSUM];
                            ord = b;
                            dp->blk_ofs = dp->dptr;
                        }
                        // Compare LFN
                        if (ord != 0xFF && !cmp_lfn(fs->lfnbuf, dp->dir)) {
                            ord = 0xFF;
                        }
                    }
                } else {
                    // SFN entry — check if LFN matched
                    if (ord != 0xFF && sum == sum_sfn(dp->dir)) {
                        // LFN matched
                        break;
                    }
                    ord = 0xFF;
                    // Also try SFN match
                    if (!(attr & AM_VOL) || (attr & AM_DIR)) {
                        // Compare SFN
                        bool match = true;
                        for (int i = 0; i < 11; i++) {
                            if (dp->fn[i] != dp->dir[DIR_NAME + i]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            dp->blk_ofs = dp->dptr; // No LFN for this entry
                            break;
                        }
                    }
                }
            } else {
                // No LFN support — SFN compare only
                if (!(attr & AM_VOL) || (attr & AM_DIR)) {
                    bool match = true;
                    for (int i = 0; i < 11; i++) {
                        if (dp->fn[i] != dp->dir[DIR_NAME + i]) {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                        break;
                }
            }
        }

        res = dir_next(dp, 0);
    } while (res == FatResult::OK);

    return res;
}

// ============================================================================
// dir_register — register a new entry at the found position
// ============================================================================

FatResult FatFs::dir_register(FatDir* dp) noexcept {
    auto* fs = dp->obj.fs;

    uint32_t n_ent = 1; // SFN entry

    if constexpr (config::USE_LFN != 0) {
        if (dp->fn[NSFLAG] & NS_LFN) {
            // Count LFN entries needed
            int lfn_len = 0;
            while (fs->lfnbuf[lfn_len] != 0)
                lfn_len++;
            n_ent = static_cast<uint32_t>((lfn_len + LFN_CHARS_PER_ENTRY - 1) / LFN_CHARS_PER_ENTRY) + 1;
        }
    }

    auto res = dir_alloc(dp, n_ent);
    if (res != FatResult::OK)
        return res;

    if constexpr (config::USE_LFN != 0) {
        if (n_ent > 1) {
            // Need to generate numeric name if there's a loss
            if (dp->fn[NSFLAG] & NS_LOSS) {
                // Try ~1, ~2, etc.
                uint8_t sfn_try[12];
                for (uint32_t seq = 1; seq < 100; seq++) {
                    gen_numname(sfn_try, dp->fn, fs->lfnbuf, seq);
                    // Check if this name exists
                    FatDir check{};
                    check.obj = dp->obj;
                    std::memcpy(check.fn, sfn_try, 12);
                    check.fn[NSFLAG] = NS_NOLFN; // SFN-only search
                    auto cr = dir_sdi(&check, 0);
                    if (cr == FatResult::OK) {
                        cr = dir_find(&check);
                        if (cr == FatResult::NO_FILE) {
                            // This name is available
                            std::memcpy(dp->fn, sfn_try, 11);
                            break;
                        }
                    }
                }
            }

            // Write LFN entries (reverse order: last entry first in directory)
            uint8_t sum = sum_sfn(dp->fn);
            uint32_t ne = n_ent - 1; // number of LFN entries

            res = dir_sdi(dp, dp->dptr);
            if (res != FatResult::OK)
                return res;

            for (uint32_t i = ne; i >= 1; i--) {
                res = move_window(fs, dp->sect);
                if (res != FatResult::OK)
                    return res;

                uint8_t ord = static_cast<uint8_t>(i);
                if (i == ne)
                    ord |= LLEF;

                put_lfn(fs->lfnbuf, dp->dir, ord, sum);
                fs->wflag = 1;

                if (i > 1) {
                    res = dir_next(dp, 1);
                    if (res != FatResult::OK)
                        return res;
                }
            }
            // Advance to SFN slot
            res = dir_next(dp, 1);
            if (res != FatResult::OK)
                return res;
        }
    }

    // Write SFN entry
    res = move_window(fs, dp->sect);
    if (res != FatResult::OK)
        return res;

    std::memset(dp->dir, 0, DIR_ENTRY_SIZE);
    std::memcpy(dp->dir + DIR_NAME, dp->fn, 11);
    fs->wflag = 1;

    return FatResult::OK;
}

// ============================================================================
// dir_remove
// ============================================================================

FatResult FatFs::dir_remove(FatDir* dp) noexcept {
    auto* fs = dp->obj.fs;

    if constexpr (config::USE_LFN != 0) {
        // Remove LFN entries starting from blk_ofs (only if LFN entries exist)
        uint32_t last = dp->dptr;
        if (dp->blk_ofs < last) {
            // LFN entries precede the SFN entry
            auto res = dir_sdi(dp, dp->blk_ofs);
            if (res != FatResult::OK)
                return res;

            while (dp->dptr <= last) {
                res = move_window(fs, dp->sect);
                if (res != FatResult::OK)
                    return res;
                dp->dir[DIR_NAME] = DDEM;
                fs->wflag = 1;
                if (dp->dptr == last)
                    break;
                res = dir_next(dp, 0);
                if (res != FatResult::OK)
                    return res;
            }
            return FatResult::OK;
        }
    }

    // Just mark SFN entry as deleted
    auto res = move_window(fs, dp->sect);
    if (res != FatResult::OK)
        return res;
    dp->dir[DIR_NAME] = DDEM;
    fs->wflag = 1;
    return FatResult::OK;
}

// ============================================================================
// get_fileinfo
// ============================================================================

void FatFs::get_fileinfo(FatDir* dp, FatFileInfo* fno) noexcept {
    auto* fs = dp->obj.fs;

    fno->fname[0] = '\0';
    if (!dp->sect)
        return;

    // Copy SFN to altname
    {
        int di = 0;
        // Body
        for (int i = 0; i < 8 && dp->dir[DIR_NAME + i] != ' '; i++) {
            uint8_t c = dp->dir[DIR_NAME + i];
            if (c == RDDEM)
                c = DDEM;
            // Apply NTRes case info
            if ((dp->dir[DIR_NTRES] & 0x08) && c >= 'A' && c <= 'Z')
                c += 0x20;
            fno->altname[di++] = static_cast<char>(c);
        }
        // Dot + extension
        if (dp->dir[DIR_NAME + 8] != ' ') {
            fno->altname[di++] = '.';
            for (int i = 8; i < 11 && dp->dir[DIR_NAME + i] != ' '; i++) {
                uint8_t c = dp->dir[DIR_NAME + i];
                if ((dp->dir[DIR_NTRES] & 0x10) && c >= 'A' && c <= 'Z')
                    c += 0x20;
                fno->altname[di++] = static_cast<char>(c);
            }
        }
        fno->altname[di] = '\0';
    }

    // Try to use LFN for fname
    if constexpr (config::USE_LFN != 0) {
        // Check if we have a valid LFN in the buffer
        if (fs->lfnbuf[0] != 0) {
            // Convert UTF-16 LFN to ANSI
            int di = 0;
            for (int i = 0; fs->lfnbuf[i] != 0 && di < LFN_BUF; i++) {
                uint16_t wc = fs->lfnbuf[i];
                if (wc < 0x80) {
                    fno->fname[di++] = static_cast<char>(wc);
                } else {
                    uint16_t oem = ff_uni2oem(wc, config::CODE_PAGE);
                    if (oem) {
                        fno->fname[di++] = static_cast<char>(oem);
                    } else {
                        fno->fname[di++] = '?';
                    }
                }
            }
            fno->fname[di] = '\0';
        } else {
            // No LFN — copy altname to fname
            std::strcpy(fno->fname, fno->altname);
        }
    } else {
        std::strcpy(fno->fname, fno->altname);
    }

    fno->fattrib = dp->dir[DIR_ATTR];
    fno->fsize = ld32(&dp->dir[DIR_FILE_SIZE]);
    fno->fdate = ld16(&dp->dir[DIR_WRT_DATE]);
    fno->ftime = ld16(&dp->dir[DIR_WRT_TIME]);
}

// ============================================================================
// Cluster load/store from directory entry
// ============================================================================

uint32_t FatFs::ld_clust(FatFsVolume* fs, const uint8_t* dir) noexcept {
    uint32_t cl = ld16(&dir[DIR_FST_CLUS_LO]);
    if (fs->fs_type == FS_FAT32) {
        cl |= static_cast<uint32_t>(ld16(&dir[DIR_FST_CLUS_HI])) << 16;
    }
    return cl;
}

void FatFs::st_clust(FatFsVolume* fs, uint8_t* dir, uint32_t cl) noexcept {
    st16(&dir[DIR_FST_CLUS_LO], static_cast<uint16_t>(cl));
    if (fs->fs_type == FS_FAT32) {
        st16(&dir[DIR_FST_CLUS_HI], static_cast<uint16_t>(cl >> 16));
    }
}

// ============================================================================
// follow_path — walk path components
// ============================================================================

FatResult FatFs::follow_path(FatDir* dp, const char* path) noexcept {
    auto* fs = dp->obj.fs;

    // Skip leading separators
    while (*path == '/' || *path == '\\')
        path++;

    // Start from root directory
    dp->obj.sclust = 0;
    if (fs->fs_type == FS_FAT32) {
        dp->obj.sclust = static_cast<uint32_t>(fs->dirbase);
    }

    if (*path == '\0') {
        // Root directory itself
        auto res = dir_sdi(dp, 0);
        dp->fn[NSFLAG] = NS_NONAME;
        return res;
    }

    for (;;) {
        auto res = create_name(dp, &path);
        if (res != FatResult::OK)
            return res;

        res = dir_find(dp);
        if (res != FatResult::OK)
            return res;

        // Skip separators
        while (*path == '/' || *path == '\\')
            path++;

        if (*path == '\0') {
            // Reached end of path — found
            return FatResult::OK;
        }

        // Must be a directory to descend
        if (!(dp->dir[DIR_ATTR] & AM_DIR))
            return FatResult::NO_PATH;

        // Descend into subdirectory
        uint32_t clst = ld_clust(fs, dp->dir);
        dp->obj.sclust = clst;
    }
}

// ============================================================================
// Volume management helpers
// ============================================================================

uint32_t FatFs::check_fs(FatFsVolume* fs, LBA_t sect) noexcept {
    // Read boot sector
    if (disk_read(fs->pdrv, fs->win, sect, 1) != DiskResult::OK)
        return 4;
    fs->winsect = sect;

    // Check boot signature
    if (fs->win[510] != 0x55 || fs->win[511] != 0xAA)
        return 3;

    // Check for FAT filesystem type strings
    if (std::memcmp(&fs->win[BS_FILESYSTYPE], "FAT", 3) == 0)
        return 0;
    if (std::memcmp(&fs->win[BS_FILESYSTYPE32], "FAT32", 5) == 0)
        return 0;

    return 2;
}

uint32_t FatFs::find_volume(FatFsVolume* fs, uint32_t part) noexcept {
    // Try sector 0 first as boot sector
    uint32_t fmt = check_fs(fs, 0);
    if (fmt <= 1)
        return 0; // Found at sector 0

    // Try as MBR — look at partition table
    if (fmt < 4) {
        // Check partition entry
        uint8_t* pt = &fs->win[446 + part * 16];
        if (pt[4] != 0) {
            LBA_t bsect = ld32(&pt[8]);
            fmt = check_fs(fs, bsect);
            if (fmt <= 1)
                return bsect;
        }
    }
    return 0xFFFFFFFF;
}

FatResult FatFs::mount_volume(const char** path, FatFsVolume** rfs, uint8_t mode) noexcept {
    // Parse volume number from path
    const char* p = *path;
    int vol = 0;
    if (p[0] >= '0' && p[0] <= '9' && p[1] == ':') {
        vol = p[0] - '0';
        p += 2;
    } else if (p[0] == ':') {
        p += 1;
    }
    *path = p;

    if (vol >= config::VOLUMES)
        return FatResult::INVALID_DRIVE;

    auto* fs = fat_fs[vol];
    if (!fs)
        return FatResult::NOT_ENABLED;

    *rfs = fs;

    if (fs->fs_type != 0) {
        // Already mounted — check disk status
        uint8_t stat = disk_status(fs->pdrv);
        if (!(stat & STA_NOINIT)) {
            if (mode && (stat & STA_PROTECT))
                return FatResult::WRITE_PROTECTED;
            return FatResult::OK;
        }
    }

    // Need to mount
    fs->fs_type = 0;
    uint8_t stat = disk_initialize(fs->pdrv);
    if (stat & STA_NOINIT)
        return FatResult::NOT_READY;
    if (mode && (stat & STA_PROTECT))
        return FatResult::WRITE_PROTECTED;

    // Find boot sector
    LBA_t bsect = find_volume(fs, 0);
    if (bsect == 0xFFFFFFFF)
        return FatResult::NO_FILESYSTEM;

    // Read BPB
    if (move_window(fs, bsect) != FatResult::OK)
        return FatResult::DISK_ERR;

    uint16_t bps = ld16(&fs->win[BPB_BYTS_PER_SEC]);
    if (bps != SS_VAL)
        return FatResult::NO_FILESYSTEM;

    uint8_t spc = fs->win[BPB_SEC_PER_CLUS];
    if (spc == 0 || (spc & (spc - 1)) != 0)
        return FatResult::NO_FILESYSTEM;

    uint8_t nfats = fs->win[BPB_NUM_FATS];
    if (nfats != 1 && nfats != 2)
        return FatResult::NO_FILESYSTEM;

    uint16_t rsvd = ld16(&fs->win[BPB_RSVD_SEC_CNT]);
    if (rsvd == 0)
        return FatResult::NO_FILESYSTEM;

    uint16_t n_rootdir = ld16(&fs->win[BPB_ROOT_ENT_CNT]);
    uint32_t root_dir_sectors = ((n_rootdir * 32) + (SS_VAL - 1)) / SS_VAL;

    uint32_t fat_sz = ld16(&fs->win[BPB_FAT_SZ16]);
    if (fat_sz == 0)
        fat_sz = ld32(&fs->win[BPB_FAT_SZ32]);
    if (fat_sz == 0)
        return FatResult::NO_FILESYSTEM;

    uint32_t tot_sec = ld16(&fs->win[BPB_TOT_SEC16]);
    if (tot_sec == 0)
        tot_sec = ld32(&fs->win[BPB_TOT_SEC32]);
    if (tot_sec == 0)
        return FatResult::NO_FILESYSTEM;

    uint32_t data_start = rsvd + nfats * fat_sz + root_dir_sectors;
    uint32_t data_sec = tot_sec - data_start;
    uint32_t n_clust = data_sec / spc;

    // Determine FAT type
    uint8_t fs_type;
    if (n_clust < 4085) {
        fs_type = FS_FAT12;
    } else if (n_clust < 65525) {
        fs_type = FS_FAT16;
    } else {
        fs_type = FS_FAT32;
    }

    // Fill volume info
    fs->fs_type = fs_type;
    fs->pdrv = static_cast<uint8_t>(vol);
    fs->ldrv = static_cast<uint8_t>(vol);
    fs->n_fats = nfats;
    fs->csize = spc;
    fs->n_rootdir = n_rootdir;
    fs->n_fatent = n_clust + 2;
    fs->fsize = fat_sz;
    fs->volbase = bsect;
    fs->fatbase = bsect + rsvd;
    fs->database = bsect + data_start;

    if (fs_type == FS_FAT32) {
        fs->dirbase = ld32(&fs->win[BPB_ROOT_CLUS32]);
    } else {
        fs->dirbase = fs->fatbase + nfats * fat_sz;
    }

    // FSInfo (FAT32)
    fs->last_clst = 0xFFFFFFFF;
    fs->free_clst = 0xFFFFFFFF;
    fs->fsi_flag = 0x80; // disabled by default

    if (fs_type == FS_FAT32) {
        uint16_t fsi_sect = ld16(&fs->win[BPB_FSINFO32]);
        if (fsi_sect == 1 && config::FS_NOFSINFO != 1) {
            if (move_window(fs, bsect + fsi_sect) == FatResult::OK) {
                if (ld32(&fs->win[FSI_LEAD_SIG]) == 0x41615252 && ld32(&fs->win[FSI_STRUC_SIG]) == 0x61417272 &&
                    ld32(&fs->win[FSI_TRAIL_SIG]) == 0xAA550000) {
                    fs->free_clst = ld32(&fs->win[FSI_FREE_COUNT]);
                    fs->last_clst = ld32(&fs->win[FSI_NXT_FREE]);
                    fs->fsi_flag = 0;
                }
            }
        }
    }

    // LFN buffer
    fs->lfnbuf = lfn_buf;

    // Bump mount ID
    fs->id++;
    fs->wflag = 0;
    fs->winsect = 0xFFFFFFFF;

    return FatResult::OK;
}

FatResult FatFs::validate(FatObjId* obj, FatFsVolume** rfs) noexcept {
    if (!obj || !obj->fs)
        return FatResult::INVALID_OBJECT;
    auto* fs = obj->fs;
    if (fs->fs_type == 0)
        return FatResult::INVALID_OBJECT;
    if (obj->id != fs->id)
        return FatResult::INVALID_OBJECT;

    uint8_t stat = disk_status(fs->pdrv);
    if (stat & STA_NOINIT)
        return FatResult::INVALID_OBJECT;

    *rfs = fs;
    return FatResult::OK;
}

FatResult FatFs::sync_fs(FatFsVolume* fs) noexcept {
    auto res = sync_window(fs);
    if (res != FatResult::OK)
        return res;

    // Update FSInfo (FAT32)
    if (fs->fs_type == FS_FAT32 && (fs->fsi_flag & 1)) {
        if (move_window(fs, fs->volbase + 1) == FatResult::OK) {
            if (ld32(&fs->win[FSI_LEAD_SIG]) == 0x41615252 && ld32(&fs->win[FSI_STRUC_SIG]) == 0x61417272) {
                st32(&fs->win[FSI_FREE_COUNT], fs->free_clst);
                st32(&fs->win[FSI_NXT_FREE], fs->last_clst);
                fs->wflag = 1;
                sync_window(fs);
            }
        }
        fs->fsi_flag &= ~1;
    }

    if (disk_ioctl(fs->pdrv, CTRL_SYNC, nullptr) != DiskResult::OK)
        return FatResult::DISK_ERR;
    return FatResult::OK;
}

FatResult FatFs::dir_clear(FatFsVolume* fs, uint32_t clst) noexcept {
    LBA_t sect = clst2sect(fs, clst);
    if (sect == 0)
        return FatResult::INT_ERR;

    // Clear all sectors in this cluster
    uint8_t zero[SS_VAL];
    std::memset(zero, 0, SS_VAL);

    for (uint32_t i = 0; i < fs->csize; i++) {
        if (disk_write(fs->pdrv, zero, sect + i, 1) != DiskResult::OK) {
            return FatResult::DISK_ERR;
        }
    }

    // Invalidate window if it was in the cleared area
    if (fs->winsect >= sect && fs->winsect < sect + fs->csize) {
        fs->winsect = 0xFFFFFFFF;
    }

    return FatResult::OK;
}

// ============================================================================
// Public API: Mount / Unmount
// ============================================================================

FatResult FatFs::mount(FatFsVolume* fs, const char* path, uint8_t opt) noexcept {
    // Parse volume number
    int vol = 0;
    const char* p = path;
    if (p[0] >= '0' && p[0] <= '9' && p[1] == ':') {
        vol = p[0] - '0';
    }

    if (vol >= config::VOLUMES)
        return FatResult::INVALID_DRIVE;

    // Reset volume
    auto* old_fs = fat_fs[vol];
    if (old_fs)
        old_fs->fs_type = 0;

    // Register new volume
    fs->fs_type = 0;
    fs->pdrv = static_cast<uint8_t>(vol);
    fat_fs[vol] = fs;

    if (opt) {
        // Force mount now
        const char* pp = path;
        FatFsVolume* rfs;
        return mount_volume(&pp, &rfs, 0);
    }

    return FatResult::OK;
}

FatResult FatFs::unmount(const char* path) noexcept {
    int vol = 0;
    if (path[0] >= '0' && path[0] <= '9' && path[1] == ':') {
        vol = path[0] - '0';
    }
    if (vol >= config::VOLUMES)
        return FatResult::INVALID_DRIVE;

    auto* fs = fat_fs[vol];
    if (fs) {
        fs->fs_type = 0;
    }
    fat_fs[vol] = nullptr;
    return FatResult::OK;
}

// ============================================================================
// Public API: File operations
// ============================================================================

FatResult FatFs::open(FatFile* fp, const char* path, uint8_t mode) noexcept {
    if (!fp)
        return FatResult::INVALID_OBJECT;

    fp->obj.fs = nullptr;
    const char* p = path;
    FatFsVolume* fs;
    auto res = mount_volume(&p, &fs, (mode & FA_WRITE) ? 1 : 0);
    if (res != FatResult::OK)
        return res;

    FatDir dj{};
    dj.obj.fs = fs;

    res = follow_path(&dj, p);

    if (res == FatResult::OK) {
        // Found existing entry
        if (dj.fn[NSFLAG] & NS_NONAME)
            return FatResult::INVALID_NAME;
        if (dj.dir[DIR_ATTR] & AM_DIR)
            return FatResult::NO_FILE; // it's a directory

        if (mode & FA_CREATE_NEW)
            return FatResult::EXIST;

        if ((mode & FA_WRITE) && (dj.dir[DIR_ATTR] & AM_RDO))
            return FatResult::DENIED;
    } else if (res == FatResult::NO_FILE) {
        // Not found
        if (!(mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS)))
            return FatResult::NO_FILE;

        // Create new entry
        res = dir_register(&dj);
        if (res != FatResult::OK)
            return res;

        mode |= FA_MODIFIED;
    } else {
        return res;
    }

    // Handle CREATE_ALWAYS on existing file — truncate
    if ((mode & FA_CREATE_ALWAYS) && res == FatResult::OK) {
        // Truncate existing file
        uint32_t cl = ld_clust(fs, dj.dir);
        if (cl != 0) {
            // Free the chain
            FatObjId tmp_obj{};
            tmp_obj.fs = fs;
            auto rres = remove_chain(&tmp_obj, cl, 0);
            if (rres != FatResult::OK)
                return rres;
        }
        st_clust(fs, dj.dir, 0);
        st32(&dj.dir[DIR_FILE_SIZE], 0);
        // Update timestamp
        uint32_t tm = get_fattime();
        st16(&dj.dir[DIR_WRT_TIME], static_cast<uint16_t>(tm));
        st16(&dj.dir[DIR_WRT_DATE], static_cast<uint16_t>(tm >> 16));
        dj.dir[DIR_ATTR] &= ~AM_ARC;
        dj.dir[DIR_ATTR] |= AM_ARC;
        fs->wflag = 1;
        mode |= FA_MODIFIED;
    }

    // Fill file object
    fp->obj.fs = fs;
    fp->obj.id = fs->id;
    fp->obj.attr = dj.dir[DIR_ATTR];
    fp->obj.sclust = ld_clust(fs, dj.dir);
    fp->obj.objsize = ld32(&dj.dir[DIR_FILE_SIZE]);
    fp->flag = mode & (FA_READ | FA_WRITE);
    fp->err = 0;
    fp->fptr = 0;
    fp->clust = 0;
    fp->sect = 0;
    fp->dir_sect = dj.sect;
    fp->dir_ptr = dj.dir;

    if (mode & FA_MODIFIED) {
        fp->flag |= FA_MODIFIED;
    }

    // Handle OPEN_APPEND — seek to end
    if (mode & FA_SEEKEND) {
        fp->fptr = fp->obj.objsize;
        // Need to walk chain to find current cluster
        if (fp->fptr > 0) {
            uint32_t cl = fp->obj.sclust;
            FSIZE_t ofs = fp->fptr;
            uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;
            uint32_t limit = fs->n_fatent;
            while (ofs > csz && cl >= 2 && cl < fs->n_fatent && limit > 0) {
                cl = get_fat(&fp->obj, cl);
                ofs -= csz;
                limit--;
            }
            fp->clust = cl;
            fp->sect = clst2sect(fs, cl) + (fp->fptr % csz) / SS_VAL;
        }
    }

    return FatResult::OK;
}

FatResult FatFs::close(FatFile* fp) noexcept {
    auto res = sync(fp);
    if (res == FatResult::OK) {
        fp->obj.fs = nullptr;
    }
    return res;
}

FatResult FatFs::read(FatFile* fp, void* buff, uint32_t btr, uint32_t* br) noexcept {
    *br = 0;

    FatFsVolume* fs;
    auto res = validate(&fp->obj, &fs);
    if (res != FatResult::OK)
        return res;
    if (fp->err)
        return FatResult::INT_ERR;
    if (!(fp->flag & FA_READ))
        return FatResult::DENIED;

    FSIZE_t remain = fp->obj.objsize - fp->fptr;
    if (btr > remain)
        btr = static_cast<uint32_t>(remain);

    auto* rbuff = static_cast<uint8_t*>(buff);
    uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;

    while (btr > 0) {
        if (fp->fptr % SS_VAL == 0) {
            // At sector boundary — might need new cluster
            if (fp->fptr % csz == 0) {
                // Need next cluster
                uint32_t clst;
                if (fp->fptr == 0) {
                    clst = fp->obj.sclust;
                } else {
                    clst = get_fat(&fp->obj, fp->clust);
                    if (clst < 2 || clst >= fs->n_fatent) {
                        fp->err = 1;
                        return FatResult::INT_ERR;
                    }
                }
                fp->clust = clst;
            }
            fp->sect = clst2sect(fs, fp->clust) + ((fp->fptr % csz) / SS_VAL);
        }

        uint32_t cc = SS_VAL - static_cast<uint32_t>(fp->fptr % SS_VAL); // bytes remaining in current sector
        if (cc > btr)
            cc = btr;

        // Optimization: direct read for full sectors
        if (cc == SS_VAL && (fp->fptr % SS_VAL) == 0) {
            // Check how many consecutive sectors we can read
            uint32_t sect_count = 1;
            uint32_t remaining_in_cluster = csz - (fp->fptr % csz);
            uint32_t max_sects = remaining_in_cluster / SS_VAL;
            while (sect_count < max_sects && (sect_count + 1) * SS_VAL <= btr) {
                sect_count++;
            }
            if (disk_read(fs->pdrv, rbuff, fp->sect, sect_count) != DiskResult::OK) {
                fp->err = 1;
                return FatResult::DISK_ERR;
            }
            uint32_t bytes = sect_count * SS_VAL;
            rbuff += bytes;
            fp->fptr += bytes;
            fp->sect += sect_count - 1; // will be incremented in next iteration
            btr -= bytes;
            *br += bytes;
            if (sect_count > 1) {
                fp->sect++; // advance past the block we read
            }
            continue;
        }

        // Partial sector — use file buffer
        // Flush dirty buffer before reading new sector
        if ((fp->flag & FA_DIRTY) != 0) {
            if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                fp->err = 1;
                return FatResult::DISK_ERR;
            }
            fp->flag &= ~FA_DIRTY;
        }
        // Read sector into buf for partial reads
        if (disk_read(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
            fp->err = 1;
            return FatResult::DISK_ERR;
        }

        uint32_t sect_ofs = static_cast<uint32_t>(fp->fptr % SS_VAL);
        std::memcpy(rbuff, fp->buf + sect_ofs, cc);
        rbuff += cc;
        fp->fptr += cc;
        btr -= cc;
        *br += cc;
    }

    return FatResult::OK;
}

FatResult FatFs::write(FatFile* fp, const void* buff, uint32_t btw, uint32_t* bw) noexcept {
    *bw = 0;

    FatFsVolume* fs;
    auto res = validate(&fp->obj, &fs);
    if (res != FatResult::OK)
        return res;
    if (fp->err)
        return FatResult::INT_ERR;
    if (!(fp->flag & FA_WRITE))
        return FatResult::DENIED;

    auto* wbuff = static_cast<const uint8_t*>(buff);
    uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;

    while (btw > 0) {
        if (fp->fptr % SS_VAL == 0) {
            // At sector boundary
            if (fp->fptr % csz == 0) {
                // Need cluster
                uint32_t clst;
                if (fp->fptr == 0) {
                    clst = fp->obj.sclust;
                    if (clst == 0) {
                        // Allocate first cluster
                        clst = create_chain(&fp->obj, 0);
                        if (clst == 0)
                            return FatResult::DENIED; // disk full
                        if (clst == 1)
                            return FatResult::INT_ERR;
                        fp->obj.sclust = clst;
                    }
                } else {
                    clst = create_chain(&fp->obj, fp->clust);
                    if (clst == 0)
                        return FatResult::DENIED;
                    if (clst == 1)
                        return FatResult::INT_ERR;
                }
                fp->clust = clst;
            }
            fp->sect = clst2sect(fs, fp->clust) + ((fp->fptr % csz) / SS_VAL);
        }

        uint32_t cc = SS_VAL - static_cast<uint32_t>(fp->fptr % SS_VAL);
        if (cc > btw)
            cc = btw;

        // Optimization: direct write for full sectors
        if (cc == SS_VAL && (fp->fptr % SS_VAL) == 0) {
            uint32_t sect_count = 1;
            uint32_t remaining_in_cluster = csz - (fp->fptr % csz);
            uint32_t max_sects = remaining_in_cluster / SS_VAL;
            while (sect_count < max_sects && (sect_count + 1) * SS_VAL <= btw) {
                sect_count++;
            }
            if (disk_write(fs->pdrv, wbuff, fp->sect, sect_count) != DiskResult::OK) {
                fp->err = 1;
                return FatResult::DISK_ERR;
            }
            uint32_t bytes = sect_count * SS_VAL;
            wbuff += bytes;
            fp->fptr += bytes;
            fp->sect += sect_count - 1;
            btw -= bytes;
            *bw += bytes;
            if (sect_count > 1) {
                fp->sect++;
            }
        } else {
            // Partial sector — use file buffer
            // Read existing sector first if not writing from start
            if (fp->fptr % SS_VAL != 0 || cc < SS_VAL) {
                if (disk_read(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                    fp->err = 1;
                    return FatResult::DISK_ERR;
                }
            }
            uint32_t sect_ofs = static_cast<uint32_t>(fp->fptr % SS_VAL);
            std::memcpy(fp->buf + sect_ofs, wbuff, cc);

            if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                fp->err = 1;
                return FatResult::DISK_ERR;
            }

            wbuff += cc;
            fp->fptr += cc;
            btw -= cc;
            *bw += cc;
        }

        fp->flag |= FA_MODIFIED;
    }

    // Update file size
    if (fp->fptr > fp->obj.objsize) {
        fp->obj.objsize = fp->fptr;
    }

    return FatResult::OK;
}

FatResult FatFs::lseek(FatFile* fp, FSIZE_t ofs) noexcept {
    FatFsVolume* fs;
    auto res = validate(&fp->obj, &fs);
    if (res != FatResult::OK)
        return res;
    if (fp->err)
        return FatResult::INT_ERR;

    // Clamp to file size for read-only files
    if (!(fp->flag & FA_WRITE) && ofs > fp->obj.objsize) {
        ofs = fp->obj.objsize;
    }

    // Expand file if writing beyond end
    if ((fp->flag & FA_WRITE) && ofs > fp->obj.objsize) {
        // Need to extend — allocate clusters
        uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;
        while (fp->obj.objsize < ofs) {
            FSIZE_t cur = fp->obj.objsize;
            if (cur % csz == 0 && cur > 0) {
                // Need new cluster
                uint32_t clst = create_chain(&fp->obj, fp->clust);
                if (clst == 0 || clst == 1)
                    break;
                fp->clust = clst;
            } else if (cur == 0) {
                // Need first cluster
                if (fp->obj.sclust == 0) {
                    uint32_t clst = create_chain(&fp->obj, 0);
                    if (clst == 0 || clst == 1)
                        break;
                    fp->obj.sclust = clst;
                    fp->clust = clst;
                }
            }
            // Advance by up to remaining in cluster
            FSIZE_t advance = csz - (cur % csz);
            if (cur + advance > ofs)
                advance = ofs - cur;
            fp->obj.objsize = cur + advance;
        }
        fp->flag |= FA_MODIFIED;
    }

    // Now walk chain to position
    if (ofs > fp->obj.objsize)
        ofs = fp->obj.objsize;

    fp->fptr = 0;
    uint32_t clst = fp->obj.sclust;
    uint32_t csz = static_cast<uint32_t>(fs->csize) * SS_VAL;

    if (ofs > 0 && clst != 0) {
        // Walk to target cluster
        uint32_t limit = fs->n_fatent;
        while (fp->fptr + csz <= ofs && clst >= 2 && clst < fs->n_fatent && limit > 0) {
            clst = get_fat(&fp->obj, clst);
            fp->fptr += csz;
            limit--;
        }
        if (clst < 2 || clst >= fs->n_fatent) {
            // Chain ended before reaching target
            clst = fp->obj.sclust;
            fp->fptr = 0;
        } else {
            fp->clust = clst;
            fp->fptr = ofs;
            fp->sect = clst2sect(fs, clst) + ((ofs % csz) / SS_VAL);
            return FatResult::OK;
        }
    }

    fp->fptr = ofs;
    fp->clust = clst;
    if (clst != 0) {
        fp->sect = clst2sect(fs, clst) + ((ofs % csz) / SS_VAL);
    } else {
        fp->sect = 0;
    }

    return FatResult::OK;
}

FatResult FatFs::truncate(FatFile* fp) noexcept {
    FatFsVolume* fs;
    auto res = validate(&fp->obj, &fs);
    if (res != FatResult::OK)
        return res;
    if (fp->err)
        return FatResult::INT_ERR;
    if (!(fp->flag & FA_WRITE))
        return FatResult::DENIED;

    if (fp->fptr < fp->obj.objsize) {
        if (fp->fptr == 0) {
            // Truncate to zero — free entire chain
            if (fp->obj.sclust != 0) {
                res = remove_chain(&fp->obj, fp->obj.sclust, 0);
                if (res != FatResult::OK)
                    return res;
            }
            fp->obj.sclust = 0;
        } else {
            // Free chain after current position
            uint32_t ncl = get_fat(&fp->obj, fp->clust);
            if (ncl < 2) { /* already at end */
            } else if (ncl < fs->n_fatent) {
                res = remove_chain(&fp->obj, ncl, fp->clust);
                if (res != FatResult::OK)
                    return res;
            }
        }
        fp->obj.objsize = fp->fptr;
        fp->flag |= FA_MODIFIED;
    }

    return FatResult::OK;
}

FatResult FatFs::sync(FatFile* fp) noexcept {
    FatFsVolume* fs;
    auto res = validate(&fp->obj, &fs);
    if (res != FatResult::OK)
        return res;

    if (fp->flag & FA_MODIFIED) {
        // Write back directory entry
        if (fp->dir_sect != 0) {
            res = move_window(fs, fp->dir_sect);
            if (res != FatResult::OK)
                return res;

            auto* dir = fp->dir_ptr;
            dir[DIR_ATTR] |= AM_ARC;
            st_clust(fs, dir, fp->obj.sclust);
            st32(&dir[DIR_FILE_SIZE], static_cast<uint32_t>(fp->obj.objsize));
            uint32_t tm = get_fattime();
            st16(&dir[DIR_WRT_TIME], static_cast<uint16_t>(tm));
            st16(&dir[DIR_WRT_DATE], static_cast<uint16_t>(tm >> 16));
            st16(&dir[DIR_CRT_TIME], static_cast<uint16_t>(tm));
            st16(&dir[DIR_CRT_DATE], static_cast<uint16_t>(tm >> 16));
            fs->wflag = 1;

            fp->flag &= ~FA_MODIFIED;
        }

        res = sync_fs(fs);
    }

    return FatResult::OK;
}

// ============================================================================
// Public API: Directory operations
// ============================================================================

FatResult FatFs::opendir(FatDir* dp, const char* path) noexcept {
    if (!dp)
        return FatResult::INVALID_OBJECT;

    dp->obj.fs = nullptr;
    const char* p = path;
    FatFsVolume* fs;
    auto res = mount_volume(&p, &fs, 0);
    if (res != FatResult::OK)
        return res;

    dp->obj.fs = fs;
    dp->obj.id = fs->id;

    res = follow_path(dp, p);

    if (res == FatResult::OK) {
        if (dp->fn[NSFLAG] & NS_NONAME) {
            // Root directory
            dp->obj.sclust = (fs->fs_type == FS_FAT32) ? static_cast<uint32_t>(fs->dirbase) : 0;
        } else if (dp->dir[DIR_ATTR] & AM_DIR) {
            dp->obj.sclust = ld_clust(fs, dp->dir);
        } else {
            return FatResult::NO_PATH;
        }

        res = dir_sdi(dp, 0);
        if (res == FatResult::OK) {
            dp->obj.id = fs->id;
            // Clear LFN buffer
            if (fs->lfnbuf)
                fs->lfnbuf[0] = 0;
        }
    }

    if (res != FatResult::OK)
        dp->obj.fs = nullptr;
    return res;
}

FatResult FatFs::closedir(FatDir* dp) noexcept {
    FatFsVolume* fs;
    auto res = validate(&dp->obj, &fs);
    if (res == FatResult::OK) {
        dp->obj.fs = nullptr;
    }
    return res;
}

FatResult FatFs::readdir(FatDir* dp, FatFileInfo* fno) noexcept {
    FatFsVolume* fs;
    auto res = validate(&dp->obj, &fs);
    if (res != FatResult::OK)
        return res;

    if (!fno) {
        // Rewind
        res = dir_sdi(dp, 0);
        if (fs->lfnbuf)
            fs->lfnbuf[0] = 0;
        return res;
    }

    // Clear LFN buffer for this read
    if (fs->lfnbuf)
        fs->lfnbuf[0] = 0;

    res = dir_read(dp, 0);
    if (res == FatResult::OK) {
        get_fileinfo(dp, fno);
        // Advance to next
        dir_next(dp, 0);
    } else {
        fno->fname[0] = '\0';
        if (res == FatResult::NO_FILE)
            res = FatResult::OK;
    }

    return res;
}

FatResult FatFs::mkdir(const char* path) noexcept {
    const char* p = path;
    FatFsVolume* fs;
    auto res = mount_volume(&p, &fs, 1);
    if (res != FatResult::OK)
        return res;

    FatDir dj{};
    dj.obj.fs = fs;
    res = follow_path(&dj, p);

    if (res == FatResult::OK)
        return FatResult::EXIST;
    if (res != FatResult::NO_FILE)
        return res;

    // Allocate a cluster for the new directory
    FatObjId tmp_obj{};
    tmp_obj.fs = fs;
    uint32_t dcl = create_chain(&tmp_obj, 0);
    if (dcl == 0)
        return FatResult::DENIED;
    if (dcl == 1)
        return FatResult::INT_ERR;

    // Clear the directory cluster
    res = dir_clear(fs, dcl);
    if (res != FatResult::OK)
        return res;

    // Create . and .. entries
    res = move_window(fs, clst2sect(fs, dcl));
    if (res != FatResult::OK)
        return res;

    // "." entry — build once, then copy to ".."
    uint8_t* dir = fs->win;
    std::memset(dir, 0, DIR_ENTRY_SIZE);
    std::memset(dir + DIR_NAME, ' ', 11);
    dir[DIR_NAME] = '.';
    dir[DIR_ATTR] = AM_DIR;
    uint32_t tm = get_fattime();
    st32(&dir[DIR_WRT_TIME], tm);
    st_clust(fs, dir, dcl);

    // ".." entry — copy "." then patch
    std::memcpy(dir + DIR_ENTRY_SIZE, dir, DIR_ENTRY_SIZE);
    dir[DIR_ENTRY_SIZE + 1] = '.';
    uint32_t pcl = dj.obj.sclust;
    if (fs->fs_type == FS_FAT32 && pcl == static_cast<uint32_t>(fs->dirbase)) {
        pcl = 0;
    }
    st_clust(fs, dir + DIR_ENTRY_SIZE, pcl);

    fs->wflag = 1;

    // Register directory entry in parent
    res = dir_register(&dj);
    if (res != FatResult::OK)
        return res;

    dj.dir[DIR_ATTR] = AM_DIR;
    st32(&dj.dir[DIR_WRT_TIME], tm);
    st_clust(fs, dj.dir, dcl);
    fs->wflag = 1;

    res = sync_fs(fs);
    return res;
}

FatResult FatFs::unlink(const char* path) noexcept {
    const char* p = path;
    FatFsVolume* fs;
    auto res = mount_volume(&p, &fs, 1);
    if (res != FatResult::OK)
        return res;

    FatDir dj{};
    dj.obj.fs = fs;
    res = follow_path(&dj, p);
    if (res != FatResult::OK)
        return res;

    if (dj.fn[NSFLAG] & NS_NONAME)
        return FatResult::INVALID_NAME;
    if (dj.dir[DIR_ATTR] & AM_RDO)
        return FatResult::DENIED;

    uint32_t dcl = ld_clust(fs, dj.dir);

    if (dj.dir[DIR_ATTR] & AM_DIR) {
        // Check if directory is empty
        FatDir sdj{};
        sdj.obj.fs = fs;
        sdj.obj.sclust = dcl;
        res = dir_sdi(&sdj, 0);
        if (res != FatResult::OK)
            return res;

        // Skip . and ..
        res = dir_next(&sdj, 0);
        if (res == FatResult::OK)
            res = dir_next(&sdj, 0);
        if (res != FatResult::OK && res != FatResult::NO_FILE)
            return res;

        // Check for more entries
        if (res == FatResult::OK) {
            res = dir_read(&sdj, 0);
            if (res == FatResult::OK)
                return FatResult::DENIED; // not empty
            if (res != FatResult::NO_FILE)
                return res;
        }
    }

    res = dir_remove(&dj);
    if (res != FatResult::OK)
        return res;

    // Free cluster chain
    if (dcl != 0) {
        FatObjId tmp_obj{};
        tmp_obj.fs = fs;
        res = remove_chain(&tmp_obj, dcl, 0);
        if (res != FatResult::OK)
            return res;
    }

    return sync_fs(fs);
}

FatResult FatFs::rename(const char* path_old, const char* path_new) noexcept {
    const char* p_old = path_old;
    FatFsVolume* fs;
    auto res = mount_volume(&p_old, &fs, 1);
    if (res != FatResult::OK)
        return res;

    // Find old entry
    FatDir dj_old{};
    dj_old.obj.fs = fs;
    res = follow_path(&dj_old, p_old);
    if (res != FatResult::OK)
        return res;

    // Save old entry info
    uint32_t old_clust = ld_clust(fs, dj_old.dir);
    uint8_t old_attr = dj_old.dir[DIR_ATTR];
    FSIZE_t old_size = ld32(&dj_old.dir[DIR_FILE_SIZE]);
    LBA_t old_sect = dj_old.sect;
    uint8_t* old_dir_ptr = dj_old.dir;
    uint32_t old_blk_ofs = dj_old.blk_ofs;
    uint32_t old_dptr = dj_old.dptr;

    // Save timestamps
    uint16_t old_wrt_time = ld16(&dj_old.dir[DIR_WRT_TIME]);
    uint16_t old_wrt_date = ld16(&dj_old.dir[DIR_WRT_DATE]);
    uint16_t old_crt_time = ld16(&dj_old.dir[DIR_CRT_TIME]);
    uint16_t old_crt_date = ld16(&dj_old.dir[DIR_CRT_DATE]);

    // Check new path
    const char* p_new = path_new;
    // Skip volume prefix
    if (p_new[0] >= '0' && p_new[0] <= '9' && p_new[1] == ':') {
        p_new += 2;
    } else if (p_new[0] == ':') {
        p_new += 1;
    }

    FatDir dj_new{};
    dj_new.obj.fs = fs;
    res = follow_path(&dj_new, p_new);

    if (res == FatResult::OK) {
        // Destination exists — fail
        return FatResult::EXIST;
    }
    if (res != FatResult::NO_FILE)
        return res;

    // Register new entry
    res = dir_register(&dj_new);
    if (res != FatResult::OK)
        return res;

    // Fill new entry with old entry's data
    res = move_window(fs, dj_new.sect);
    if (res != FatResult::OK)
        return res;

    dj_new.dir[DIR_ATTR] = old_attr;
    st_clust(fs, dj_new.dir, old_clust);
    st32(&dj_new.dir[DIR_FILE_SIZE], static_cast<uint32_t>(old_size));
    st16(&dj_new.dir[DIR_WRT_TIME], old_wrt_time);
    st16(&dj_new.dir[DIR_WRT_DATE], old_wrt_date);
    st16(&dj_new.dir[DIR_CRT_TIME], old_crt_time);
    st16(&dj_new.dir[DIR_CRT_DATE], old_crt_date);
    fs->wflag = 1;

    // Sync to minimize cross-link window
    res = sync_fs(fs);
    if (res != FatResult::OK)
        return res;

    // Remove old entry
    dj_old.sect = old_sect;
    dj_old.dir = old_dir_ptr;
    dj_old.blk_ofs = old_blk_ofs;
    dj_old.dptr = old_dptr;
    res = dir_remove(&dj_old);
    if (res != FatResult::OK)
        return res;

    return sync_fs(fs);
}

FatResult FatFs::stat(const char* path, FatFileInfo* fno) noexcept {
    const char* p = path;
    FatFsVolume* fs;
    auto res = mount_volume(&p, &fs, 0);
    if (res != FatResult::OK)
        return res;

    FatDir dj{};
    dj.obj.fs = fs;
    res = follow_path(&dj, p);

    if (res == FatResult::OK) {
        if (dj.fn[NSFLAG] & NS_NONAME) {
            // Root directory
            fno->fname[0] = '\0';
            fno->altname[0] = '\0';
            fno->fattrib = AM_DIR;
            fno->fsize = 0;
            fno->fdate = 0;
            fno->ftime = 0;
        } else {
            get_fileinfo(&dj, fno);
        }
    }

    return res;
}

FatResult FatFs::getfree(const char* path, uint32_t* nclst, FatFsVolume** fatfs) noexcept {
    const char* p = path;
    FatFsVolume* fs;
    auto res = mount_volume(&p, &fs, 0);
    if (res != FatResult::OK)
        return res;

    *fatfs = fs;

    if (fs->free_clst <= fs->n_fatent - 2) {
        *nclst = fs->free_clst;
        return FatResult::OK;
    }

    // Need to scan FAT
    uint32_t nfree = 0;
    uint32_t clst = 2;
    FatObjId tmp_obj{};
    tmp_obj.fs = fs;

    for (; clst < fs->n_fatent; clst++) {
        uint32_t val = get_fat(&tmp_obj, clst);
        if (val == 1)
            return FatResult::INT_ERR;
        if (val == 0)
            nfree++;
    }

    fs->free_clst = nfree;
    fs->fsi_flag |= 1;
    *nclst = nfree;

    return FatResult::OK;
}

} // namespace umi::fs
