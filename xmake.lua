-- =====================================================================
-- UMI-OS Build Configuration
-- =====================================================================
-- A modular RTOS for embedded audio/MIDI applications
--
-- Build targets:
--   Host (native):     xmake build test_kernel test_audio test_midi
--   ARM firmware:      xmake build firmware renode_test
--   All:               xmake build -a
--
-- Tasks:
--   xmake test         - Run host unit tests (xmake standard)
--   xmake show         - Show project info (xmake standard)
--   xmake clean        - Clean build artifacts (xmake standard)
--   xmake flash        - Flash target (arm-embedded plugin)
--   xmake debugger     - Debug target (arm-embedded plugin)
--   xmake emulator.*   - Renode tasks (arm-embedded plugin)
--   xmake deploy.*     - Deploy/serve WASM (arm-embedded plugin)
--
-- Configuration:
--   xmake f -m debug|release      - Build mode
--   xmake f --board=stm32f4|stub  - Target board
--   xmake f --kernel=mono|micro   - Kernel variant
-- =====================================================================

set_project("umi_os")
set_version("0.2.0")
set_xmakever("2.8.0")

-- =====================================================================
-- Package Repositories
-- =====================================================================

-- arm-embedded toolchain and plugins
if os.isdir(path.join(os.projectdir(), ".refs/arm-embedded-xmake-repo")) then
    add_repositories("arm-embedded " .. path.join(os.projectdir(), ".refs/arm-embedded-xmake-repo"))
else
    add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")
end

-- Arm embedded toolchain (optional, for firmware builds)
add_requires("arm-embedded", {optional = true})

-- =====================================================================
-- Language and Build Settings
-- =====================================================================

set_languages("c++23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
set_warnings("all", "extra", "error")

-- =====================================================================
-- Options
-- =====================================================================

option("board")
    set_default("stub")
    set_showmenu(true)
    set_description("Target board")
    set_values("stm32f4", "stub")
option_end()

option("kernel")
    set_default("mono")
    set_showmenu(true)
    set_description("Kernel configuration")
    set_values("mono", "micro")
option_end()

-- =====================================================================
-- Includes (Migrated Libraries Only)
-- =====================================================================

-- Individual libraries (lib/umi is being migrated)
includes("lib/umimmio")
includes("lib/umitest")
includes("lib/umibench")
includes("lib/umistring")

-- =====================================================================
-- WASM Targets (Emscripten) - 移行完了後に再有効化
-- =====================================================================

-- includes("examples/headless_webhost")

-- =====================================================================
-- STM32F4 Kernel + Application - 移行完了後に再有効化
-- =====================================================================

-- includes("examples/stm32f4_kernel")
-- includes("examples/synth_app")
-- includes("examples/daisy_pod_kernel")
-- includes("examples/daisy_pod_synth_h7")
