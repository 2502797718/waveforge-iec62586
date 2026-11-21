# WaveForge 使用手册

> 版本：1.2.0（`WaveForge 1.2.0`）
> 适用平台：Linux / WSL / macOS（POSIX 工具链）；Windows 原生构建可用，但自测依赖 POSIX 接口。
> 本文只讲**怎么用**；结构与契约见 [架构](architecture.md)、[输入格式](input-format.md)、[输出格式](output-format.md)。

## 1. WaveForge 是什么

WaveForge 是一个用 C99 编写的软件波形发生器：

- 输入：场景名 + 覆盖参数，或一份单层 JSON，或直接调用 C API；
- 输出：多通道波形文件（`WGIEC625` 格式）或逐帧回调流；
- 用途：为电能质量测量设备、算法与回放系统提供**可复现的测试激励**；
- 不负责：ADC/模拟前端/抗混叠/真实时钟，也不做 IEC 合格判定。

## 2. 安装与构建

### 2.1 依赖

| 依赖 | 说明 |
|---|---|
| C99 编译器 | gcc / clang / MSVC 均可；`make` 默认用 `cc` |
| GNU Make 或兼容 make | 也可用 `tools/build.sh` 包装 |
| Python 3.8+ | 仅独立校验工具需要，只用标准库 |
| `sha256sum` | 仅 `selftest` 的确定性用例需要（Linux/WSL/macOS 自带；macOS 可用 `shasum -a 256` 替换） |

运行时库只需要标准 C 库和 `libm`。

### 2.2 用 make 构建

```sh
cd apm900-waveform-sim-20261118
make                 # 等价于 make all
```

产物在 `build/`：

| 文件 | 说明 |
|---|---|
| `build/libwaveforge.a` | 静态库（Signal Core + Preset Layer） |
| `build/wg` | 命令行工具 |
| `build/selftest` | 16 项自测 |

其它目标：

```sh
make clean
make test            # 构建并运行自测；期望 SUMMARY: all tests PASSED
make install         # 安装到 PREFIX（默认 /usr/local）
```

`tools/build.sh` 会先清理再构建并跑自测：

```sh
sh tools/build.sh
```

> 下文命令直接写 `./build/wg`；`make install` 后也可以直接用 `wg`。
> 构建产物统一在 `build/`，并已被 `.gitignore` 排除；不要提交二进制。

### 2.3 用 CMake 构建

仓库根目录已提供 `CMakeLists.txt`：

```sh
cmake -S . -B build/cmake          # 与 make 的 build/ 分开
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

| 目标 | 产物 |
|---|---|
| `waveforge` | 静态库 `libwaveforge.a` |
| `wg` | 命令行工具 |
| `selftest` | 自测（已注册为 CTest 用例） |

### 2.4 示例

`examples/` 提供可直接构建的输入：

| 文件 | 内容 |
|---|---|
| `examples/basic.json` | 基础稳态配置 |
| `examples/harmonics.json` | 谐波配置 |
| `examples/multi_modulation.json` | 组合调制 |
| `examples/builder_example.c` | builder C API 示例 |

```sh
./build/wg gen --config examples/harmonics.json --out h.bin

cc -std=c99 -Iinclude examples/builder_example.c build/libwaveforge.a -lm \
   -o build/builder_example
./build/builder_example /tmp/builder_example.bin
```

### 2.5 在 Windows 上构建

- `wg` 本身只依赖 C 标准库，可在 MSVC/MinGW 下编译。
- `selftest` 依赖 `unistd.h`、`/proc/self/statm` 和 `sha256sum`，**建议在 WSL 或 Linux 下运行**。
- 若在原生 Windows 下使用，请把 `sha256sum` 换成可用的等价命令，或只运行 `wg` 与 Python 校验。

## 3. 快速开始

```sh
# 1. 构建
make

# 2. 看有哪些场景
./build/wg list

# 3. 一行生成：50.05 Hz 稳态，60 s
./build/wg gen --test A1.2.2 --system 50 --duration 60 --out a122.bin

# 4. 独立校验（自动寻找 a122.bin.manifest.json）
python3 tools/verify.py a122.bin --report a122.verify.json

