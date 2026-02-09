rule("umiport.board")
    on_config(function(target)
        local board = target:values("umiport.board")
        if not board then return end

        local umiport_dir = path.join(os.scriptdir(), "..")
        local board_include = path.join(umiport_dir, "include/umiport/board", board)

        -- Board-specific includedirs (platform.hh resolution)
        target:add("includedirs", board_include, {public = false})

        -- Opt out of default startup with umiport.startup = "false"
        local use_startup = target:values("umiport.startup")
        if use_startup == "false" then return end

        -- Auto-add startup/syscalls/linker for embedded MCU targets
        local mcu = target:values("embedded.mcu")
        if mcu then
            local mcu_family = mcu:match("^(stm32%a%d)")
            if mcu_family then
                local mcu_src = path.join(umiport_dir, "src", mcu_family)
                target:add("files", path.join(mcu_src, "startup.cc"))
                target:add("files", path.join(mcu_src, "syscalls.cc"))

                -- Replace linker script: remove embedded rule's default,
                -- then add the project-specific one.
                local ld_path = path.join(mcu_src, "linker.ld")
                local old_flags = target:get("ldflags") or {}
                local new_flags = {}
                for _, flag in ipairs(old_flags) do
                    if not flag:find("^%-T") then
                        table.insert(new_flags, flag)
                    end
                end
                target:set("ldflags", new_flags)
                target:add("ldflags", "-T" .. ld_path, {force = true})
            end
        end
    end)
