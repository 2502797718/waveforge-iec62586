# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.x | Yes |
| < 1.0 | No |

Security fixes are applied to the latest 1.x release line. Because WaveForge is a host-side
waveform generator with no network or device I/O, the most relevant issues are memory-safety
bugs in parsing or generation paths (JSON input, CLI arguments, builder calls, file output) and
integer or buffer handling in the writer.

## Reporting a vulnerability

**Do not open a public issue, discussion or pull request for a security problem.**

Report privately to the maintainers:

- Email: **[maintainer email]**
- Or use the repository's private security advisory feature, if enabled.

Please include:

1. The affected version (`wg_version_string()` or the manifest `generator_version`).
2. Build details: compiler, flags, platform, commit or release.
3. A minimal reproducer: command line or JSON input, and the expected versus observed behavior.
4. Any crash output, sanitizer report or proof-of-concept, if available.

## What to expect

- Acknowledgement within a reasonable time frame.
- An initial assessment of severity and reproducibility.
- Coordinated disclosure: we will agree on a timeline before any public write-up.
- Credit in the release notes if you wish to be named.

## Out of scope

- Claims that generated waveforms do not satisfy IEC 62586-2 or any other standard. WaveForge
  produces mathematical stimuli and does not issue conformity verdicts.
- Performance or accuracy of DACs, analog front ends, anti-aliasing filters, instrument
  transformers or real-time clocks.
- Issues that require modifying the standard text or reproducing copyrighted material.

Thank you for helping keep the project and its users safe.
