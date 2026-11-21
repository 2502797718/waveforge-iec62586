# 输入格式

> 本文描述 WaveForge 的命令行（CLI）与 JSON 配置契约。
> 核心原则：**未给出的字段取默认值；给出的值原样生效；未知字段直接拒绝。**
> 相关文档：[架构](architecture.md) · [输出格式](output-format.md) · [信号目录](signal-catalog.md)

## 1. 入口

```text
wg gen [--test ID] [--config FILE] [选项]
wg list
```

- `wg list` 打印内置场景：`场景名 | 50/60 | 说明`。非 `DEFAULT` 状态会写在说明列（`CUSTOM:`、`NO_DEFAULT`、`EXCLUDED:`）。
- `wg gen` 负责生成文件。
- `--help` / `-h` 打印用法。
- `--stream` 在 CLI 中不支持，会提示 `stream mode: use C API` 并以状态 2 退出；流式输出只能用 C API。
- 未知命令或未知选项直接报错退出，不做静默忽略。

## 2. 合并顺序与默认值

### 2.1 合并顺序

```text
内置默认值 → --system → --test 套用预设 → --config 读 JSON → 其余覆盖项（按出现顺序）
```

- `--test` 和 `--config` 可以同时出现：先套预设，再用 JSON 覆盖。
- 两者都不给时，使用内置默认配置（50 Hz、60 s、`Ua/Ub/Uc`、220 V、f64）。
- 命令行覆盖项始终最后生效；manifest 记录的是最终生效值。

### 2.2 默认值

| 字段 | 默认 |
|---|---|
| 系统频率 | 50 Hz（`--system 60` 切换为 60 Hz 预设） |
| 基波频率 | 50 Hz |
| 总时长 | 60 s |
| 通道 | `Ua, Ub, Uc` |
| `nominal`（电压通道） | 220 V |
| `nominal`（电流通道） | 5 A |
| 相位 | `0, −120, +120`°；电流通道同相 |
| 采样率 | 51200 Hz |
| 帧长 | 256 点/帧 |
| 样本格式 | f64 |
| 起始 UTC | 当前整秒 |
| 幅值 | 100 % |
| 调制 | 无 |
| program | 空（走旧 `signal` 模型） |

## 3. CLI 选项

### 3.1 场景与配置

| 选项 | 别名 | 含义 | 约束 |
|---|---|---|---|
| `--test ID` | `-t` | 场景名（大小写不敏感），见 `wg list` | 未知场景报 `test: unknown scenario` |
| `--system 50\|60` | — | 系统频率，决定预设频率 | 只接受 50 或 60 |
| `--config FILE` | `-c` | 单层 JSON 配置 | 文件 ≤ 4 MiB |
| `--list` | — | 打印场景清单并退出 | 可放在 `gen` 后任意位置 |

### 3.2 时间、采样与输出

| 选项 | 别名 | 含义 | 取值 |
|---|---|---|---|
| `--duration TIME` | `-d` | 总时长 | `60`、`60s`、`500ms`、`2min`、`1h`；范围 0.001–7300 s |
| `--rate HZ` | `--sample-rate` | 采样率 | 整数 1000–1 000 000 |
| `--frame N` | `--frame-samples` | 每帧点数 | 整数 1–65535 |
| `--format f64\|f32\|i24` | — | 样本格式 | 默认 f64 |
| `--start TIME` | `--start-utc` | 首样本 UTC 时间 | RFC 3339（如 `2026-11-18T00:00:00Z`、带 `+08:00` 偏移）或 epoch 秒 |
| `--out FILE` | `-o` | 输出文件 | 默认 `out.bin` |

> 需要 2 h 聚合块并额外留 10 s 让最后一个 10 min 窗闭合时，可设 `--duration 7210`；上限为 7300 s。

### 3.3 信号覆盖

| 选项 | 映射到 | 语法 |
|---|---|---|
| `--freq HZ` | 基波频率（常数轮廓） | `--freq 49.95` |
| `--amp ...` | 基波逐通道幅值 | `--amp Ua=80%,Ub=80%`，规则见下 |
| `--phase ...` | 基波逐通道相位 | `--phase 0,-120,120` 或 `--phase Ua=0,Ub=-120` |
| `--harmonics ...` | 谐波列表 | `"3=10%,180;7=5%,0"` |
| `--interharmonics ...` | 间谐波列表 | `"1.5=5%,0;7.5=10%,0"` |
| `--tone ...` | 载波频点（可重复） | `1060=3%,0` |
| `--mod ...` | 基波调制器（可重复） | `sine:2.3:10:0` / `rect:10:5:50:0` |
| `--phase-step ...` | 相位阶跃事件（可重复） | `5.0:30` |