# 5. 看 manifest
cat a122.bin.manifest.json
```

## 4. 场景清单 `wg list`

输出三列：

```text
A1.2.2        50/60  steady 50.05 Hz (60 Hz 59.95 Hz), 60 s
A4.1.3        50/60  threshold-margin step, no default delta (NO_DEFAULT)
A6.1.6        50/60  EXCLUDED: anti-alias proof belongs to analog front end
```

- 第 1 列：场景名（大小写不敏感）。
- 第 2 列：适用的系统频率。
- 第 3 列：说明；非 `DEFAULT` 场景会带 `CUSTOM:` / `NO_DEFAULT` / `EXCLUDED:` 前缀。

完整清单与状态见 [信号目录](signal-catalog.md)。

## 5. 一行生成

### 5.1 常用命令

```sh
# 频率量程下限/中点/上限
./build/wg gen --test A1.2.1 --out a121.bin
./build/wg gen --test A1.2.2 --out a122.bin
./build/wg gen --test A1.2.3 --out a123.bin

# 频率三角（42.5↔57.5 Hz，5 s 单程）
./build/wg gen --test A1.1.1 --duration 2min --out a111.bin

# 正弦调幅：2.3 Hz / 10 %
./build/wg gen --test A2.1.1 --out a211.bin

# 三次谐波 10 %
./build/wg gen --test A6.2.2 --duration 2 --out h3.bin

# 1060 Hz 载波 3 %
./build/wg gen --test A8.1.3 --duration 2 --out carrier.bin

# 三相依次暂降
./build/wg gen --test A4.2.1 --duration 15 --out a421.bin
```

### 5.2 覆盖预设默认值

只改想改的字段，其余仍取预设：

```sh
# 时长从 660 s 改为 30 s
./build/wg gen --test A6.5.1 --duration 30 --out h3ramp.bin

# 频率从 50.05 改为 49.95 Hz
./build/wg gen --test A1.2.2 --freq 49.95 --out a122_4995.bin

# 三相 80 %，相位 0/-120/+120
./build/wg gen --test A2.2.2 --amp Ua=80%,Ub=80%,Uc=80% \
  --phase 0,-120,120 --out a222.bin

# 追加 3 次 10 %/180° 与 1.5 次间谐波 5 %
./build/wg gen --test A1.2.2 --duration 2 \
  --harmonics "3=10%,180" --interharmonics "1.5=5%,0" --out mix.bin

# 叠加 2.3 Hz/10 % 正弦调幅
./build/wg gen --test A1.2.2 --duration 5 --mod sine:2.3:10:0 --out am.bin

# 在 5 s 处加 +30° 相位阶跃
./build/wg gen --test A1.2.2 --duration 10 --phase-step 5.0:30 --out step.bin
```

默认 CLI 软上限 7300 s，防止误输入写满磁盘。更长时长显式加 `--allow-long`，程序会按格式与通道数打印预估文件大小：

```sh
./build/wg gen --test A2.7.1 --duration 24h --allow-long --out a271_24h.bin
```

库与 JSON 的 `time.duration_s`、`segments[].duration_s` 不设上限，只要求有限正数；流式 `wg_run_stream()` 不落盘，内存恒定。

### 5.3 60 Hz 系统

```sh
./build/wg gen --test A1.2.2 --system 60 --out a122_60.bin
```

`--system 60` 会把预设里的 50 Hz 数值切换为 60 Hz 对应值（如 A1.2.2 的 59.95 Hz）。

## 6. JSON program（复杂波形）

当 CLI 选项不够用时（线性/三角调制、频率阶跃、幅值事件），写 JSON：

```json
{
  "time": { "duration_s": 10, "sample_rate_hz": 51200 },
  "channels": ["Ua", "Ub", "Uc"],
  "program": {
    "components": [
      {
        "kind": "fundamental",
        "frequency_hz": 50,
        "amp_pct": 100,
        "phase_deg": { "Ua": 0, "Ub": -120, "Uc": 120 },
        "modulators": [
          { "kind": "amp_sine", "frequency_hz": 2.3, "depth_pct": 10 },
          { "kind": "freq_triangle", "from": 40, "to": 60, "period_s": 6 }
        ]
      },
      { "kind": "harmonic", "order": 3, "amp_pct": 7, "phase_deg": 45 },
      { "kind": "interharmonic", "order": 1.5, "amp_pct": 5 },
      { "kind": "tone", "frequency_hz": 1060, "amp_pct": 1 }
    ],
    "events": [
      { "kind": "phase_step", "time_s": 5, "channel_mask": "Ua,Ub,Uc", "value": 30 }
    ]
  },
  "output": { "format": "f64", "frame_samples": 256 }
}
```

```sh
./build/wg gen --config my.json --out custom.bin
```

要点：

- `program.components` 非空时，旧 `signal.harmonics/interharmonics/tones/modulation` 被忽略。
- `frequency_hz` 对 `tone` 必填；基波/谐波/间谐波可省略，由 `order` 推导。
- `period_s` 是**完整往返周期**：40→60→40 Hz 用 `6`，单程 3 s。
- `phase_step` 加在积分相位上，谐波/间谐波按 `order` 缩放（H3 会得到 +90°），载波不变。
- 事件时间必须落在 `[0, duration_s]`。

更多字段与约束见 [输入格式](input-format.md)。

## 7. C API

### 7.1 头文件与链接

```c
#include "wg.h"          /* 公共 API */
#include "wg_config.h"   /* 可选：默认配置、JSON、场景 */
```

```sh
cc -std=c99 -O2 -Iinclude my_tool.c build/libwaveforge.a -lm -o my_tool
```

### 7.2 builder：显式构造 program

```c
#include <string.h>
#include "wg.h"

