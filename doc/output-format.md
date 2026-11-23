# 输出格式

> WaveForge 的波形文件扩展名由用户决定（示例用 `.bin`）。所有多字节整数与浮点均为**小端**；
> 浮点为 IEEE 754 binary64 / binary32。帧之间无对齐填充。
> 相关文档：[输入格式](input-format.md) · [架构](architecture.md) · [校验口径](verification.md)

## 1. 文件总体布局

```text
+----------------------------------+
| 文件头 64 B（magic "WGIEC625"）   |
+----------------------------------+
| 帧 0：帧头 56 B + 样本 payload    |
| 帧 1：帧头 56 B + 样本 payload    |
| ...                              |
+----------------------------------+
| 结束记录 32 B（magic "WGE1"）     |
+----------------------------------+
```

- 每帧 payload 长度 = `frames × channel_count × 每点字节数`。
- 末帧 `frames` 可以小于文件头里的 `frame_samples`。
- 同目录另写 `<输出文件名>.manifest.json`；manifest 不属于波形文件，也**不参与** SHA-256。

## 2. 文件头（64 字节）

| 偏移 | 长度 | 字段 | 值 / 含义 |
|---|---|---|---|
| 0 | 8 | `magic` | ASCII `WGIEC625`，无结尾 NUL |
| 8 | 2 | `version` | 1 |
| 10 | 2 | `header_bytes` | 64 |
| 12 | 4 | `sample_rate_hz` | 采样率 |
| 16 | 2 | `channel_count` | 1–7 |
| 18 | 2 | `frame_samples` | 常规帧点数 |
| 20 | 2 | `format` | 0 = f64，1 = f32，2 = i24 |
| 22 | 2 | `flags` | 当前恒为 0，保留 |
| 24 | 8 | `start_utc_ns` | 首帧首样本的 UTC 纳秒 |
| 32 | 8 | `sample_period_ns` | double，`1e9 / sample_rate_hz` |
| 40 | 8 | `u_scale` | double，i24 电压通道 V/LSB；非 i24 为 0 |
| 48 | 8 | `i_scale` | double，i24 电流通道 A/LSB；非 i24 为 0 |
| 56 | 8 | `reserved` | 全 0 |

## 3. 帧头（56 字节）

| 偏移 | 长度 | 字段 | 值 / 含义 |
|---|---|---|---|
| 0 | 4 | `magic` | ASCII `WGF1` |
| 4 | 2 | `version` | 1 |
| 6 | 2 | `header_bytes` | 56 |
| 8 | 8 | `frame_index` | 从 0 递增 |
| 16 | 8 | `start_sample` | 帧首样本的全局序号 |
| 24 | 8 | `start_utc_ns` | 帧首样本 UTC 纳秒 |
| 32 | 8 | `end_utc_ns` | 帧末样本**之后沿**的 UTC 纳秒 |
| 40 | 4 | `frames` | 本帧点数 |
| 44 | 4 | `flags` | bit0 断点，bit1 首帧，bit2 末帧 |
| 48 | 2 | `channel_count` | 通道数 |
| 50 | 2 | `reserved` | 0 |
| 52 | 4 | `payload_bytes` | 本帧样本字节数 |

| 位 | 宏 | 含义 | 当前行为 |
|---|---|---|---|
| 0 | `WG_FRAME_FLAG_BREAK` | 本帧前存在时间/数据缺口 | 头文件保留；当前生成器不置位 |
| 1 | `WG_FRAME_FLAG_FIRST` | 文件/流首帧 | 首帧置位 |
| 2 | `WG_FRAME_FLAG_LAST` | 文件/流末帧 | 末帧置位 |

## 4. 结束记录（32 字节）

| 偏移 | 长度 | 字段 | 值 / 含义 |
|---|---|---|---|
| 0 | 4 | `magic` | ASCII `WGE1` |
| 4 | 2 | `version` | 1 |
| 6 | 2 | `reason` | 0 = 正常完成，1 = 错误中止；2 保留（调用方中止） |
| 8 | 8 | `total_frames` | 已写帧数 |
| 16 | 8 | `total_samples` | 每通道总样本数 |
| 24 | 8 | `reserved` | 全 0 |

## 5. 样本编码

### 5.1 平面排列

