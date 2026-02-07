-- UMI release task
-- Usage: xmake release --ver=0.3.0 [--libs umimmio,umitest] [--dry-run]
-- Config: tools/release_config.lua controls which libraries are publishable

task("release")
    set_category("action")

    on_run(function()
        import("core.base.option")

        local version = option.get("ver")
        if not version then
            raise("--ver is required (e.g., xmake release --ver=0.3.0)")
        end
        if not version:match("^%d+%.%d+%.%d+$") then
            raise("version must be semver format: MAJOR.MINOR.PATCH (got: %s)", version)
        end

        local dry_run    = option.get("dry-run")
        local no_test    = option.get("no-test")
        local no_tag     = option.get("no-tag")
        local no_archive = option.get("no-archive")

        -- load release config
        local project_root = os.projectdir()
        local config_path = path.join(project_root, "tools", "release_config.lua")
        local config = io.load(config_path)
        if not config then
            raise("failed to load release config: %s", config_path)
        end

        local proj    = config.project or {}
        local lib_dir = proj.lib_dir or "lib"
        local out_dir = proj.output_dir or "build/packages"
        local commit_fmt       = proj.commit_message or "release: v%s"
        local changelog_marker = proj.changelog_marker or "## [Unreleased]"
        local archive_files    = proj.archive_files or {"VERSION", "LICENSE", "README.md", "CHANGELOG.md"}
        local archive_dirs     = proj.archive_dirs or {"include", "platforms", "src", "renode"}
        local lib_config       = config.libs or {}

        -- resolve library list
        local libs
        local libs_opt = option.get("libs")
        if libs_opt then
            -- explicit --libs: validate each name exists in config
            libs = {}
            for name in libs_opt:gmatch("[^,]+") do
                name = name:match("^%s*(.-)%s*$") -- trim
                if not lib_config[name] then
                    raise("unknown library: %s (not in release_config.lua)", name)
                end
                table.insert(libs, name)
            end
        else
            -- default: all libraries with publish = true
            libs = {}
            local skipped = {}
            for name, entry in pairs(lib_config) do
                if entry.publish then
                    table.insert(libs, name)
                else
                    table.insert(skipped, name)
                end
            end
            table.sort(libs)
            table.sort(skipped)
            if #skipped > 0 then
                print("Skipping unpublished libraries: %s", table.concat(skipped, ", "))
            end
        end

        if #libs == 0 then
            raise("no libraries selected for release")
        end

        -- validate library directories exist
        for _, name in ipairs(libs) do
            local dir = path.join(project_root, lib_dir, name)
            if not os.isdir(dir) then
                raise("library not found: %s (expected at %s)", name, dir)
            end
        end

        local date = os.date("%Y-%m-%d")
        local prefix = dry_run and "[dry-run] " or ""

        print("%s=== Releasing v%s ===", prefix, version)
        print("%sLibraries: %s", prefix, table.concat(libs, ", "))
        print("")

        -- step 1: pre-checks (skip in dry-run)
        if not dry_run and not no_tag then
            print("Step 1/6: Pre-checks...")
            local ok1 = os.execv("git", {"diff", "--quiet"}, {try = true})
            local ok2 = os.execv("git", {"diff", "--cached", "--quiet"}, {try = true})
            if ok1 ~= 0 or ok2 ~= 0 then
                raise("uncommitted changes detected. Commit or stash first.")
            end
            print("  ok: working tree clean")
        else
            print("%sStep 1/6: Pre-checks (skipped)", prefix)
        end

        -- step 2: tests
        if not no_test then
            print("%sStep 2/6: Running tests...", prefix)
            if not dry_run then
                local ok = os.execv("xmake", {"test"}, {try = true})
                if ok ~= 0 then
                    raise("tests failed")
                end
            end
            print("  ok: tests passed")
        else
            print("%sStep 2/6: Tests (skipped)", prefix)
        end

        -- step 3: update version files
        print("%sStep 3/6: Updating version files...", prefix)

        -- root VERSION
        local root_version = path.join(project_root, "VERSION")
        print("  %s%s", prefix, root_version)
        if not dry_run then
            io.writefile(root_version, version .. "\n")
        end

        -- root xmake.lua: set_version
        local root_xmake = path.join(project_root, "xmake.lua")
        local xmake_content = io.readfile(root_xmake)
        local new_xmake = xmake_content:gsub('set_version%(".-"%)', 'set_version("' .. version .. '")')
        if new_xmake ~= xmake_content then
            print("  %s%s (set_version)", prefix, root_xmake)
            if not dry_run then
                io.writefile(root_xmake, new_xmake)
            end
        end

        -- per-library VERSION + Doxyfile + CHANGELOG
        for _, name in ipairs(libs) do
            local ldir = path.join(project_root, lib_dir, name)

            -- VERSION
            local ver_file = path.join(ldir, "VERSION")
            if os.isfile(ver_file) then
                print("  %s%s", prefix, ver_file)
                if not dry_run then
                    io.writefile(ver_file, version .. "\n")
                end
            end

            -- Doxyfile: PROJECT_NUMBER
            local doxyfile = path.join(ldir, "Doxyfile")
            if os.isfile(doxyfile) then
                local content = io.readfile(doxyfile)
                local updated = content:gsub("PROJECT_NUMBER%s*=%s*[^\n]+",
                    "PROJECT_NUMBER         = " .. version)
                if updated ~= content then
                    print("  %s%s (PROJECT_NUMBER)", prefix, doxyfile)
                    if not dry_run then
                        io.writefile(doxyfile, updated)
                    end
                end
            end

            -- CHANGELOG
            local changelog = path.join(ldir, "CHANGELOG.md")
            if os.isfile(changelog) then
                local content = io.readfile(changelog)
                local pos = content:find(changelog_marker, 1, true)
                if pos then
                    local replacement = changelog_marker .. "\n\n## [" .. version .. "] - " .. date
                    local updated = content:sub(1, pos - 1) .. replacement .. content:sub(pos + #changelog_marker)
                    print("  %s%s (new release section)", prefix, changelog)
                    if not dry_run then
                        io.writefile(changelog, updated)
                    end
                end
            end
        end

        -- step 4: generate archives
        local sha_results = {}
        if not no_archive then
            print("%sStep 4/6: Generating archives...", prefix)
            local pkg_dir = path.join(project_root, out_dir)
            if not dry_run then
                os.mkdir(pkg_dir)
            end

            for _, name in ipairs(libs) do
                local ldir = path.join(project_root, lib_dir, name)
                local archive_name = name .. "-" .. version
                local staging_dir = path.join(pkg_dir, archive_name)
                local archive_path = path.join(pkg_dir, archive_name .. ".tar.gz")

                print("  %s%s", prefix, archive_path)

                if not dry_run then
                    -- clean staging
                    os.tryrm(staging_dir)
                    os.mkdir(staging_dir)

                    -- copy files
                    for _, f in ipairs(archive_files) do
                        local src = path.join(ldir, f)
                        if os.isfile(src) then
                            os.cp(src, path.join(staging_dir, f))
                        end
                    end

                    -- copy directories
                    for _, d in ipairs(archive_dirs) do
                        local src = path.join(ldir, d)
                        if os.isdir(src) then
                            os.cp(src, path.join(staging_dir, d))
                        end
                    end

                    -- create tar.gz with relative paths
                    local old_dir = os.cd(pkg_dir)
                    os.execv("tar", {"czf", archive_name .. ".tar.gz", archive_name})
                    os.cd(old_dir)

                    -- compute sha256
                    local sha = hash.sha256(archive_path)
                    sha_results[name] = sha

                    -- write sha256 file
                    io.writefile(archive_path .. ".sha256", sha .. "  " .. archive_name .. ".tar.gz\n")

                    -- clean staging
                    os.tryrm(staging_dir)
                end
            end
        else
            print("%sStep 4/6: Archives (skipped)", prefix)
        end

        -- step 5: git commit + tags
        local commit_msg = string.format(commit_fmt, version)
        if not no_tag then
            print("%sStep 5/6: Git commit and tags...", prefix)
            if not dry_run then
                os.execv("git", {"add", "-A"})
                os.execv("git", {"commit", "-m", commit_msg})
                os.execv("git", {"tag", "v" .. version})
                for _, name in ipairs(libs) do
                    local tag = name .. "/v" .. version
                    os.execv("git", {"tag", tag})
                    print("  tag: %s", tag)
                end
            else
                print("  %scommit: %s", prefix, commit_msg)
                print("  %stag: v%s", prefix, version)
                for _, name in ipairs(libs) do
                    print("  %stag: %s/v%s", prefix, name, version)
                end
            end
        else
            print("%sStep 5/6: Git (skipped)", prefix)
        end

        -- step 6: summary
        print("")
        print("%s=== Release v%s complete ===", prefix, version)
        print("")

        if not no_archive and not dry_run then
            print("Archives:")
            for _, name in ipairs(libs) do
                local archive_name = name .. "-" .. version .. ".tar.gz"
                print("  %s/%s", out_dir, archive_name)
            end
            print("")
            print("SHA256 (for xmake-repo package definitions):")
            for _, name in ipairs(libs) do
                local sha = sha_results[name]
                if sha then
                    print("  %s: add_versions(\"%s\", \"%s\")", name, version, sha)
                end
            end
            print("")
        end

        if not no_tag then
            if dry_run then
                print("Next steps (after real run):")
            else
                print("Next steps:")
            end
            print("  git push origin main --tags")
            print("  Update xmake-repo/synthernet with SHA256 hashes above")
        end
    end)

    set_menu {
        usage = "xmake release --ver=0.3.0 [options]",
        description = "Release UMI libraries (version bump, archive, tag)",
        options = {
            {nil, "ver",        "kv", nil, "Version to release (e.g., 0.3.0)"},
            {'l', "libs",       "kv", nil, "Comma-separated library list (default: all publishable)"},
            {'d', "dry-run",    "k",  nil, "Show changes without executing"},
            {nil, "no-test",    "k",  nil, "Skip test execution"},
            {nil, "no-tag",     "k",  nil, "Skip git commit and tag creation"},
            {nil, "no-archive", "k",  nil, "Skip archive generation"},
        }
    }
task_end()
