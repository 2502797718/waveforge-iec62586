# WaveForge 1.1.0

Long-duration generation is now limited by physics, not by an arbitrary constant.

## What changed

- **Library and JSON have no duration upper bound.** `time.duration_s` and
  `segments[].duration_s` accept any finite positive number. The existing sample-count
  guard (`duration × sample rate ≤ 9e18`) still protects integer arithmetic.
- **The CLI keeps a 7300 s soft limit.** Pass `--allow-long` to exceed it. The tool
  prints an estimated output size so a 24 h run is intentional:

  ```sh
  ./build/wg gen --test A2.7.1 --duration 24h --allow-long --out a271_24h.bin
  ```

- **Non-finite input is rejected.** JSON `Infinity`/`NaN` and CLI `inf`/`nan` are no
  longer accepted.
- **Streaming remains constant-memory.** `wg_run_stream()` does not depend on duration;
  only file output grows linearly.

## Why a CLI guard

7 channels of f64 at 51200 Hz is about **2.87 MB/s**, or **248 GB per 24 h**. A typo such
as `--duration 72100` would otherwise fill a disk before the run finishes. The library
stays unrestricted for embedded and streaming users; the CLI asks for explicit consent.

## Verification

- `make clean && make all selftest`: 14/14 PASS, including `duration-limit`
  (`86400` accepted, `0` rejected).
- CLI: `--duration 86400` is rejected without `--allow-long`, accepted with it;
  the estimated size is printed.
- CMake/CTest and GCC/Clang CI matrix pass.

---

# WaveForge 1.1.0（中文）

长时生成不再受任意常数限制。

## 变更

- **库与 JSON 取消时长上限**：`time.duration_s`、`segments[].duration_s` 接受任意有限正数。
  原有 `duration × 采样率 ≤ 9e18` 的样本数保护继续生效。
- **CLI 保留 7300 s 软上限**：超过时须加 `--allow-long`，程序会打印预估文件大小：

  ```sh
  ./build/wg gen --test A2.7.1 --duration 24h --allow-long --out a271_24h.bin
  ```

- **拒绝非有限输入**：JSON 的 `Infinity`/`NaN`、CLI 的 `inf`/`nan` 不再被接受。
- **流式输出内存恒定**：`wg_run_stream()` 不随时长增长；只有文件输出线性增长。

## 为什么 CLI 保留保护

7 通道 f64、51200 Hz 约 **2.87 MB/s**，24 h 约 **248 GB**。`--duration 72100` 这类误输入
可能在跑完前写满磁盘。库保持自由，CLI 要求显式确认。

## 验证

- `make clean && make all selftest`：14/14 PASS，含 `duration-limit`
  （接受 `86400`，拒绝 `0`）。
- CLI：`--duration 86400` 无 `--allow-long` 被拒，有则通过并打印预估大小。
- CMake/CTest 与 GCC/Clang CI 矩阵通过。
