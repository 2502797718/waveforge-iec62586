# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned

- Re-verify the RVC preset defaults (A13.3.1–A13.5.2) against the source tables.
- Add 6.14–6.17 current scenarios to `wg list`.
- Golden-file fixtures for f32 and i24 output.
- Python bindings over the stream API.

## [1.0.1] - 2026-11-21

### Changed

- Raise the maximum `time.duration_s`, `segments[].duration_s` and `--duration` from
  7200 s to 7300 s, so a 2 h aggregation block can include a 10 s window-closure
  margin (`7210 s`). The limit is centralized as `WG_MAX_DURATION_S`
  (CR-WAVEFORGE-001).
- Document the `7210 s` recipe for A2.7.1 in the input format, signal catalog and
  standard mapping.

### Fixed

- Make the self-test compile cleanly with Clang `-Werror` by initializing the
  measurement variables used by the arbitrary-component and per-component
  envelope checks.

## [1.0.0] - 2026-11-19

Initial open-source release.

### Added

- **Two-layer architecture.** A Signal Core (`src/wg_program.c`, `src/wg_sampler.c`) that evaluates
  a `wg_program_t`, and a Preset Layer (`src/wg_scenario.c`) that only fills defaults.
- **Component model.** Fundamental, harmonics 2–64, fractional interharmonics, and absolute carrier
  tones; up to 96 components and 8 modulators per component.
- **Modulation.** `amp_const`, `amp_linear`, `amp_triangle`, `amp_step`, `amp_sine`, `amp_rect`,
  `freq_linear`, `freq_triangle`, `freq_step`; amplitude factors multiply, one `FREQ_*` per program.
- **Events and segments.** `amp_step` and `phase_step` at absolute time with channel masks; one
  level of segments with `immediate` or `zero_crossing` switching and per-channel delay.
- **Presets.** 101 IEC 62586-2 scenario names over clauses 6.1–6.17 plus 3 frequency aliases;
  81 `DEFAULT`, 4 `CUSTOM`, 15 `NO_DEFAULT`, 1 `EXCLUDED`. All preset values are overridable.
- **CLI.** `wg gen` and `wg list`; overrides for test, system frequency, duration, frequency,
  amplitude, phase, harmonics, interharmonics, tones, sine/rectangular AM, phase steps, sample
  rate, frame length, format, start UTC and output path.
- **JSON input.** Single-level configuration with `time`, `channels`, `signal`, `segments`,
  `program` and `output`; unknown fields are rejected.
- **Output.** `WGIEC625` container with 64 B header, 56 B frame headers and 32 B end record;
  f64 / f32 / i24 planar samples; `<output>.manifest.json` with the effective configuration and
  the waveform SHA-256.
- **Streaming.** `wg_run_stream()` frame callbacks with constant memory; frames are byte-identical
  to file output.
- **C API.** `wg_builder_*` heap builder, `wg_create_from_builder()`, `wg_run_file()`,
  `wg_run_stream()`, `wg_version_string()`.
- **Determinism.** `double` phase integration, integer-nanosecond timestamps recomputed from the
  sample index, no random seeds. Same build + config + start time yields the same SHA-256.
- **Verification.** 13 self-tests, 6 regression scenarios, and independent `tools/verify.py` and
  `tools/check_acceptance.py` that never link the generator library.
- **Build systems.** GNU Make and CMake ≥ 3.16 with CTest registration, plus a GitHub Actions
  workflow for Linux.
- **Documentation.** Architecture, input/output formats, signal catalog, standard mapping, user
  manual (Chinese and English), verification guide, FAQ and project log.

### Notes

- WaveForge is not an IEC certification or type-test tool. It does not judge conformity and makes
  no claims about analog hardware or real-time clocks.
- No standard text, table or scan is reproduced. Preset numbers are this project's defaults.