int main(void)
{
    const uint8_t ch[3] = { WG_CH_UA, WG_CH_UB, WG_CH_UC };
    const double  nom[3] = { 220.0, 220.0, 220.0 };
    wg_builder_t *b = wg_builder_create(51200u, 0u);
    wg_component_t c;
    wg_modulator_t m;
    wg_handle_t *h;
    char err[256];

    if (b == NULL) {
        return 1;
    }
    wg_builder_set_channels(b, ch, 3u, nom);
    wg_builder_set_duration(b, 10.0);
    wg_builder_set_output(b, (uint8_t)WG_FMT_F64, 256u);

    /* 基波 50 Hz / 100 %，三相 0/-120/+120° */
    memset(&c, 0, sizeof(c));
    c.kind = WG_COMP_FUNDAMENTAL;
    c.order = 1.0;
    c.frequency_hz = 50.0;
    c.channel_mask = 0x07u;
    c.amp_pct[WG_CH_UA] = c.amp_pct[WG_CH_UB] = c.amp_pct[WG_CH_UC] = 100.0;
    c.phase_deg[WG_CH_UA] = 0.0;
    c.phase_deg[WG_CH_UB] = -120.0;
    c.phase_deg[WG_CH_UC] = 120.0;
    wg_builder_add_component(b, &c);

    /* 3 次谐波 7 % / 45°，相对基波 */
    memset(&c, 0, sizeof(c));
    c.kind = WG_COMP_HARMONIC;
    c.order = 3.0;
    c.channel_mask = 0x07u;
    c.amplitude_ref = 1u;
    c.amp_pct[WG_CH_UA] = c.amp_pct[WG_CH_UB] = c.amp_pct[WG_CH_UC] = 7.0;
    c.phase_deg[WG_CH_UA] = c.phase_deg[WG_CH_UB] = c.phase_deg[WG_CH_UC] = 45.0;
    wg_builder_add_component(b, &c);

    /* 基波叠加 2.3 Hz / 10 % 正弦调幅 */
    memset(&m, 0, sizeof(m));
    m.kind = WG_MOD2_AMP_SINE;
    m.frequency_hz = 2.3;
    m.depth_pct = 10.0;
    wg_builder_add_modulator(b, 0u, &m);

    h = wg_create_from_builder(b, err, sizeof(err));
    wg_builder_destroy(b);                 /* 句柄创建后 builder 可销毁 */
    if (h == NULL) {
        return 1;
    }
    wg_run_file(h, "custom.bin");
    wg_destroy(h);
    return 0;
}
```

### 7.3 file 模式

```c
int32_t rc = wg_run_file(h, "out.bin");
```

- 成功返回 0；失败返回负值（-1 参数、-2 I/O、-3 内存、-4 配置）。
- 会覆盖同名文件；成功后再写 `out.bin.manifest.json`。
- 同一个句柄可以多次调用，每次从 t = 0 重新开始。

### 7.4 stream 模式

```c
#include <stdio.h>
#include "wg.h"

static int32_t on_frame(const wg_frame_t *f, void *ctx)
{
    (void)ctx;
    printf("frame %llu: %u samples/ch, t=%llu ns\n",
           (unsigned long long)f->frame_index,
           (unsigned)f->frames,
           (unsigned long long)f->start_utc_ns);
    /* f->samples 只在回调期间有效，不要保存指针 */
    return 0;   /* 非 0 会中止并作为 wg_run_stream 的返回值 */
}

