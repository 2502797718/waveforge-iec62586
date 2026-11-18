# WaveForge 1.0.0

**Deterministic, composable IEC 62586 waveform generation in C99.**

WaveForge turns a continuous-time signal model into discrete, timestamped multi-channel
waveforms. It is a host-side engine, not a certification tool: it produces the ideal stimulus a
power-quality instrument is expected to receive, and it never claims IEC conformity.

## Highlights

- **Two-layer architecture.** A signal core built from components, modulators and events; an IEC
  62586 preset layer that only fills defaults.
- **Arbitrary composition.** Up to 96 components — fundamental, harmonics 2–64, fractional
  interharmonics (1.5, 7.5, 49.5), absolute carrier tones — each with up to 8 stackable amplitude or
  frequency modulators.
- **Presets are defaults, not locks.** Every standard value can be overridden by the CLI, JSON or
  the C API. Missing standard values return `NO_DEFAULT` with the exact field path instead of
  guessing.
- **One core, two sinks.** `wg_run_file()` writes the `WGIEC625` container plus a manifest;
  `wg_run_stream()` delivers identical frames through a callback with constant memory.
- **Pure math, reproducible.** C99 + `libm`, double phase integration, integer-nanosecond
  timestamps. Same build + config + start time ⇒ byte-identical output and the same SHA-256.
- **Zero dependencies.** No third-party libraries. Python 3.8+ is needed only for the independent
  verifier, and it uses the standard library alone.

## Quick start

```sh
git clone https://github.com/2502797718/waveforge-iec62586.git
cd waveforge-iec62586
make
./build/wg list
./build/wg gen --test A1.2.2 --system 50 --duration 60 --out a122.bin
python3 tools/verify.py a122.bin --report a122.verify.json
```

CMake is also supported:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

## Standards coverage

- IEC 62586-2 clauses 6.1–6.13 plus current-channel recipes for 6.14–6.17.
- 101 scenario names + 3 aliases: **81 DEFAULT, 4 CUSTOM, 15 NO_DEFAULT, 1 EXCLUDED**.
- No standard text or scanned pages are redistributed. Standard values are defaults; obtain the
  official documents from IEC or an authorized channel.

## Verification

- 13 self-tests: phase integration, arbitrary components, stacked modulation, per-component
  envelopes, custom frequency triangle, `NO_DEFAULT`, old-config compatibility, file/stream
  identity, 11-minute streaming, determinism, resource bounds.
- 6 regression scenarios: A1.1.1, A2.5.1, A4.2.1, A5.1.2, A6.2.2, A8.1.3.
- CI matrix: GCC and Clang, with both CMake/CTest and Make/self-test.
- Acceptance is **math-only**: samples are reconstructed from the published format and manifest;
  IEC measurement uncertainty, hardware accuracy and conformity are out of scope.

## API in one glance

```c
wg_builder_t *b = wg_builder_create(51200, start_utc_ns);
/* add components, modulators and events */
wg_handle_t *h = wg_create_from_builder(b, err, sizeof(err));
wg_run_file(h, "wave.bin");          /* or wg_run_stream(h, emit, ctx) */
wg_destroy(h);
wg_builder_destroy(b);
```

## Documentation

- [Architecture](doc/architecture.md)
- [Input format](doc/input-format.md)
- [Output format](doc/output-format.md)
- [Signal catalog](doc/signal-catalog.md)
- [User manual (中文)](doc/user-manual.md) · [English](doc/user-manual.en.md)
- [Verification](doc/verification.md)
- [FAQ](doc/faq.md)

## Known boundaries

- WaveForge generates ideal samples. It does not model ADC, analog front ends, anti-aliasing,
  clocks, amplifiers or hardware calibration.
- It does not decide IEC conformity or replace a type test.
- `NO_DEFAULT` items need user-supplied values or device declarations (for example, threshold
  margins, 41–50th harmonic levels, interharmonic compatibility curves, RVC table values).
- A global phase step currently scales with harmonic order (fundamental +30° ⇒ H3 +90°,
  interharmonic +45°); tone frequencies are independent. This is documented as an open semantic.

## License

MIT. See [LICENSE](../LICENSE).

---

# WaveForge 1.0.0（中文）

**用 C99 组合并确定性生成 IEC 62586 波形。**

WaveForge 把连续时间信号模型离散化为带绝对时间戳的多通道波形。它是主机侧波形引擎，不是认证
工具：只产生电能质量仪器“应该收到”的理想激励，不宣称 IEC 合格。

## 主要特性

- **两层架构**：底层由分量、调制器和事件组成；上层 IEC 62586 预设只填默认值。
- **任意组合**：最多 96 个分量（基波、2–64 次谐波、1.5/7.5/49.5 等小数次间谐波、任意载波频点），
  每个分量最多 8 条可叠加的幅值/频率调制器。
- **预设只是默认值**：任何标准数值都可由 CLI、JSON 或 C API 覆盖；标准缺值返回 `NO_DEFAULT`
  与精确字段路径，不猜测。
- **单一核心、两种输出**：文件模式生成 `WGIEC625` 容器与 manifest；流模式逐帧回调、内存恒定。
- **纯数学、可复现**：C99 + `libm`、double 相位积分、整数纳秒时间戳；相同构建 + 配置 + 起始时间
  得到逐字节一致输出与相同 SHA-256。
- **零依赖**：无第三方库；Python 3.8+ 仅用于独立校验器。

## 快速开始

```sh
git clone https://github.com/2502797718/waveforge-iec62586.git
cd waveforge-iec62586
make
./build/wg list
./build/wg gen --test A1.2.2 --system 50 --duration 60 --out a122.bin
python3 tools/verify.py a122.bin --report a122.verify.json
```

## 验证与边界

- 13 项自测、6 个回归场景、GCC/Clang 双编译器 CI。
- 验收只做数学重建与结构校验；不判定 IEC 测量不确定度、硬件精度或标准符合性。
- 标准原文不随仓库分发；标准数值只是默认值，请从 IEC 或授权渠道获取正式文本。

## 许可证

MIT，见 [LICENSE](../LICENSE)。
