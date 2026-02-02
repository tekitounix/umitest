-- Daisy Pod H7 Synth Application (.umia)
-- Runs on daisy_pod_kernel via QSPI XIP

target("daisy_pod_synth_h7")
    set_group("apps")
    set_default(false)

    add_rules("embedded")
    set_values("embedded.mcu", "stm32h750ib")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "app.ld"))
    set_values("embedded.optimize", "size")

    add_files("src/*.cc")

    add_defines("UMIOS_APP=1")

    add_ldflags("-nodefaultlibs", {force = true})
target_end()
