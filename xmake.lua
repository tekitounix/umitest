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
-- UMI Package (provides all library include paths)
-- =====================================================================

includes("lib/umi")

-- =====================================================================
-- WASM Targets (Emscripten)
-- =====================================================================

-- Include headless_webhost subproject
includes("examples/headless_webhost")

-- =====================================================================
-- STM32F4 Kernel + Application (Separated Architecture)
-- =====================================================================

includes("examples/stm32f4_kernel")
includes("examples/synth_app")
includes("examples/daisy_pod_kernel")
includes("examples/daisy_pod_synth_h7")

