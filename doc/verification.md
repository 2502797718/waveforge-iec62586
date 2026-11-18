# 数学保真校验

> 本文说明 WaveForge 的验收口径、自测项、回归场景与独立校验工具的用法。
> **验收范围只有数学保真与结构正确**：不判定 IEC 合格，不评估测量不确定度、标准限值或硬件精度。
> 相关文档：[输出格式](output-format.md) · [架构](architecture.md) · [信号目录](signal-catalog.md)

## 1. 验收口径

| 维度 | 判据 | 说明 |
|---|---|---|
| 样值 | 与解析式一致（浮点/量化容差内） | 独立按配置重建，不读生成器内部状态 |
| 频谱 | 稳态分量 RMS / 相位匹配 | DFT 或最小二乘 |
| 时间戳 | 整数纳秒精确匹配 | 按样本序号重算，不累加 |
| file vs stream | 样本、帧头逐字节一致 | 同一求值核心 |
| 确定性 | 相同输入 SHA-256 相同 | 不依赖墙钟 |
| 资源 | 长时运行 RSS 不随时长增长 | stream 模式 |
| 结构 | magic、长度、帧索引、标志、结束记录 | 独立解析器 |

不做的事：

- 不判定被测设备的 IEC 合格/不合格；
- 不评估 ADC、模拟前端、抗混叠、互感器、真实时钟；
- 不把软件 PASS 写成硬件或型式试验结论；
- 不放宽容差来“让结果通过”。

## 2. 容差（校验器默认）

以下容差来自 [`tools/verify.py`](../tools/verify.py) 与 [`tools/check_acceptance.py`](../tools/check_acceptance.py)，属于**数学重建容差**，不是 IEC 限值：

| 项 | 容差 |
|---|---|
| 基波 RMS 相对误差 | ≤ 0.1 % |
| 基波频率误差 | ≤ 1 mHz |
| 相位误差 | ≤ 0.1° |
| 谐波/间谐波/载波 RMS | ≤ 0.5 % 相对 |
| 时间戳 | 精确整数匹配（0 ns） |
| 动态轨迹（1 s 窗均值） | ≤ 0.1 Hz |
| 幅值斜坡零点附近 | 绝对下限 0.05 % |

f32/i24 的量化误差按格式单独考虑；i24 用文件头 `u_scale`/`i_scale` 还原。

## 3. selftest 13 项

运行：

```sh
make test
```

| # | 用例 | 检查内容 | 典型实测 |
|---|---|---|---|
| 1 | `config-validation` | 默认配置可创建；`sample_rate=0`、`channel_count=0`、`frame_samples=0` 被拒绝 | 全部符合 |
| 2 | `stream-1s` | 1 s、51200 Hz、256 点/帧的样本数/帧数/首末标志/序号/时间戳 | 51200 点、200 帧 |
| 3 | `file-vs-stream` | 同配置 file 与 stream 的 payload 逐字节一致 | 245 760 B 一致 |
| 4 | `determinism` | 同配置连续两次 file 输出的 SHA-256 相同 | 哈希一致 |
| 5 | `phase-A1.1.1` | 42.5↔57.5 Hz 三角下的相位积分连续性 | 最大误差 < 1e−9 rad |
| 6 | `i24-encoding` | i24 比例、码值范围、峰值接近满量程 | scale > 0，码值在 ±8388607 内 |
| 7 | `longrun-660s` | 660 s 流式长跑的样本数/帧数/RSS | 33 792 000 点、132 000 帧，RSS 稳定 |
| 8 | `builder-arbitrary` | 基波 + H3 7 %/45° + H25 0.5 %/90° 的幅值/相位 | 220 V、15.4 V、1.1 V |
| 9 | `per-component-envelope` | 只对 H3 加线性包络，基波不受影响 | 早/晚 H3 比约 9 |
| 10 | `stacked-modulation` | 2.3 Hz/10 % 调幅 + 40–60 Hz 三角 + 5 s 相位 +30° 叠加 | 跳变 30°，频率/包络误差小于阈值 |
| 11 | `custom-triangle` | 40–60 Hz、完整周期 6 s、12 s 时长 | 端点与周期正确 |
| 12 | `no-default` | A6.2.5 返回 `NO_DEFAULT` 且 rc=1 | 字段路径可读 |
| 13 | `old-compat` | 旧 `signal.harmonics` 与等价 program 的 RMS/相位一致 | 相对差 < 1e−6 |

