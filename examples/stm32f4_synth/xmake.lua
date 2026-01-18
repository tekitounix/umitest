-- STM32F4-Discovery Synthesizer
-- Uses CS43L22 audio codec (I2S) and USB MIDI

target("stm32f4_synth")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", "examples/stm32f4_synth/linker.ld")
    set_values("embedded.optimize", "size")

    -- Platform includes (Cortex-M backend)
    add_includedirs("$(projectdir)/lib/umios/backend/cm")
    add_includedirs("$(projectdir)/lib/umios")
    add_includedirs("$(projectdir)/lib")

    -- Synth engine from headless_webhost (unchanged)
    add_includedirs("$(projectdir)/examples/headless_webhost/src")

    -- DSP library for synth
    add_deps("umi.core", "umi.dsp")

    add_defines("STM32F4", "STM32F407xx", "BOARD_STM32F4_DISCOVERY")
    add_defines("HSE_VALUE=8000000")  -- 8MHz crystal

    add_files("$(projectdir)/examples/stm32f4_synth/src/*.cc")
target_end()

-- Flash task
task("flash-synth")
    set_category("action")
    on_run(function ()
        print("Building STM32F4 Synth...")
        os.exec("xmake build stm32f4_synth")
        print("\nFlashing via ST-Link...")
        -- Use st-flash with binary format
        os.exec("st-flash write build/stm32f4_synth/release/stm32f4_synth.bin 0x08000000")
    end)
    set_menu {usage = "xmake flash-synth", description = "Build and flash STM32F4 synth"}
task_end()
