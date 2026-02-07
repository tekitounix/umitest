// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Public facade API for benchmark execution and result reporting.

// Core
#include <cstdint>

#include "umibench/core/measure.hh"
#include "umibench/core/runner.hh"
#include "umibench/core/stats.hh"

// Timer
#include "umibench/timer/concept.hh"

// Output
#include "umibench/output/concept.hh"

namespace umi::bench {

/// @brief Emit a full benchmark report line.
/// @tparam PlatformT Platform type that provides timer/output metadata.
/// @param name Benchmark name.
/// @param stats Aggregated benchmark statistics.
/// @note Output format:
/// `name=<name> target=<target> unit=<unit> n=<samples> iters=<iterations> min=<min> ... cv=<cv>%`
template <typename PlatformT>
void report(const char* name, const Stats& stats) {
    using Output = typename PlatformT::Output;
    Output::puts("name=");
    Output::puts(name);
    Output::puts(" target=");
    Output::puts(PlatformT::target_name());
    Output::puts(" unit=");
    Output::puts(PlatformT::timer_unit());
    Output::puts(" n=");
    Output::print_uint(static_cast<std::uint64_t>(stats.samples));
    Output::puts(" iters=");
    Output::print_uint(static_cast<std::uint64_t>(stats.iterations));
    Output::puts(" min=");
    Output::print_uint(stats.min);
    Output::puts(" max=");
    Output::print_uint(stats.max);
    Output::puts(" median=");
    Output::print_uint(stats.median);
    Output::puts(" mean=");
    Output::print_double(stats.mean);
    Output::puts(" stddev=");
    Output::print_double(stats.stddev);
    Output::puts(" cv=");
    Output::print_double(stats.cv());
    Output::puts("%\n");
}

/// @brief Emit a compact benchmark report line.
/// @tparam PlatformT Platform type that provides output implementation.
/// @param name Benchmark name.
/// @param stats Aggregated benchmark statistics.
/// @note Compact format prints minimum time and optional per-iteration net value.
template <typename PlatformT>
void report_compact(const char* name, const Stats& stats) {
    using Output = typename PlatformT::Output;
    Output::puts("  ");
    Output::puts(name);
    Output::puts(": ");
    Output::print_uint(stats.min);
    if (stats.iterations > 1) {
        Output::puts(" (net=");
        Output::print_uint(stats.min / stats.iterations);
        Output::puts("/iter)");
    }
    Output::puts("\n");
}

} // namespace umi::bench
