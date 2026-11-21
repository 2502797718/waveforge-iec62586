# Contributing to WaveForge

Thanks for your interest. This file is intentionally short: build it, test it, keep the diff
focused, and describe what changed.

## Build and test

```sh
make                 # build/libwaveforge.a, build/wg, build/selftest
make test            # expect "SUMMARY: all tests PASSED"
```

The CMake path must stay green as well:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

`build/` is gitignored. Never commit binaries, waveforms or generated manifests.

## Code style

- **C99**, no compiler extensions; `-std=c99 -pedantic` must pass.
- **4 spaces**, no tabs; LF line endings; final newline.
- **Allman braces**; 100-column soft limit. The repository ships `.clang-format` (LLVM base) and
  `.editorconfig`; run `clang-format -i` on touched files when available.
- **Zero warnings** under `-Wall -Wextra -Werror`.
- Keep the two-layer split: the preset layer may only fill defaults through
  `wg_program_add_*`; it must not sample or write files.
- Do not add dependencies beyond the C standard library and `libm`.

## Tests and evidence

- Any change to the signal model, phase integration, file format, config parsing or scenario table
  must keep `make test` at 14/14 and re-run the six regression scenarios listed in
  [doc/verification.md](doc/verification.md).
- New behavior needs a self-test or an example that demonstrates it.
- Independent verification (`tools/verify.py`, `tools/check_acceptance.py`) must not link the
  generator library; it reads only the published format.

## Commits and pull requests

- One logical change per commit. Write imperative subjects, e.g. `sampler: keep phase continuous
  across segments`.
- Describe **what** changed and **why**, list the commands you ran, and note any behavior change
  visible to users of the CLI, JSON contract or C API.
- Update `CHANGELOG.md` under `[Unreleased]` for user-visible changes.
- Pull requests should be focused. Refactors unrelated to the fix belong in a separate PR.
- By submitting a pull request you agree to license your contribution under the MIT License.

## Documentation and examples

- Documentation lives in `doc/` and must match the actual CLI/API. If a flag, default value or
  error message changes, update the relevant document in the same PR.
- Do not copy standard text, tables or scans into the repository. Refer to clause numbers and to
  our own default values only.
- Examples under `examples/` must remain runnable and must not assume a private path or device.

## Reporting bugs and security issues

- Functional bugs: use the issue template and include the exact command, config, and the
  manifest or `wg_version_string()` information.
- Security issues: **do not open a public issue**; follow [SECURITY.md](SECURITY.md).

---

# 参与贡献（中文摘要）

感谢参与。构建、测试、保持改动聚焦，并说清楚改了什么即可。

- **构建/测试**：`make` 后运行 `make test`，期望 `SUMMARY: all tests PASSED`；CMake 路径也须通过
  `ctest`。
- **代码风格**：C99、4 空格、Allman 花括号、LF、行宽 100；`-Wall -Wextra -Werror` 下零警告；
  可运行仓库自带的 `.clang-format`。
- **分层约束**：预设层只通过 `wg_program_add_*` 填默认值，不采样、不写文件；不引入标准库与
  `libm` 之外的依赖。
- **测试**：改动信号模型、相位积分、文件格式、配置解析或场景表时，必须保持 14 项自测通过，
  并重跑 [校验文档](doc/verification.md) 中的 6 个回归场景。
- **提交/PR**：一个提交只做一件事；说明改了什么、为什么、跑了哪些命令；用户可见变更写入
  `CHANGELOG.md` 的 `[Unreleased]`。
- **文档/示例**：`doc/` 必须与真实 CLI/API 一致；不复制标准正文、整表或扫描件；示例必须可运行。
- **安全**：不要公开提 issue，按 [SECURITY.md](SECURITY.md) 私下报告。