#### `--amp` 规则

- `通道=值`：只改指定通道，例如 `--amp Ua=80%,Ub=80%`。
- 裸值：应用到所有已启用通道，例如 `--amp 80%`。
- 值带 `%`：相对该通道 `nominal`。
- 值带 `V`/`A` 或不带单位：按该通道物理量换算为百分比（`Ua=176` 等价于 `176V`）。
- 百分比范围 0–1000。

#### `--harmonics` / `--interharmonics` / `--tone`

- `--harmonics` 用 `;` 分隔多项，每项 `次数=幅值[%][,相位°]`；次数必须是 2–64 的整数。
- `--interharmonics` 的 `次数 > 0`，允许 1.5、7.5、49.5。
- `--tone` 的键是绝对频率 Hz（>0），可重复给多个 `--tone`。
- 第一次出现 `--harmonics`/`--interharmonics`/`--tone` 时，会先清空预设里的同类分量，再追加；同一选项重复出现时继续追加。

#### `--mod` 规则

```text
sine:FM:DEPTH[:PHASE]        正弦调幅
rect:FM:DEPTH:DUTY[:PHASE]   矩形调幅，DUTY 为 0–100 %
```

- 可重复，最多 8 条，全部挂在基波上。
- 使用 `--mod` 时，CLI 会把当前旧 `signal` 物化为 program；若存在 `segments`，报 `mod: not supported with segments`。
- 复杂调制（`amp_linear`、`amp_triangle`、`freq_triangle` 等）没有对应的 CLI 选项，请使用 JSON `program`。

#### `--phase-step` 规则

```text
TIME_S:ANGLE_DEG
```

- 作用于所有已启用通道。
- `TIME_S` 必须在 `[0, duration_s]` 内。
- 语义见 [架构](architecture.md) §5.3：偏移按分量 `order` 缩放，载波不变。

## 4. 单位与通道名

1. 频率：Hz；省略单位按 Hz。
2. 幅值：`%` 为相对 `nominal`；`V`/`A` 为绝对 RMS；省略单位按通道物理量（电压 V、电流 A）。
3. 相位：度，允许负值。
4. 时间：`s`（默认）、`ms`、`min`、`h`。
5. 采样率：Hz，正整数。
6. 通道名固定为 `Ua`、`Ub`、`Uc`、`Ia`、`Ib`、`Ic`、`In`，对应索引 0–6。
7. 默认只启用 `Ua/Ub/Uc`；需要电流时通过 JSON `channels` 显式加入。
8. 通道名大小写敏感（`Ua` 有效，`ua` 无效）。

## 5. JSON 配置

### 5.1 顶层结构

```json
{
  "test": "A2.5.1",
  "time": { "duration_s": 660, "sample_rate_hz": 51200, "start_utc_ns": 0 },
  "channels": ["Ua", "Ub", "Uc"],
  "signal": { },
  "segments": [],
  "program": { },
  "output": { "format": "f64", "frame_samples": 256 }
}
```

| 顶层键 | 必需 | 说明 |
|---|---|---|
| `test` | 否 | **仅作追溯元数据**，不会套用预设；预设只能通过 CLI `--test` 触发 |
| `time` | 否 | `duration_s`、`sample_rate_hz`、`start_utc_ns` |
| `channels` | 否 | 数组或逗号分隔字符串；1–7 个，不重复 |
| `signal` | 否 | 旧信号模型 |
| `segments` | 否 | 一层时间线 |
| `program` | 否 | 分量 + 调制链 + 事件；非空时优先于 `signal` |
| `output` | 否 | `format`、`frame_samples` |

顶层未知键报 `<key>: unknown field`；对象内未知键报 `<path>.<key>: unknown field`。

### 5.2 `time`

| 字段 | 类型 | 范围 | 默认 |
|---|---|---|---|
| `duration_s` | number | 0.001–7300 | 60 |
| `sample_rate_hz` | integer | 1000–1 000 000 | 51200 |
| `start_utc_ns` | integer | ≥ 0 | 当前整秒 |

`start_utc_ns` 只接受整数纳秒；需要 RFC 3339 时请用 CLI `--start`。

### 5.3 `signal`（旧模型）

