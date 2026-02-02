// SPDX-License-Identifier: MIT
// UMI-OS Backend: Cortex-M IRQ System Implementation
//
// Provides the SRAM-resident vector table instance.

#include <common/irq.hh>

namespace umi::backend::cm {

// Global SRAM vector table instance
// Placed in .data section (initialized SRAM)
VectorTableRAM<UMI_CM_NUM_IRQS> g_vector_table;

}  // namespace umi::backend::cm