每帧内按通道平面排列：

```text
[ch0 第 0..N-1 点][ch1 第 0..N-1 点] ... [ch(C-1) 第 0..N-1 点]
```

通道顺序由配置 `channels[]` 决定；索引即文件内顺序。默认是 `Ua, Ub, Uc`。

### 5.2 f64 / f32

样本为工程单位瞬时值：电压 V、电流 A。

- `amp_pct = 100` 时，基波 RMS = 通道 `nominal`，峰值 = `nominal × √2`。
- f32 由 double 直接转换为 float，存在 24 位尾数精度损失。

### 5.3 i24

- 每点 3 字节，小端补码，范围 −8 388 608 … 8 388 607。
- 电压通道比例：

```text
u_scale = max(nominal_V among enabled Ua/Ub/Uc) × √2 / 8388607   [V/LSB]
code    = clamp(round(x / u_scale), -8388608, 8388607)
```

- 电流通道比例同理，写入 `i_scale`（A/LSB）。
- 读取方还原：`x = code × scale`。`scale` 是唯一权威换算，不要假设固定 220 V 或 5 A。
- 不同通道 `nominal` 不同时，文件头记录的是该类别（电压/电流）中的**最大** `nominal` 对应比例，避免削顶；逐通道 `nominal` 在 manifest 中。
- 非 i24 格式的 `u_scale`/`i_scale` 为 0。

## 6. 时间戳

样本 `n` 的 UTC 偏移（整数纳秒，纯整数运算）：

```c
static uint64_t wg_ns_at(uint64_t n, uint32_t fs)
{
    uint64_t q = n / fs;
    uint64_t r = n % fs;
    return q * 1000000000ull + (r * 1000000000ull + fs / 2u) / fs;
}
```

- 舍入方式为四舍五入（half up），因为 `r × 1e9` 恒非负。
- `start_utc_ns = 文件头 start_utc_ns + wg_ns_at(start_sample, fs)`。
- `end_utc_ns = 文件头 start_utc_ns + wg_ns_at(start_sample + frames, fs)`。
- `end_utc_ns` 是帧末样本的**后沿**，不落在任何样本上；相邻帧满足
  `帧 k.end_utc_ns == 帧 k+1.start_utc_ns`。
- 不使用浮点 `n × 1e9 / fs`，避免累积误差。
- 总样本数 = `llround(duration_s × sample_rate_hz)`，且至少为 1。

## 7. stream 模式

`wg_run_stream()` 通过 `wg_frame_t` 交付与文件帧等价的内容：

```c
typedef struct {
    uint64_t frame_index;
    uint64_t start_sample;
    uint64_t start_utc_ns;
    uint64_t end_utc_ns;
    uint32_t frames;
    uint32_t flags;
    uint16_t channel_count;
    uint16_t reserved;
    const void *samples;
} wg_frame_t;
```

| 文件字段 | `wg_frame_t` 成员 |
|---|---|
| `frame_index` | `frame_index` |
| `start_sample` | `start_sample` |
| `start_utc_ns` | `start_utc_ns` |
| `end_utc_ns` | `end_utc_ns` |
| `frames` | `frames` |
| `flags` | `flags` |
| `channel_count` | `channel_count` |
| `payload_bytes` | `frames × channel_count × 每点字节` |
| 样本 payload | `samples`（平面排列，格式由配置 `format` 决定） |

- `samples` 指向内部帧缓冲，**仅在回调期间有效**；回调返回后不得保存指针。
- 不写文件头、帧头、结束记录，也不写 manifest。
- 内存占用恒定，不随总时长增长；适合 ≥2 h 的长时输出。
- 回调返回非 0 会立即中止生成，并把该值作为 `wg_run_stream` 的返回值。

## 8. manifest

文件名固定为 **`<输出文件名>.manifest.json`**，例如 `a122.bin.manifest.json`。
只在 `wg_run_file()` 成功写完波形后生成。

