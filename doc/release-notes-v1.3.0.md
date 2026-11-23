# WaveForge 1.3.0

Per-modulator channel selection.

## What changed

- **`channel_mask` / `channels` on modulators.** Any `AMP_*` modulator can now be limited
  to a subset of its component's channels. `0` (the default when omitted) keeps the previous
  behavior: all channels of the component.
- **Modulate current only, keep voltage at the test point.** One fundamental component can
  carry all six channels while the amplitude modulation applies only to `Ia/Ib/Ic`:

  ```json
  {
    "kind": "fundamental",
    "channels": ["Ua", "Ub", "Uc", "Ia", "Ib", "Ic"],
    "modulators": [
      {
        "kind": "amp_sine",
        "frequency_hz": 2.3, "depth_pct": 10,
        "start_s": 10,
        "channels": ["Ia", "Ib", "Ic"]
      }
    ]
  }
  ```

  This is the shape required by the IEC 62586-2 Annex F current tests (A14.1.1) and the
  10 min current aggregation ramp (A14.6.1), without falling back to a `tone` component.

- **CLI support.** `--mod` accepts a trailing channel list:
  `--mod sine:2.3:10:0:channels=Ia,Ib,Ic`.
- **Validation.** The modulator mask must be a subset of its component mask, otherwise the
  error is `components[i].modulators[j].channels: channel not enabled by component`.
  `FREQ_*` is a global frequency profile and rejects a mask with
  `FREQ_* is global; channel selection not allowed`.
- **Manifest.** `channel_mask` is recorded for channel-scoped modulators and omitted when it
  is `0`, so manifests from earlier versions stay byte-identical.

## Verification

- 18/18 self-tests, including:
  - `mod-channel-mask`: six channels, `amp_const` 50 % on `Ia/Ib/Ic` only — voltages stay at
    220 V, currents at 2.5 A; with an `Ia`-only mask, `Ib/Ic` stay at 5.0 A.
  - `mod-mask-invalid`: out-of-range masks and invalid JSON are rejected with explicit paths.
- Independent `verify.py` run on a channel-scoped file: voltages remain 220.000 V in both
  windows; currents drop to the modulated level.
- CMake/CTest and the GCC/Clang CI matrix pass.

## Compatibility

- Configs without modulator `channels` behave exactly as before, including byte-identical
  output for existing scenarios.
- `wg_modulator_t` gains one trailing field (`uint8_t channel_mask`), so existing field
  offsets are unchanged; rebuild against 1.3.0.

---

# WaveForge 1.3.0（中文）

调制器级通道选择。

## 变更

- **调制器支持 `channel_mask` / `channels`**：任意 `AMP_*` 调制器都可以只作用于所属分量的部分通道。省略时为 `0`，保持旧行为“作用于本分量全部通道”。
- **只调制电流、电压保持在测试点**：一个 fundamental 分量覆盖六通道，调幅只落在 `Ia/Ib/Ic`：

  ```json
  {
    "kind": "fundamental",
    "channels": ["Ua", "Ub", "Uc", "Ia", "Ib", "Ic"],
    "modulators": [
      {
        "kind": "amp_sine",
        "frequency_hz": 2.3, "depth_pct": 10,
        "start_s": 10,
        "channels": ["Ia", "Ib", "Ic"]
      }
    ]
  }
  ```

  这正是 IEC 62586-2 Annex F 电流测试（A14.1.1）与 10 min 电流聚合斜坡（A14.6.1）所需的结构，不必再用 `tone` 分量绕行。

- **CLI 支持**：`--mod` 可带尾部通道列表，如 `--mod sine:2.3:10:0:channels=Ia,Ib,Ic`。
- **校验**：调制器掩码必须是所属分量掩码的子集，否则报 `components[i].modulators[j].channels: channel not enabled by component`。`FREQ_*` 是全局频率轮廓，带掩码会报 `FREQ_* is global; channel selection not allowed`。
- **manifest**：通道限定的调制器记录 `channel_mask`；掩码为 `0` 时不输出，因此旧配置的 manifest 逐字节不变。

## 验证

- 自测 18/18，包括：
  - `mod-channel-mask`：六通道，`amp_const` 50 % 只作用于 `Ia/Ib/Ic` —— 电压保持 220 V，电流 2.5 A；掩码仅 `Ia` 时，`Ib/Ic` 保持 5.0 A。
  - `mod-mask-invalid`：越界掩码与非法 JSON 均被拒绝，并给出明确字段路径。
- 对通道限定文件运行独立 `verify.py`：两个窗口内电压均为 220.000 V，电流按调制下降。
- CMake/CTest 与 GCC/Clang CI 矩阵通过。

## 兼容性

- 未配置调制器 `channels` 的配置行为完全不变，既有场景输出逐字节一致。
- `wg_modulator_t` 末尾新增一个字段（`uint8_t channel_mask`），既有字段偏移不变；需针对 1.3.0 重新编译。
