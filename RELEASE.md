# Release and Versioning Policy

## Current Release Line

- Current version: `0.2.0`
- Stability intent: pre-release (`0.x.y`)

## Published Libraries

| Library | Description |
|---------|-------------|
| umitest | Zero-macro test framework |
| umimmio | Type-safe MMIO register abstraction |
| umirtm  | RTT-compatible debug monitor |
| umibench | Cross-target microbenchmark |
| umiport | Shared platform infrastructure (STM32F4) |

## Versioning Rules

UMI uses **unified versioning** across all published libraries.
All libraries share the same version number, managed from the root `VERSION` file.

Semantic Versioning applies:

During `0.x.y` (current):
- Breaking API changes: bump minor (`0.x.0` where `x` increases)
- Backward-compatible features/fixes: bump patch (`0.x.y` where `y` increases)

After `1.0.0`:
- Breaking changes: bump major
- New features (backward-compatible): bump minor
- Bug fixes: bump patch

## Changelog Rules

- Every release must update each library's `CHANGELOG.md`.
- Keep `[Unreleased]` at top for pending changes.
- Move entries into versioned section at release cut.
- Libraries with no changes record "No changes" in their version entry.

## Release Checklist

1. Confirm `xmake test` passes (host + WASM + compile-fail).
2. Run `tools/release.sh <version>` which:
   - Updates root `VERSION` and all `lib/*/VERSION`
   - Updates `Doxyfile` `PROJECT_NUMBER` values
   - Updates `xmake.lua` `set_version()`
   - Updates `CHANGELOG.md` release sections
   - Generates release archives with sha256
   - Creates commit and tags (unified + per-library)
3. Push: `git push origin main --tags`
4. Update `synthernet-xmake-repo` with sha256 hashes.

## CI Scope

- Required: host tests + compile-fail + WASM tests
- ARM cross-build: gcc-arm build verification
- Doxygen: generation verification
- Optional/manual: Renode smoke checks (environment/tooling dependent)