期望输出：

```text
SUMMARY: all tests PASSED
```

## 4. 回归场景

固定起始时间（只影响时间戳，不影响样值），例如 `--start 1794700800`。

| 场景 | 命令 | 关键测量 | 判定 |
|---|---|---|---|
| A1.1.1 | `--test A1.1.1 --duration 12` | 端点 42.53/57.52/57.49/42.49 Hz；斜率 ±3 Hz/s | 端点 ±0.1 Hz |
| A2.5.1 | `--test A2.5.1 --duration 130` | 22.26/175.74/22.26 V；13 个 10 s 窗均 50.125 Hz | 解析窗均值 |
| A4.2.1 | `--test A4.2.1 --duration 15` | 三相 0 V 区间 200/180/140 ms；过零残差 ≤ 19.53 µs | 顺序与区间 |
| A5.1.2 | `--test A5.1.2 --duration 2` | 160.60/176.00/191.40 V | ±0.2 V |
| A6.2.2 | `--test A6.2.2 --duration 2` | 基波 220.000 V；H3 22.000 V（10.000 %） | 相对容差 |
| A8.1.3 | `--test A8.1.3 --duration 2` | 基波 220.000 V；1060 Hz 6.600 V（3.000 %） | 相对容差 |

扩展回归：

| 场景 | 内容 | 判定 |
|---|---|---|
| A6.5.1 | H3 幅值 10→0→10 %（1 %/s），30 s | 轨迹与结构 PASS；verify 汇总 `PARTIAL`（动态分量） |
| A4.1.1 | 三通道 0/90/94 % 阶跃序列 | 顺序/0 V 区间/过零残差 PASS |
| 自定义 program | 50/150/75/1060 Hz + 调制 + 相位阶跃 | 幅值最大偏差 0.0013 V、相位 0.0034° |

### 4.1 动态分量的处理

含调制器、频率轮廓或事件的分量不适用“稳态单值”判据：

- `verify.py` 将其标为 `DYNAMIC`，测量值照常输出，但状态为 `SKIP`；
- 文件级汇总因此可能为 `PARTIAL`，`structure.ok` 仍应为 `true`，`FAIL` 数应为 0；
- 动态轨迹由 `check_acceptance.py` 的轨迹用例或多时刻 DFT 单独核对。

`PARTIAL` 不是生成偏差。

## 5. 运行独立校验

### 5.1 `verify.py`

```sh
python3 tools/verify.py a122.bin
python3 tools/verify.py a122.bin --report a122.verify.json
python3 tools/verify.py a122.bin --manifest a122.bin.manifest.json
python3 tools/verify.py a122.bin --expect a122.expect.json
```

- 自动寻找 manifest：`<file>.manifest.json` → `<stem>.manifest.json` → 同目录 `manifest.json`。
- `--expect` 可提供独立预期（JSON），用于覆盖 manifest 中的预期。
- 退出码：`0` PASS，`1` FAIL，`2` PARTIAL，`3` 用法/IO 错误。
- 无 manifest 时只做结构校验。

输出 JSON 关键字段：

| 字段 | 含义 |
|---|---|
| `structure.ok` | magic/长度/帧索引/时间戳等结构项是否全 PASS |
| `checks[]` | 逐项 `name/expected/actual/tolerance/status` |
| `component_measurements[]` | 逐分量测量；动态分量带 `dynamic: true` |
| `summary` | `PASS` / `FAIL` / `PARTIAL` |

### 5.2 `check_acceptance.py`