```json
{
  "freq_hz": 50.125,
  "amp_pct": { "Ua": 100, "Ub": 100, "Uc": 100 },
  "phase_deg": { "Ua": 0, "Ub": -120, "Uc": 120 },
  "harmonics": [ { "order": 3, "amp_pct": 10, "phase_deg": 180 } ],
  "interharmonics": [],
  "tones": [],
  "modulation": null
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `freq_hz` | number > 0 | 基波常数频率 |
| `amp_pct` | number 或 `{通道: number}` | 0–1000；number 应用到所有已启用通道 |
| `phase_deg` | 对象或数组 | 数组按已启用通道顺序对应 |
| `harmonics[]` | array | `order` 2–64 整数；`amp_pct` 或 `amplitude_pct`；`phase_deg` 可省 |
| `interharmonics[]` | array | `order > 0`，可小数 |
| `tones[]` | array | `frequency_hz > 0` |
| `modulation` | object 或 null | `type` 为 `none`/`sine`/`rect` |

`modulation` 字段：

| 字段 | 范围 | 说明 |
|---|---|---|
| `type` | `none`/`sine`/`rect` | 默认 `none` |
| `frequency_hz` | > 0 | `type != none` 时必填 |
| `depth_pct` | 0–100 | 调制深度 |
| `duty_pct` | 0–100 | 仅 `rect`，默认 50 |
| `phase_deg` | number | 调制初相 |

旧 `signal.modulation` 只乘在基波项；program 的 `amp_sine`/`amp_rect` 作用于所属分量，差异见 [架构](architecture.md) §5.5。

### 5.4 `segments`

```json
"segments": [
  {
    "duration_s": 1.0,
    "transition": "zero_crossing",
    "channel_delay_s": { "Ua": 0, "Ub": 0.00333, "Uc": 0.00667 },
    "signal": { "amp_pct": 80 }
  }
]
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `duration_s` | number | 0.001–7300 |
| `transition` | string | `immediate`（默认）或 `zero_crossing` |
| `channel_delay_s` | 对象或数组 | 相对段起点的延时，≥ 0；`zero_crossing` 时配合本相过零点使用 |
| `signal` | object | 只写需要覆盖的字段，其余继承上一层 |

规则：

- 只允许一层，段内不能再写 `segments` 或 `program`。
- 各段时长之和必须等于 `time.duration_s`（允许 1e-9 的相对浮点余量）；不足或超出都会被拒绝。
- `immediate`：段起点立即生效；`zero_crossing`：各通道在延时后自己的上升过零点切换。
- 使用 `segments` 时，CLI 的 `--mod` / `--phase-step` 不可用。

### 5.5 `program`

`program` 放在 JSON 顶层，是全局分量/事件集合：

```json
{
  "time": { "duration_s": 10, "sample_rate_hz": 51200 },
  "program": {
    "components": [
      {
        "kind": "fundamental",
        "order": 1,
        "frequency_hz": 50,
        "amp_pct": { "Ua": 100, "Ub": 100, "Uc": 100 },
        "phase_deg": { "Ua": 0, "Ub": -120, "Uc": 120 },
        "channels": ["Ua", "Ub", "Uc"],
        "amplitude_ref": "nominal",
        "modulators": [
          { "kind": "amp_sine", "frequency_hz": 2.3, "depth_pct": 10, "phase_deg": 0 },
          { "kind": "freq_triangle", "from": 40, "to": 60, "period_s": 10 }
        ]
      },
      {
        "kind": "harmonic",
        "order": 3,
        "amp_pct": 7,
        "phase_deg": 45,
        "amplitude_ref": "fundamental"
      },
      {
        "kind": "interharmonic",
        "order": 1.5,
        "amp_pct": 5
      },
      { "kind": "tone", "frequency_hz": 1060, "amp_pct": 1 }
    ],
    "events": [
      { "kind": "phase_step", "time_s": 5, "channel_mask": "Ua,Ub,Uc", "value": 30 }
    ]
  }
}
```

#### `components[]`

| 字段 | 类型 | 说明 |
|---|---|---|
| `kind` | string | `fundamental` / `harmonic` / `interharmonic` / `tone` |
| `order` | number | 基波必须为 1；谐波 2–64 整数；间谐波 > 0（可小数）；`tone` 忽略 |
| `frequency_hz` | number | `tone` 必填且 > 0；其余可省略（可填 0） |
| `amp_pct` / `amplitude_pct` | number 或 `{通道: number}` | 0–1000 |
| `phase_deg` | number 或 `{通道: number}` | 度 |
| `channels` | 数组或逗号字符串 | 默认全部已启用通道 |
| `amplitude_ref` | `"nominal"`/`"fundamental"` 或 0/1 | 默认：谐波为 1，其余为 0 |
| `modulators[]` | array | 最多 8 条 |

#### `modulators[]`

