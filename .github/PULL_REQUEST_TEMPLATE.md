## Summary

Describe the change and the problem it solves.

## Type of change

- [ ] Bug fix
- [ ] New feature (signal core, preset, CLI, JSON, C API)
- [ ] Output format or streaming change
- [ ] Documentation / examples
- [ ] Build system / CI
- [ ] Refactor with no behavior change

## How it was tested

List the exact commands you ran.

```sh
make
make test
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

If the change affects the signal model, file format, config parsing or scenario table, also run the
six regression scenarios from `doc/verification.md`.

## Checklist

- [ ] `make test` passes (`SUMMARY: all tests PASSED`).
- [ ] CMake build and CTest pass.
- [ ] No new warnings under `-Wall -Wextra -Werror -pedantic`.
- [ ] Code follows the repository style (C99, 4 spaces, Allman braces, LF).
- [ ] Documentation in `doc/` matches the actual CLI/API.
- [ ] `CHANGELOG.md` updated under `[Unreleased]` for user-visible changes.
- [ ] No standard text, tables or scans are reproduced.
- [ ] No binaries, waveform files or manifests are committed.

## Related issues

Closes #