```sh
# 通用测量
python3 tools/check_acceptance.py rms  FILE [--channel Ua] [--window 0.2] [--start 0]
python3 tools/check_acceptance.py freq FILE [--channel Ua] [--window 1] [--start 0] [--end N]
python3 tools/check_acceptance.py dft  FILE [--channel Ua] [--freq 50] [--start 0] [--end N]

# 场景化验收
python3 tools/check_acceptance.py a1-1-1 FILE
python3 tools/check_acceptance.py a2-5-1 FILE
python3 tools/check_acceptance.py a4-2-1 FILE
python3 tools/check_acceptance.py a5-1-2 FILE
python3 tools/check_acceptance.py a6-2-2 FILE
python3 tools/check_acceptance.py a6-5-1 FILE
python3 tools/check_acceptance.py a8-1-3 FILE
```

- 每个用例输出 `checks` 与 `values`，并可用 `--json PATH` 另存。
- 退出码 0 = PASS，1 = FAIL。
- 该工具不读取 manifest，按用例内的解析预期独立判断。

### 5.3 建议的完整回归流程

```sh
make clean && make
./build/selftest

./build/wg gen --test A1.1.1 --duration 12 --start 1794700800 --out a111.bin
./build/wg gen --test A2.5.1 --duration 130 --start 1794700800 --out a251.bin
./build/wg gen --test A4.2.1 --duration 15 --start 1794700800 --out a421.bin
./build/wg gen --test A5.1.2 --duration 2 --start 1794700800 --out a512.bin
./build/wg gen --test A6.2.2 --duration 2 --start 1794700800 --out a622.bin
./build/wg gen --test A8.1.3 --duration 2 --start 1794700800 --out a813.bin

for f in a111 a251 a421 a512 a622 a813; do
  python3 tools/verify.py $f.bin --report $f.verify.json
done

python3 tools/check_acceptance.py a1-1-1 a111.bin
python3 tools/check_acceptance.py a2-5-1 a251.bin
python3 tools/check_acceptance.py a4-2-1 a421.bin
python3 tools/check_acceptance.py a5-1-2 a512.bin
python3 tools/check_acceptance.py a6-2-2 a622.bin
python3 tools/check_acceptance.py a8-1-3 a813.bin
```

## 6. 证据目录

运行证据保存在本地的 `runs/<run-id>/`，**不随公开仓库分发**。典型结构：

```text
runs/<run-id>/
├─ command.txt              实际命令与环境
├─ build.log / clean.log    构建输出（0 warning）
├─ selftest.log             13 项自测输出
├─ *.bin                    波形文件
├─ *.bin.manifest.json      对应的 manifest
├─ check/                   场景化验收 JSON
├─ verify/                  verify.py 报告
├─ *.independent-dft.txt    独立谱线/阶跃分析
├─ compat.independent.txt   旧 CLI/旧 JSON 兼容对比
├─ list.log                 场景清单
└─ report.md                结论、边界与异常
```

读取证据时的约定：

- 先看 `report.md` 的结论与边界，再看原始 JSON；
- `PARTIAL` 需区分“动态分量 SKIP”与真实失败；
- 不把某个 run 的 PASS 继承到不同版本或不同配置；
- 失败目录保留，不覆盖。

## 7. 回归触发条件

修改以下内容后必须重跑：

| 改动 | 必跑 |
|---|---|
| 信号模型/相位积分 | selftest + 6 场景回归 + 频谱对比 |
| 帧/文件格式 | 旧文件解析 + `verify.py` 结构项 |
| 配置解析/默认值 | 旧 CLI/旧 JSON 兼容 |
| 场景库 | 受影响场景 + manifest 对比 |
| sink（file/stream） | file-vs-stream 逐字节 |
| 预设层 program 化 | 6 场景 + 新能力场景 |

## 8. 结果解释

| 结果 | 含义 |
|---|---|
| `PASS` | 结构与数学判据均满足 |
| `PARTIAL` | 结构满足，动态分量按设计跳过稳态单值判据 |
| `FAIL` | 存在结构或数学偏差，需保留原始数据并定位 |
| `NOT_RUN` | 未执行 |
| `EXCLUDED` | 明确不适用，并写明理由 |

再次强调：以上状态只描述**软件生成的数学波形**，不描述硬件、模拟链路或 IEC 合格性。