| 字段 | 类型 | 含义 |
|---|---|---|
| `generator_version` | string | 如 `WaveForge 1.3.0` |
| `config` | object | 生效配置摘要：`duration_s`、`sample_rate_hz`、`start_utc_ns`、`segment_count`、`format`、`frame_samples`、`channel_count`、`channels`、`nominal` |
| `final_params` | object | 最终参数：`sample_rate_hz`、`start_utc_ns`、`format`、`frame_samples`、`channel_count` |
| `signal` | object | 旧模型有效值：频率轮廓、逐通道幅值/相位、谐波/间谐波/载波、调制 |
| `program` | object | 仅当 program 非空时出现：`components[]`（含调制链）与 `events[]` |
| `segments` | array | 分段配置；无分段时为空数组 |
| `sample_rate_hz` | number | 采样率 |
| `start_utc_ns` | number | 首样本 UTC 纳秒 |
| `output_file` | string | 波形文件名（调用时传入的路径） |
| `output_sha256` | string | 波形文件 SHA-256，小写十六进制 |
| `sample_count` | number | 每通道总样本数 |
| `total_samples` | number | 与 `sample_count` 相同 |
| `total_frames` | number | 帧数 |

说明：

- manifest 中没有 `test`、`system_hz`、`seed` 字段；场景名只存在于 CLI 输入，不写入 manifest。
- `program.components[].modulators[]` 记录该 kind 实际生效的参数，包括 `start_s`、`hold`；当调制器限定通道时额外记录数值 `channel_mask`（掩码为 0 时不输出，保持旧配置的 manifest 逐字节不变）。`events[]` 记录 `channel_mask`，用于复现逐通道相位偏移。
- `config` 与 `final_params` 都记录最终生效值，不是默认值来源。
- 相同输入两次运行，`output_sha256` 必须相同；manifest 本身不参与哈希。

示例（节选）：

```json
{
  "generator_version": "WaveForge 1.3.0",
  "config": {
    "duration_s": 2,
    "sample_rate_hz": 51200,
    "start_utc_ns": 1794700800000000000,
    "segment_count": 0,
    "format": "f64",
    "frame_samples": 256,
    "channel_count": 3,
    "channels": ["Ua", "Ub", "Uc"],
    "nominal": [220, 220, 220]
  },
  "final_params": {
    "sample_rate_hz": 51200,
    "start_utc_ns": 1794700800000000000,
    "format": "f64",
    "frame_samples": 256,
    "channel_count": 3
  },
  "sample_rate_hz": 51200,
  "start_utc_ns": 1794700800000000000,
  "output_file": "h3.bin",
  "output_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "sample_count": 102400,
  "total_samples": 102400,
  "total_frames": 400
}
```

## 9. 读取示例（Python 3，仅标准库）

```python
import struct

FILE_HDR  = struct.Struct("<8sHHIHHHHQdddQ")   # 64 B
FRAME_HDR = struct.Struct("<4sHHQQQQIIHHI")    # 56 B
BPS = {0: 8, 1: 4, 2: 3}

with open("a122.bin", "rb") as fh:
    magic, ver, hb, fs, nch, frame_n, fmt, flags, t0, period, us, isc, _ = \
        FILE_HDR.unpack(fh.read(64))
    assert magic == b"WGIEC625"

    while True:
        head = fh.read(56)
        if len(head) < 56 or head[:4] != b"WGF1":
            break
        (_, _, _, idx, start, t_start, t_end, frames, fflags, ch, _, nbytes) = \
            FRAME_HDR.unpack(head)
        raw = fh.read(nbytes)
        if fmt == 0:
            plane0 = struct.unpack("<%dd" % frames, raw[:8 * frames])
        elif fmt == 1:
            plane0 = struct.unpack("<%df" % frames, raw[:4 * frames])
        else:
            codes = [int.from_bytes(raw[3*i:3*i+3], "little", signed=True)
                     for i in range(frames)]
            plane0 = [c * us for c in codes]
        # plane0 是第一个通道的帧内样本
```

完整读取器与独立校验见 [`tools/verify.py`](../tools/verify.py) 和 [`tools/check_acceptance.py`](../tools/check_acceptance.py)。

## 10. 确定性与兼容

1. 输出只依赖版本、最终配置、采样率、起始 UTC 与样本格式。
2. 时间戳按样本序号重算，不累加，长时运行无漂移。
3. 同一构建环境下，相同输入逐字节一致。
4. 文件格式版本为 1；字段或语义变化必须提升 `version` 并在发布说明中注明。
5. 读取方应以文件头/magic 判断格式，不要假设文件扩展名。