int main(void)
{
    wg_config_t cfg;
    char err[256];
    wg_handle_t *h;

    wg_config_default(&cfg);
    cfg.duration_s = 3600.0;          /* 1 h，内存仍恒定 */
    cfg.frame_samples = 512u;
    cfg.start_utc_ns = 1794700800000000000ull;

    h = wg_create(&cfg, err, sizeof(err));
    if (h == NULL) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    wg_run_stream(h, on_frame, NULL);
    wg_destroy(h);
    return 0;
}
```

stream 不写文件、不写 manifest；帧内容与 file 模式逐字节一致。

### 7.5 使用 JSON/场景的 C 入口

```c
wg_config_t cfg;
char err[256];
wg_config_default(&cfg);
wg_scenario_apply("A1.2.2", 50, &cfg, err, sizeof(err));
cfg.duration_s = 30.0;
```

`wg_scenario_apply()` 返回 0 成功；返回 1 表示预设拒绝（`err` 为 `NO_DEFAULT:`/`CUSTOM:`/`EXCLUDED:`）。

## 8. 读取输出

### 8.1 先用 manifest 看参数

```sh
cat a122.bin.manifest.json
```

重点字段：`config.channels`、`config.nominal`、`config.format`、`start_utc_ns`、`output_sha256`。

### 8.2 文件结构

- 64 B 文件头（`WGIEC625`）
- 若干帧：56 B 帧头（`WGF1`）+ 平面样本
- 32 B 结束记录（`WGE1`）

字段偏移、i24 换算与时间戳公式见 [输出格式](output-format.md)。

### 8.3 最小读取流程

1. 读 64 B 头，校验 magic、`format`、`channel_count`。
2. 循环读 56 B 帧头，校验 `WGF1`，按 `payload_bytes` 读样本。
3. i24 用 `u_scale`/`i_scale` 还原；f64/f32 直接按小端解析。
4. 时间戳用 `wg_ns_at(start_sample, fs)` 复算，不要依赖累加。

## 9. 独立校验

两个工具都不链接发生器库，只按公开格式读取文件。

### 9.1 `verify.py`

```sh
python3 tools/verify.py a122.bin
python3 tools/verify.py a122.bin --report a122.verify.json
python3 tools/verify.py a122.bin --manifest a122.bin.manifest.json
python3 tools/verify.py a122.bin --expect my.expect.json
```

- 自动寻找 `<file>.manifest.json`、`<stem>.manifest.json` 或同目录 `manifest.json`。
- 检查结构、时间戳、稳态分量的 RMS/频率/相位；动态分量（含调制器/频率轮廓/事件）标 `DYNAMIC` 并跳过单值判据。
- 退出码：0 = PASS，1 = FAIL，2 = PARTIAL，3 = 用法/IO 错误。
- 没有 manifest 时仍做结构校验，但不做信号测量。

### 9.2 `check_acceptance.py`

```sh
# 通用测量
python3 tools/check_acceptance.py rms  a122.bin --channel Ua --window 0.2
python3 tools/check_acceptance.py freq a122.bin --channel Ua --window 1
python3 tools/check_acceptance.py dft  a122.bin --channel Ua --freq 150

