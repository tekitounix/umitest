// SPDX-License-Identifier: BSD-3-Clause
// IS25LP064A on Daisy Seed (STM32H750)
// Device information for STM32CubeProgrammer external loader

#include "Dev_Inf.h"

struct StorageInfo const StorageInfo = {
    "IS25LP064A_DaisySeed_H750",   // Device Name
    NOR_FLASH,                      // Device Type
    0x90000000,                     // Device Start Address (QSPI mapped)
    0x00800000,                     // Device Size: 8 MBytes
    0x100,                          // Programming Page Size: 256 Bytes
    0xFF,                           // Initial Content of Erased Memory
    // Sector layout: 2048 sectors of 4KB
    {
        { 0x00000800, 0x00001000 }, // 2048 sectors, 4KB each
        { 0x00000000, 0x00000000 },
    }
};
