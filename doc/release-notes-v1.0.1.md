# WaveForge 1.0.1

Patch release for CR-WAVEFORGE-001.

## What changed

- The maximum generation duration is now **7300 s** for all three entry points:
  `time.duration_s`, `segments[].duration_s` and `--duration`.
- The limit is defined once as `WG_MAX_DURATION_S` in `include/wg_types.h`; no more
  drift between the JSON validator and the CLI.
- A 2 h aggregation block that needs an extra 10 s for the final 10 min window to
  close can now use `7210 s` directly:

  ```sh
  ./build/wg gen --test A2.7.1 --duration 7210 --out a271.bin
  ```

- The self-test now includes a `duration-limit` case: `7210` is accepted and
  `7300.001` is rejected.
- Clang `-Werror` build is clean (measurement variables initialized).

## Compatibility

- Configurations with `duration_s <= 7200` behave exactly as before.
- JSON schema is unchanged.
- Streaming memory stays constant; CPU time grows linearly with duration.

## Verification

- `make clean && make all selftest`: 14/14 PASS.
- CMake/CTest: pass.
- GitHub Actions: GCC and Clang × CMake and Make, all green.

---

# WaveForge 1.0.1（中文）

CR-WAVEFORGE-001 补丁版本。

## 变更

- `time.duration_s`、`segments[].duration_s`、`--duration` 三处上限统一提升到 **7300 s**。
- 上限集中定义为 `include/wg_types.h` 的 `WG_MAX_DURATION_S`，避免 JSON 校验与 CLI 漂移。
- 2 h 聚合块需要额外 10 s 让最后一个 10 min 窗闭合时，可直接使用 `7210 s`：

  ```sh
  ./build/wg gen --test A2.7.1 --duration 7210 --out a271.bin
  ```

- 自测新增 `duration-limit`：接受 `7210`，拒绝 `7300.001`。
- Clang `-Werror` 构建清零告警。

## 兼容性

- `duration_s <= 7200` 的配置行为完全不变。
- JSON schema 不变。
- 流式生成内存恒定，CPU 时间随时长线性增长。

## 验证

- `make clean && make all selftest`：14/14 PASS。
- CMake/CTest：通过。
- GitHub Actions：GCC/Clang × CMake/Make 全部通过。
