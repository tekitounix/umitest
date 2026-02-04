-- SPDX-License-Identifier: MIT
-- UMI bench framework

local bench_dir = os.scriptdir()
local include_dir = path.join(bench_dir, "include")
local platform_host_dir = path.join(bench_dir, "platform/host")
local platform_renode_dir = path.join(bench_dir, "platform/renode")
local stm32f4_linker = path.join(bench_dir, "target/stm32f4/linker.ld")
-- Header-only framework
target("umi.bench")
    set_kind("headeronly")
    set_group("umi")
    add_includedirs(include_dir, {public = true})
target_end()

includes("test")

local function stm32f4_bench_target(name, opts)
    opts = opts or {}
    target(name)
        set_group(opts.group or "bench")
        set_default(false)
        add_rules("embedded")
        set_values("embedded.mcu", "stm32f407vg")
        set_values("embedded.linker_script", stm32f4_linker)
        if opts.optimize then
            set_values("embedded.optimize", opts.optimize)
        end
        -- Keep self-contained (no external deps)
        add_includedirs(include_dir, platform_renode_dir)
        if opts.startup then
            add_files(opts.startup)
        end
        add_files(opts.source)
        if opts.renode_script then
            on_run(function(target)
                local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
                if not os.isfile(renode) then
                    renode = "renode"
                end
                os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. opts.renode_script})
            end)
        end
    target_end()
end

stm32f4_bench_target("bench_stm32f4", {
    source = path.join(bench_dir, "test/bench_stm32f4.cc"),
    startup = path.join(bench_dir, "target/stm32f4/startup.cc"),
    optimize = "fast",
    renode_script = "lib/umi/bench/test/renode/bench_stm32f4.resc"
})

stm32f4_bench_target("bench_stm32f4_single", {
    source = path.join(bench_dir, "test/bench_stm32f4_single.cc"),
    optimize = "fast",
    renode_script = "lib/umi/bench/test/renode/bench_stm32f4.resc"
})

stm32f4_bench_target("bench_verify_dwt", {
    source = path.join(bench_dir, "test/verify_dwt_cycles.cc"),
    optimize = "fast",
    renode_script = "lib/umi/bench/test/renode/verify_dwt.resc"
})

stm32f4_bench_target("comprehensive_bench", {
    source = path.join(bench_dir, "test/comprehensive_bench.cc"),
    startup = path.join(bench_dir, "target/stm32f4/startup.cc"),
    optimize = "fast",
    renode_script = "lib/umi/bench/test/renode/comprehensive_bench.resc"
})
