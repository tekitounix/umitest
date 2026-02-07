-- Release configuration for UMI libraries
--
-- [project]  : project-wide settings
-- [libs]     : per-library release settings
--   publish    : include in release (version bump, archive, tag)
--   headeronly : true for header-only libs, false if src/ must be packaged
--
-- To add a new library: add an entry to [libs] and ensure lib/<name>/ exists.
-- To exclude from release: set publish = false.
{
    project = {
        -- directory containing library subdirectories (relative to project root)
        lib_dir = "lib",
        -- output directory for archives (relative to project root)
        output_dir = "build/packages",
        -- git commit message format (%s is replaced with version)
        commit_message = "release: v%s",
        -- CHANGELOG section marker (plain text, not pattern)
        changelog_marker = "## [Unreleased]",
        -- files to include in archive (if they exist in each library)
        archive_files = {"VERSION", "LICENSE", "README.md", "CHANGELOG.md"},
        -- directories to include in archive (if they exist in each library)
        archive_dirs = {"include", "platforms", "src", "renode"}
    },
    libs = {
        umitest = {
            publish = true,
            headeronly = true
        },
        umimmio = {
            publish = true,
            headeronly = true
        },
        umirtm = {
            publish = true,
            headeronly = true
        },
        umibench = {
            publish = true,
            headeronly = true
        },
        umiport = {
            publish = false,
            headeronly = false
        }
    }
}