| `kind` | 必填 | 可选 |
|---|---|---|
| `amp_const` | `from` | — |
| `amp_linear` | `from`、`to`、`duration_s` | — |
| `amp_triangle` | `from`、`to`、`period_s` | — |
| `amp_step` | `from`、`to`、`start_s` | — |
| `amp_sine` | `frequency_hz`、`depth_pct` | `phase_deg` |
| `amp_rect` | `frequency_hz`、`depth_pct` | `duty_pct`（默认 0）、`phase_deg` |
| `freq_linear` | `from`、`to`、`duration_s` | 仅基波 |
| `freq_triangle` | `from`、`to`、`period_s` | 仅基波 |
| `freq_step` | `from`、`to`、`start_s` | 仅基波 |

- `amp_*` 的 `from`/`to` 单位为 %，范围 0–1000。
- `freq_*` 的 `from`/`to` 单位为 Hz，必须 > 0；整个 program 最多一条 `FREQ_*`。
- 未知 `kind` 直接拒绝，不降级为“无调制”。
- 同一个分量可叠加多条调制器，幅值因子相乘。

#### `events[]`

| 字段 | 类型 | 说明 |
|---|---|---|
| `kind` | string | `amp_step` / `phase_step` |
| `time_s` | number | `[0, duration_s]`，相对全局起点 |
| `channel_mask` | 整数 0–127 或通道数组/字符串 | bit n = 通道 n；省略表示所有通道 |
| `value` | number | `amp_step` 为幅值 %；`phase_step` 为角度 ° |
| `duration_s` | number | 过渡时长，≥ 0；当前实现按立即生效处理 |

#### 优先级与兼容

- `program.components` 非空时，忽略旧 `signal.harmonics/interharmonics/tones/modulation`（不合并）。
- 基波分量未写 `amp_pct`/`phase_deg` 时，继承 `signal.amp_pct`/`signal.phase_deg`。
- 基波分量未写 `frequency_hz` 时，使用 `signal.freq_hz`（或默认 50 Hz）。
- `program` 为空（无分量、无事件）时完全走旧 `signal` 路径。

### 5.6 `output`

| 字段 | 类型 | 取值 | 默认 |
|---|---|---|---|
| `format` | string | `f64` / `f32` / `i24` | `f64` |
| `frame_samples` | integer | 1–65535 | 256 |

## 6. 完整示例

### 6.1 稳态 + 单次谐波

```json
{
  "time": { "duration_s": 2 },
  "channels": ["Ua", "Ub", "Uc"],
  "signal": {
    "freq_hz": 50,
    "amp_pct": 100,
    "phase_deg": { "Ua": 0, "Ub": -120, "Uc": 120 },
    "harmonics": [ { "order": 3, "amp_pct": 10, "phase_deg": 180 } ]
  },
  "output": { "format": "f64", "frame_samples": 256 }
}
```

```sh
wg gen --config h3.json --out h3.bin
```

### 6.2 频率三角 + 正弦调幅（复杂调制走 program）

```json
{
  "time": { "duration_s": 20, "start_utc_ns": 1794700800000000000 },
  "program": {
    "components": [{
      "kind": "fundamental",
      "frequency_hz": 50,
      "amp_pct": 100,
      "phase_deg": 0,
      "modulators": [
        { "kind": "freq_triangle", "from": 42.5, "to": 57.5, "period_s": 10 },
        { "kind": "amp_sine", "frequency_hz": 2.3, "depth_pct": 10 }
      ]
    }]
  }
}
```

`period_s` 是完整往返周期（42.5 → 57.5 → 42.5 Hz）；单程 5 s 就写 `period_s = 10`。

### 6.3 分段暂降 + 过零切换

```json
{
  "time": { "duration_s": 1.0 },
  "segments": [
    { "duration_s": 0.2, "transition": "immediate", "signal": { "amp_pct": 100 } },
    {
      "duration_s": 0.2,
      "transition": "zero_crossing",
      "channel_delay_s": { "Ua": 0, "Ub": 0.00333, "Uc": 0.00667 },
      "signal": { "amp_pct": 0 }
    },
    { "duration_s": 0.6, "transition": "zero_crossing", "signal": { "amp_pct": 100 } }
  ]
}
```

三段之和必须等于 1.0 s。

### 6.4 事件：幅值阶跃 + 相位阶跃

```json
{
  "time": { "duration_s": 10 },
  "program": {
    "components": [{ "kind": "fundamental", "frequency_hz": 50, "amp_pct": 100 }],
    "events": [
      { "kind": "amp_step", "time_s": 3, "channel_mask": "Ub", "value": 70 },
      { "kind": "phase_step", "time_s": 6, "channel_mask": ["Ua", "Ub", "Uc"], "value": 30 }
    ]
  }
}
```

