#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Convenience umbrella header — includes Suite and PlainSuite aliases.
/// @author Shota Moriguchi @tekitounix

#include <umitest/reporters/plain.hh> // IWYU pragma: export
#include <umitest/reporters/stdio.hh> // IWYU pragma: export
#include <umitest/suite.hh>           // IWYU pragma: export

namespace umi::test {

/// @brief Default Suite using StdioReporter.
using Suite = BasicSuite<StdioReporter>;

/// @brief Plain Suite without ANSI colors.
using PlainSuite = BasicSuite<PlainReporter>;

} // namespace umi::test
