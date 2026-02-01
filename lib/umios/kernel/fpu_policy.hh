// SPDX-License-Identifier: MIT
// UMI-OS Kernel: FPU Policy — compile-time automatic determination
#pragma once

#include <cstdint>

namespace umi {

/// FPU context save/restore policy per task.
/// Determined automatically at compile time from TaskFpuDecl.
enum class FpuPolicy : uint8_t {
    Forbidden = 0,  ///< Task never uses FPU. Basic stack frame.
    Exclusive = 1,  ///< Task is the sole FPU user. Extended frame but no save/restore needed.
    LazyStack = 2,  ///< Multiple FPU tasks. Hardware lazy stacking (LSPACT).
};

/// Compile-time declaration of FPU usage per task.
struct TaskFpuDecl {
    bool audio   = true;
    bool system  = true;
    bool control = true;
    bool idle    = false;
};

/// Count how many tasks use FPU.
consteval int count_fpu_tasks(TaskFpuDecl decl) {
    return static_cast<int>(decl.audio) + static_cast<int>(decl.system)
         + static_cast<int>(decl.control) + static_cast<int>(decl.idle);
}

/// Determine FPU policy for a single task based on its declaration and the total FPU task count.
consteval FpuPolicy resolve_fpu_policy(bool uses_fpu, int total_fpu_tasks) {
    if (!uses_fpu) return FpuPolicy::Forbidden;
    if (total_fpu_tasks == 1) return FpuPolicy::Exclusive;
    return FpuPolicy::LazyStack;
}

/// Whether a policy requires an extended (FPU) stack frame.
consteval bool needs_fpu_frame(FpuPolicy policy) {
    return policy != FpuPolicy::Forbidden;
}

} // namespace umi
