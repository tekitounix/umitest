// SPDX-License-Identifier: MIT
// Tests for syscall number definitions and AudioContext

#include <umitest.hh>
#include <syscall_nr.hh>
#include <audio_context.hh>

using namespace umitest;
namespace sc = umi::syscall;

// =============================================================================
// Syscall number tests
// =============================================================================

bool test_syscall_groups(TestContext& t) {
    // Process Control (0–9)
    t.assert_eq(sc::nr::exit, 0u);
    t.assert_eq(sc::nr::yield, 1u);
    t.assert_eq(sc::nr::register_proc, 2u);
    t.assert_eq(sc::nr::unregister_proc, 3u);

    // Time / Scheduling (10–19)
    t.assert_eq(sc::nr::wait_event, 10u);
    t.assert_eq(sc::nr::get_time, 11u);
    t.assert_eq(sc::nr::sleep, 12u);

    // Configuration (20–29)
    t.assert_eq(sc::nr::set_app_config, 20u);
    // Nr 21-24: reserved (consolidated into set_app_config)

    // Info (40–49)
    t.assert_eq(sc::nr::get_shared, 40u);

    // I/O (50–59)
    t.assert_eq(sc::nr::log, 50u);
    t.assert_eq(sc::nr::panic, 51u);

    // Filesystem (60–84)
    t.assert_eq(sc::nr::file_open, 60u);
    t.assert_eq(sc::nr::fs_result, 84u);
    return true;
}

bool test_syscall_range_helpers(TestContext& t) {
    // is_fs_syscall: 60–84
    t.assert_true(!sc::is_fs_syscall(59), "59 not FS");
    t.assert_true(sc::is_fs_syscall(60), "60 is FS");
    t.assert_true(sc::is_fs_syscall(84), "84 is FS");
    t.assert_true(!sc::is_fs_syscall(85), "85 not FS");

    // is_fs_request: 60–83 (excludes fs_result=84)
    t.assert_true(sc::is_fs_request(60), "60 is request");
    t.assert_true(sc::is_fs_request(83), "83 is request");
    t.assert_true(!sc::is_fs_request(84), "84 not request");
    return true;
}

bool test_syscall_errors(TestContext& t) {
    t.assert_eq(static_cast<int32_t>(sc::SyscallError::OK), 0);
    t.assert_eq(static_cast<int32_t>(sc::SyscallError::INVALID_SYSCALL), -1);
    t.assert_eq(static_cast<int32_t>(sc::SyscallError::NOT_FOUND), -4);
    return true;
}

bool test_event_bits(TestContext& t) {
    t.assert_eq(sc::event::audio, 1u << 0);
    t.assert_eq(sc::event::midi, 1u << 1);
    t.assert_eq(sc::event::fs, 1u << 5);
    t.assert_eq(sc::event::shutdown, 1u << 31);

    // No overlap
    t.assert_eq(sc::event::audio & sc::event::midi, 0u);
    t.assert_eq(sc::event::audio & sc::event::fs, 0u);
    return true;
}

// =============================================================================
// AudioContext tests
// =============================================================================

bool test_audio_context_basic(TestContext& t) {
    constexpr uint32_t BUF_SIZE = 64;
    umi::sample_t in_l[BUF_SIZE] = {};
    umi::sample_t in_r[BUF_SIZE] = {};
    umi::sample_t out_l[BUF_SIZE] = {};
    umi::sample_t out_r[BUF_SIZE] = {};

    const umi::sample_t* ins[] = {in_l, in_r};
    umi::sample_t* outs[] = {out_l, out_r};

    umi::EventQueue<> eq;

    umi::AudioContext ctx{
        .inputs = ins,
        .outputs = outs,
        .input_events = {},
        .output_events = eq,
        .sample_rate = 48000,
        .buffer_size = BUF_SIZE,
        .dt = 1.0f / 48000.0f,
        .sample_position = 0,
    };

    t.assert_eq(ctx.num_inputs(), 2u);
    t.assert_eq(ctx.num_outputs(), 2u);
    t.assert_eq(ctx.input(0), in_l);
    t.assert_eq(ctx.input(1), in_r);
    t.assert_eq(ctx.output(0), out_l);
    t.assert_eq(ctx.output(1), out_r);
    t.assert_eq(ctx.input(99), static_cast<const umi::sample_t*>(nullptr));
    t.assert_eq(ctx.output(99), static_cast<umi::sample_t*>(nullptr));
    t.assert_near(ctx.dt, 1.0f / 48000.0f);
    return true;
}

bool test_audio_context_clear_outputs(TestContext& t) {
    constexpr uint32_t BUF_SIZE = 16;
    umi::sample_t out_l[BUF_SIZE];
    umi::sample_t out_r[BUF_SIZE];
    for (auto& s : out_l) s = 1.0f;
    for (auto& s : out_r) s = 1.0f;

    umi::sample_t* outs[] = {out_l, out_r};
    umi::EventQueue<> eq;

    umi::AudioContext ctx{
        .inputs = {},
        .outputs = outs,
        .input_events = {},
        .output_events = eq,
        .sample_rate = 48000,
        .buffer_size = BUF_SIZE,
        .dt = 1.0f / 48000.0f,
        .sample_position = 0,
    };

    ctx.clear_outputs();

    for (uint32_t i = 0; i < BUF_SIZE; ++i) {
        t.assert_eq(out_l[i], 0.0f);
        t.assert_eq(out_r[i], 0.0f);
    }
    return true;
}

bool test_audio_context_passthrough(TestContext& t) {
    constexpr uint32_t BUF_SIZE = 8;
    umi::sample_t in_buf[BUF_SIZE];
    umi::sample_t out_buf[BUF_SIZE] = {};
    for (uint32_t i = 0; i < BUF_SIZE; ++i) in_buf[i] = static_cast<float>(i);

    const umi::sample_t* ins[] = {in_buf};
    umi::sample_t* outs[] = {out_buf};
    umi::EventQueue<> eq;

    umi::AudioContext ctx{
        .inputs = ins,
        .outputs = outs,
        .input_events = {},
        .output_events = eq,
        .sample_rate = 48000,
        .buffer_size = BUF_SIZE,
        .dt = 1.0f / 48000.0f,
        .sample_position = 0,
    };

    ctx.passthrough();

    for (uint32_t i = 0; i < BUF_SIZE; ++i) {
        t.assert_eq(out_buf[i], static_cast<float>(i));
    }
    return true;
}

// =============================================================================

int main() {
    Suite s("syscall_context");

    s.section("Syscall Numbers");
    s.run("groups", test_syscall_groups);
    s.run("range_helpers", test_syscall_range_helpers);
    s.run("error_codes", test_syscall_errors);
    s.run("event_bits", test_event_bits);

    s.section("AudioContext");
    s.run("basic", test_audio_context_basic);
    s.run("clear_outputs", test_audio_context_clear_outputs);
    s.run("passthrough", test_audio_context_passthrough);

    return s.summary();
}
