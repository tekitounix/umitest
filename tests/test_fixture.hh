// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Shared test declarations for umitest self-tests.
#pragma once

#include <umitest/test.hh>

namespace umitest::test {

/// @brief Register assertion edge-case tests.
void run_assertion_tests(umi::test::Suite& suite);

/// @brief Register Suite workflow and counting tests.
void run_suite_workflow_tests(umi::test::Suite& suite);

/// @brief Register format_value output tests.
void run_format_tests(umi::test::Suite& suite);

} // namespace umitest::test