# 场景化验收
python3 tools/check_acceptance.py a1-1-1 a111.bin
python3 tools/check_acceptance.py a2-5-1 a251.bin
python3 tools/check_acceptance.py a4-2-1 a421.bin
python3 tools/check_acceptance.py a5-1-2 a512.bin
python3 tools/check_acceptance.py a6-2-2 a622.bin
python3 tools/check_acceptance.py a6-5-1 a651.bin
python3 tools/check_acceptance.py a8-1-3 a813.bin
```

输出为 JSON：`checks` 是逐项判定，`values` 是原始测量值。退出码 0 = PASS，1 = FAIL。

### 9.3 selftest

```sh
make test
```

16 项覆盖：配置校验、时长上限、流计数/时间戳、file-vs-stream、确定性、相位连续、i24、660 s 长跑、builder 任意分量、逐分量包络、叠加调制、相位掩码、调制起始/保持、自定义三角、`NO_DEFAULT`、旧字段兼容。详见 [校验](verification.md)。

## 10. 常见场景做法

### 10.1 三相不平衡

```sh
./build/wg gen --test A5.1.2 --out a512.bin
```

或自定义：

```sh
./build/wg gen --test A5.1.1 --amp Ua=73%,Ub=80%,Uc=87% --out unbal.bin
```

### 10.2 分相暂降 + 过零切换

用 `segments`：

```json
{
  "time": { "duration_s": 1.0 },
  "segments": [
    { "duration_s": 0.2, "signal": { "amp_pct": 100 } },
    {
      "duration_s": 0.2,
      "transition": "zero_crossing",
      "channel_delay_s": { "Ua": 0, "Ub": 0.00333, "Uc": 0.00667 },
      "signal": { "amp_pct": 0 }
    },
    { "duration_s": 0.6, "signal": { "amp_pct": 100 } }
  ]
}
```

段时长之和必须等于 `time.duration_s`。

### 10.3 谐波幅值斜坡

```sh
./build/wg gen --test A6.5.1 --duration 30 --out h3ramp.bin
```

预设用 `amp_triangle` 把 H3 从 10 % 降到 0 再升回 10 %（1 %/s）。

### 10.4 长时输出

- 文件模式：注意磁盘占用（f64、3 通道、2 h 约 8.8 GB）。
- 流模式：常驻内存恒定，适合 2 h 以上；用 `wg_run_stream()`。

### 10.5 自定义采样率/格式

```sh
./build/wg gen --test A1.2.2 --rate 25600 --frame 512 --format i24 --out a122_i24.bin
```

i24 的换算比例在文件头 `u_scale`/`i_scale` 中，不要写死 220 V。

## 11. 故障排查

| 现象 / 报错 | 原因 | 处理 |
|---|---|---|
| `test: unknown scenario 'X'` | 场景名拼错 | 用 `wg list` 确认 |
| `NO_DEFAULT: ...` | 预设缺标准默认值 | 用 `--freq/--amp/--harmonics` 或 JSON `program` 补字段 |
| `CUSTOM: ...` | 该场景需要用户选择/给值 | 按提示补通道、参考通道或分量 |
| `EXCLUDED: ...` | 不属发生器职责 | 不要用数字注入代替硬件试验 |
| `duration_s: expected number >= 0.001` | 时长为 0、负或非有限 | 给有限正数 |
| `duration_s: ... exceeds 7300; pass --allow-long ...` | CLI 软上限 | 加 `--allow-long`，或缩短时长 |
| `sample_rate_hz: expected integer in [1000, 1000000]` | 采样率越界 | 改到 1 k–1 MHz |
| `frame_samples: expected integer in [1, 65535]` | 帧长越界 | 改到 1–65535 |
| `segments: expected total duration >= duration_s` | 段时长之和不足 | 让各段之和等于 `duration_s` |
| `segments: total duration exceeds duration_s` | 段时长之和超出 | 同上 |
| `mod: not supported with segments` | `--mod` 与分段同用 | 改用 JSON `program` 的调制器 |
| `program.components: expected at least one fundamental` | program 没有基波 | 加一个 `kind: "fundamental"` |
| `program.modulators: expected at most 1 FREQ_* entry` | 频率轮廓重复 | 只保留一条 `FREQ_*` |
| `program.components[0].order: expected integer 2..64` | 谐波次数非法 | 改用 2–64 的整数 |
| `stream mode: use C API` | CLI 不支持 `--stream` | 用 `wg_run_stream()` |
| `config: cannot open '...'` | JSON 路径错误 | 检查路径与权限 |
| `generate: failed to write '...'` | 输出路径不可写/磁盘满 | 换路径或清理空间 |
| 输出哈希每次不同 | 起始 UTC 默认当前整秒 | 用 `--start` 固定时间 |
| 校验报 `DYNAMIC`/`PARTIAL` | 含调制/斜坡/事件 | 属预期：动态分量不做稳态单值判定 |
| `selftest` 编译失败 | Windows 缺 POSIX 头 | 在 WSL/Linux 下构建 |

## 12. 使用边界

- WaveForge 的输出是**软件生成的数学波形**，不能证明 ADC、模拟前端、抗混叠、互感器、真实时钟或整机性能。
- 软件校验 PASS 不等于 IEC 型式试验或计量认证合格。
- 标准预设数值只是默认值；标准原文需自行获取。
- 不要把 `NO_DEFAULT` 当成“无法生成”：补齐字段即可。

更多问题见 [FAQ](faq.md)。
