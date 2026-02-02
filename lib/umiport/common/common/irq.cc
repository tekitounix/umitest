// SPDX-License-Identifier: MIT
// UMI-OS Backend: Cortex-M IRQ System Implementation
//
// Provides the SRAM-resident vector table instance.

#include <common/irq.hh>

namespace umi::backend::cm {

// Global vector table instance — placed in DTCM to avoid D-cache coherency issues.
// Cortex-M7 vector fetch bypasses D-cache for TCM regions, ensuring handlers
// are always visible to the hardware without explicit cache maintenance.
__attribute__((section(".dtcmram_bss")))
VectorTableRAM<UMI_CM_NUM_IRQS> g_vector_table;

}  // namespace umi::backend::cm
