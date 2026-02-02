#pragma once
#include <concepts>
#include <cstdint>

namespace hal {

/// CacheOps - cache control (CM7 etc.)
template <typename T>
concept CacheOps = requires(T& cache, void* addr, std::size_t size) {
    { cache.enable_icache() } -> std::same_as<void>;
    { cache.enable_dcache() } -> std::same_as<void>;
    { cache.disable_icache() } -> std::same_as<void>;
    { cache.disable_dcache() } -> std::same_as<void>;
    { cache.invalidate_dcache(addr, size) } -> std::same_as<void>;
    { cache.clean_dcache(addr, size) } -> std::same_as<void>;
};

/// FpuOps - FPU configuration
template <typename T>
concept FpuOps = requires(T& fpu) {
    { fpu.enable() } -> std::same_as<void>;
};

/// ContextSwitch - task context switching
template <typename T>
concept ContextSwitch = requires(T& ctx, void* stack_ptr) {
    { ctx.save_context() } -> std::convertible_to<void*>;
    { ctx.restore_context(stack_ptr) } -> std::same_as<void>;
};

/// ArchTraits - architecture-specific constants
template <typename T>
concept ArchTraits = requires {
    { T::has_dcache } -> std::convertible_to<bool>;
    { T::has_icache } -> std::convertible_to<bool>;
    { T::has_fpu } -> std::convertible_to<bool>;
    { T::has_double_fpu } -> std::convertible_to<bool>;
    { T::mpu_regions } -> std::convertible_to<std::uint8_t>;
};

} // namespace hal
