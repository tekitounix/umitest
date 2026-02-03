// SPDX-License-Identifier: MIT
// Minimal binary to measure reference FATfs C code size on ARM.

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>
#include <cstdint>
#include <cstring>

extern "C" {
#include "ff.h"
#include "diskio.h"
}

static constexpr uint32_t BLK_SZ = 512;
static constexpr uint32_t BLK_CNT = 128;
static uint8_t storage[BLK_SZ * BLK_CNT];

extern "C" {

DWORD get_fattime(void) {
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

DSTATUS disk_initialize(BYTE) { return 0; }
DSTATUS disk_status(BYTE) { return 0; }

DRESULT disk_read(BYTE, BYTE* buff, LBA_t sector, UINT count) {
    std::memcpy(buff, &storage[sector * BLK_SZ], count * BLK_SZ);
    return RES_OK;
}

DRESULT disk_write(BYTE, const BYTE* buff, LBA_t sector, UINT count) {
    std::memcpy(&storage[sector * BLK_SZ], buff, count * BLK_SZ);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE, BYTE cmd, void* buff) {
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *static_cast<LBA_t*>(buff) = BLK_CNT; return RES_OK;
    case GET_SECTOR_SIZE: *static_cast<WORD*>(buff) = BLK_SZ; return RES_OK;
    case GET_BLOCK_SIZE: *static_cast<DWORD*>(buff) = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

} // extern "C"

extern "C" [[noreturn]] void _start() {
    // Format FAT12
    std::memset(storage, 0, sizeof(storage));
    uint8_t* bs = storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02;
    bs[13] = 1; bs[14] = 1; bs[15] = 0; bs[16] = 2;
    bs[17] = 0x40; bs[18] = 0x00;
    bs[19] = 128; bs[20] = 0; bs[21] = 0xF8;
    bs[22] = 1; bs[23] = 0; bs[24] = 0x3F; bs[25] = 0;
    bs[26] = 0xFF; bs[27] = 0; bs[38] = 0x29;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT12   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    uint8_t* fat1 = &storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
    std::memcpy(&storage[512 * 2], fat1, 3);

    FATFS vol{};
    f_mount(&vol, "", 1);

    FIL fp{};
    UINT bw, br;
    f_open(&fp, "T.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    char data[16] = "hello";
    f_write(&fp, data, sizeof(data), &bw);
    f_lseek(&fp, 0);
    f_truncate(&fp);
    f_sync(&fp);
    f_close(&fp);

    f_open(&fp, "T.TXT", FA_READ);
    f_read(&fp, data, sizeof(data), &br);
    f_close(&fp);

    f_mkdir("DIR");
    FILINFO fno{};
    f_stat("DIR", &fno);
    f_rename("DIR", "DIR2");
    f_unlink("DIR2");

    DIR dp{};
    f_opendir(&dp, "");
    f_readdir(&dp, &fno);
    f_closedir(&dp);

    DWORD nclst;
    FATFS* vp;
    f_getfree("", &nclst, &vp);
    f_unmount("");

    while (true) { __asm volatile("wfi"); }
}
