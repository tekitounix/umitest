# Changelog

All notable changes to `umibench` are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
with pre-release tags during beta.

## [Unreleased]

### Added

- Placeholder for upcoming changes after `0.1.0`.

## [0.1.0] - 2026-02-06

### Added

- Unified public API surface around `bench.hh`, `Runner`, `measure`, and `Stats`.
- Host, WebAssembly, and STM32F4 target bindings under `platforms/`.
- Test suite covering semantics, numeric edge cases, and compile-fail API guard (`calibrate<0>()`).
- Structured documentation set (`docs/`) with English/Japanese variants.
- Doxygen configuration and generation flow (`xmake doxygen`).
- Doxygen GitHub workflow for artifact upload and Pages deployment.

### Changed

- Reorganized target layout to architecture-first structure (`platforms/arm/cortex-m/...`).
- Improved API and implementation comments for Doxygen output quality.
- Promoted `docs/INDEX.md` as canonical documentation entrypoint.

### Notes

- This is a beta release line intended for real-world validation before `1.0.0`.
