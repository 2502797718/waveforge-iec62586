# WaveForge 1.2.0

Scheduled modulation and per-channel phase steps.

## What changed

- **`start_s` on more modulators.** `amp_triangle`, `amp_sine`, `amp_rect` and
  `freq_triangle` can now begin at an absolute time instead of sample 0.
- **`hold` for amplitude triangle.** Before `start_s`, the triangle holds a configurable
  amplitude percentage (default `0`). Example:

  ```json
  {
    "kind": "amp_triangle",
    "from": 10, "to": 90, "period_s": 1000,
    "start_s": 1.0, "hold": 50
  }
  ```

  This gives 50 % amplitude (110 V RMS at 220 V nominal) before 1 s, then a slow
  triangle starting at 10 %.

- **Per-channel `phase_step`.** The event now updates only the channels selected by
  `channel_mask`; each channel keeps its own offset. A step on Ua no longer shifts Ub/Uc.
- **Manifest completeness.** `start_s` and `hold` are written to the manifest so a run
  can be reproduced from the recorded configuration.

## Verification

- 16/16 self-tests, including:
  - `phase-mask`: 3 channels, Ua +30°, Ub/Uc 0°.
  - `mod-start-hold`: 110.0 V before `start_s`, 22.1 V after.
- CMake/CTest and the GCC/Clang CI matrix pass.

---

# WaveForge 1.2.0（中文）

调制起始时刻与逐通道相位阶跃。

## 变更

- **更多调制器支持 `start_s`**：`amp_triangle`、`amp_sine`、`amp_rect`、`freq_triangle`
  可从绝对时刻开始，而不是从样本 0 开始。
- **幅值三角增加 `hold`**：`start_s` 前保持指定幅值百分比（默认 `0`）。例如：

  ```json
  {
    "kind": "amp_triangle",
    "from": 10, "to": 90, "period_s": 1000,
    "start_s": 1.0, "hold": 50
  }
  ```

  1 s 前为 50 % 幅值（220 V 基准下 110 V RMS），之后从 10 % 开始缓慢三角。

- **`phase_step` 改为逐通道**：只修改 `channel_mask` 选中的通道，各通道偏移独立；
  对 Ua 的阶跃不再影响 Ub/Uc。
- **manifest 完整记录**：`start_s`、`hold` 写入 manifest，便于复现。

## 验证

- 自测 16/16，包括：
  - `phase-mask`：3 通道，Ua +30°，Ub/Uc 0°。
  - `mod-start-hold`：起始前 110.0 V，起始后 22.1 V。
- CMake/CTest 与 GCC/Clang CI 矩阵通过。