## 7. 状态：DEFAULT / CUSTOM / NO_DEFAULT / EXCLUDED

| 状态 | 含义 | `wg gen --test` 行为 |
|---|---|---|
| `DEFAULT` | 标准默认值齐备 | 直接生成 |
| `CUSTOM` | 标准未给数值，或属通道/能力选择项 | 预设返回 `CUSTOM: <字段路径>`，需用户补参数 |
| `NO_DEFAULT` | 缺标准数值或设备声明 | 预设返回 `NO_DEFAULT: <字段路径>`，不产生文件 |
| `EXCLUDED` | 不属波形发生器职责 | 返回 `EXCLUDED: <原因>` |

- 这三种拒绝都**不是能力缺失**：用户用 JSON/C API 补齐字段后即可生成。
- 退出码：成功 0；配置/场景错误 1；用法错误 2。
- 预设拒绝时不会创建输出文件。

## 8. 错误格式

错误只返回一条“字段路径 + 期望”，便于命令行直接展示：

```text
freq_hz: expected number > 0
amp_pct.Ua: expected number in [0, 1000]
phase_deg.Ub: expected number
duration_s: expected number in [0.001, 7300]
sample_rate_hz: expected integer in [1000, 1000000]
frame_samples: expected integer in [1, 65535]
channels[1]: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In
modulation.type: expected one of none|sine|rect
program.components[0].kind: expected one of fundamental|harmonic|interharmonic|tone
program.components[0].order: expected integer 2..64
program.components[1].modulators[0].duration_s: expected number > 0
program.components[0].modulators[0].kind: FREQ_* only allowed on the fundamental component
program.modulators: expected at most 1 FREQ_* entry
program.components: expected at least one fundamental
segments: expected total duration >= duration_s
segments: total duration exceeds duration_s
mod: not supported with segments
NO_DEFAULT: harmonics[41..50].amp_pct: Class 3 levels not defined
CUSTOM: signal.reference_channel: user must supply per-channel stimulus
EXCLUDED: aliasing.margin: analog front-end property
test: unknown scenario 'A0.0.0'
```

路径规则：JSON 层级用 `.`，数组用 `[i]`；CLI 覆盖项映射到 JSON 路径后报错。

## 9. C API 输入

不想经过 CLI/JSON 时，直接使用 builder：

```c
#include "wg.h"

wg_builder_t *b = wg_builder_create(51200u, 0u);
if (b == NULL) { /* 内存不足 */ }

const uint8_t ch[3] = { WG_CH_UA, WG_CH_UB, WG_CH_UC };
const double  nom[3] = { 220.0, 220.0, 220.0 };

wg_builder_set_channels(b, ch, 3u, nom);
wg_builder_set_duration(b, 10.0);
wg_builder_set_output(b, (uint8_t)WG_FMT_F64, 256u);

wg_component_t c;
memset(&c, 0, sizeof(c));
c.kind = WG_COMP_FUNDAMENTAL;
c.order = 1.0;
c.frequency_hz = 50.0;
c.channel_mask = 0x07u;
c.amp_pct[0] = c.amp_pct[1] = c.amp_pct[2] = 100.0;
wg_builder_add_component(b, &c);

char err[256];
wg_handle_t *h = wg_create_from_builder(b, err, sizeof(err));
wg_builder_destroy(b);          /* 句柄创建后 builder 可销毁 */
if (h == NULL) { /* err 为可读原因 */ }
```

- builder 创建在堆上（内部持有约 121 KB 配置），不会撑爆调用方栈。
- 各 `wg_builder_*` 返回 0 成功、负值失败；失败时不修改已加入的内容。
- `wg_create_from_builder()` 会完整复检配置（含“至少一个基波”）。

## 10. 资源与容量

| 项 | 上限 |
|---|---|
| 分量总数 | 96 |
| 每分量调制器 | 8 |
| 事件总数 | 64 |
| 通道数 | 7 |
| 分段数 | 16 |
| 旧模型谐波/间谐波/载波 | 64 / 16 / 8 |
| JSON 文件 | 4 MiB，嵌套深度 16 |

超限直接报错，不静默截断。

## 11. 版权与标准说明

本文中的数值是 WaveForge 的**预设默认值**，用于演示与回归；它们不是标准原文的替代。
IEC 62586-2、IEC 61000-4-7、IEC 61000-4-15、IEC 61000-2-4 等标准原文需自行向 IEC 或授权渠道获取。
本项目不复制标准整表、整页或扫描件，也不对任何设备作 IEC 合格判定。
