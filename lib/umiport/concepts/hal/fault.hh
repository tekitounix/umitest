#pragma once
#include <concepts>
#include <cstdint>

namespace hal {

/// FaultReport - fault information interface
template <typename T>
concept FaultReport = requires(const T& report) {
    { report.fault_type() } -> std::convertible_to<std::uint32_t>;
    { report.fault_address() } -> std::convertible_to<std::uintptr_t>;
    { report.stack_pointer() } -> std::convertible_to<std::uintptr_t>;
};

} // namespace hal
