# FAQ

> 常见问题与边界说明。安装与用法见 [使用手册](user-manual.md)，结构与契约见 [架构](architecture.md)。

## 1. WaveForge 和商业信号源有什么区别？

| 维度 | WaveForge | 商业信号源/功率放大器 |
|---|---|---|
| 形态 | 主机侧 C99 软件，输出文件或逐帧流 | 硬件设备，直接驱动被测设备 |
| 输出 | 数字样本（V/A 工程单位或 i24 码值） | 真实电压/电流、真实时钟 |
| 精度来源 | 数学计算（double） | DAC、模拟前端、时钟、校准 |
| 用途 | 算法开发、回放、回归、协议/格式联调 | 型式试验、计量、硬件验证 |
| 可复现 | 相同配置逐字节一致 | 受硬件与温漂影响 |
| 成本 | 无硬件成本 | 设备与校准成本 |

WaveForge 不替代硬件信号源；它产生的是“应该送给硬件的理想波形”。

## 2. 能用于 IEC 认证或型式试验吗？

**不能。** WaveForge 只生成数学波形：

- 它不做 IEC 合格/不合格判定；
- 它不评估测量不确定度、标准限值或设备精度；
- 软件校验 PASS 不代表 ADC、模拟前端、抗混叠、互感器、真实时钟或整机合格。

如果要做型式试验，需要实验室、标准原文、可溯源校准的硬件信号源与被测设备。

## 3. 支持哪些标准？

- **主依据**：IEC 62586-2 的测试条款结构（6.1–6.17），用于场景命名与信号需求。
- **背景引用**：IEC 61000-4-7（谐波/间谐波）、IEC 61000-4-15（闪变）、IEC 61000-2-4（Class 3 兼容水平）。
- 本项目只记录条款编号与我们自己的默认参数；**不复制标准正文、整表或扫描页**。
- 标准原文需自行向 IEC 或授权渠道获取。

支持的信号能力包括：稳态/频率变化、幅值阶跃与斜坡、三相不平衡、谐波/间谐波/载波、正弦/矩形调幅、分段过零切换、长时输出与事件。

## 4. 为什么很多场景显示 `NO_DEFAULT`？

`NO_DEFAULT` 表示**预设没有标准默认值**，不是“发生器不能生成”。常见原因：

- 标准只给了曲线或未给出该点数值（如 Class 3 的 41–50 次谐波、间谐波曲线）；
- 数值依赖厂商声明（如暂降阈值、迟滞、不确定度）；
- 标准原文转写待复核（如部分 RVC 表）。

解决方式：用 `--freq/--amp/--harmonics` 或 JSON `program` 补上对应字段，即可生成。字段路径会直接写在错误信息里。

## 5. 如何添加自定义谐波？

CLI：

```sh
./build/wg gen --test A1.2.2 --duration 2 \
  --harmonics "3=10%,180;7=5%,0;25=0.5%,90" --out h.bin
```

JSON：

```json
"signal": {
  "freq_hz": 50,
  "harmonics": [
    { "order": 3, "amp_pct": 10, "phase_deg": 180 },
    { "order": 25, "amp_pct": 0.5, "phase_deg": 90 }
  ]
}
```

`order` 必须是 2–64 的整数；幅值相对基波（program 中 `amplitude_ref = "fundamental"`）。

## 6. 如何添加间谐波或载波？

```sh
# 1.5 次间谐波 5 %
./build/wg gen --test A1.2.2 --duration 2 --interharmonics "1.5=5%,0" --out ih.bin

# 1060 Hz 载波 3 %
./build/wg gen --test A1.2.2 --duration 2 --tone 1060=3%,0 --out tone.bin
```

- 间谐波 `order > 0`，可为 1.5、7.5、49.5。
- 载波使用绝对频率，不随基波缩放。

## 7. 如何做线性/三角调制或频率斜坡？

CLI 只提供正弦/矩形调幅。复杂调制用 JSON `program`：

```json
"modulators": [
  { "kind": "amp_linear",   "from": 10, "to": 80, "duration_s": 60 },
  { "kind": "amp_triangle", "from": 10, "to": 80, "period_s": 120 },
  { "kind": "freq_triangle","from": 40, "to": 60, "period_s": 6 },
  { "kind": "freq_step",    "from": 50, "to": 60, "start_s": 5 }
]
```

`period_s` 是完整往返周期；`freq_*` 只能挂在基波上，且整个 program 最多一条。

需要从指定时刻才开始调制时，给调制器加 `start_s`；`amp_triangle` 还可以用 `hold` 指定开始前的幅值：

```json
{ "kind": "amp_triangle", "from": 10, "to": 90, "period_s": 1000,
  "start_s": 5, "hold": 50 }
```

`phase_step` 的 `channel_mask` 只影响被选中的通道；各通道的相位偏移彼此独立。

如果只想调制电流、保持电压不变，给调制器加 `channels`（必须是所属分量通道的子集）：

```json
"components": [{
  "kind": "fundamental",
  "channels": ["Ua","Ub","Uc","Ia","Ib","Ic"],
  "modulators": [
    { "kind": "amp_sine", "frequency_hz": 2.3, "depth_pct": 10,
      "start_s": 10, "channels": ["Ia","Ib","Ic"] }
  ]
}]
```

省略 `channels` 或掩码为 0 时作用于本分量全部通道，与旧行为一致。CLI 对应写法：`--mod sine:2.3:10:0:channels=Ia,Ib,Ic`。

## 8. 为什么没有硬件结论？

发生器只输出数字样本。以下因素不在软件可控范围：

- DAC 分辨率、线性度、噪声；
- 模拟前端带宽、增益、偏置；
- 抗混叠滤波器；
- 互感器/分流器；
- 真实时钟的抖动与漂移；
- 温漂与长期稳定性。

因此本项目只声明 `HOST_FILE` / `HOST_STREAM` / `HOST_VERIFY` 三种软件证据等级，不声明硬件或 IEC 结论。

## 9. 输出文件很大，怎么办？

体积 = 时长 × 采样率 × 通道数 × 每点字节数。例如 2 h、51200 Hz、3 通道：

| 格式 | 约占用 |
|---|---|
| f64 | 8.8 GB |
| f32 | 4.4 GB |
| i24 | 3.3 GB |

建议：

- 长时优先用 stream 模式（不落盘、内存恒定）；
- 或降低采样率/通道数；
- 或改用 f32/i24；
- CLI 默认 7300 s 软上限，更长需 `--allow-long`，程序会打印预估大小。

## 10. 为什么两次运行的哈希不同？

默认 `start_utc_ns` 是“当前整秒”，会进入文件头，因此哈希不同。

```sh
./build/wg gen --test A1.2.2 --start 1794700800 --out a.bin
```

固定起始时间后，相同配置与相同构建环境应逐字节一致。

## 11. 能否在 Windows 上使用？

- `wg` 本身是 C99，可在 Windows 编译。
- `selftest` 依赖 POSIX 接口（`unistd.h`、`/proc/self/statm`、`sha256sum`），建议在 WSL/Linux 运行。
- 构建产物在 `build/`，已被 `.gitignore` 排除；请不要提交二进制，始终重新构建。

## 12. 相位跳变为什么让 H3 变成 +90°？

`phase_step` 把偏移加在积分相位上，各分量使用 `order × φ`：

- 基波：+30°；
- H3：+90°；
- 1.5 次间谐波：+45°；
- 载波：0°。

这是“基波相位跳变、谐波自然跟随”的语义。若需要“每条谱线绝对 +30°”，当前实现不满足，见 [项目日志](project-log.md) 的 ISS-020。

## 13. 如何复现一次实验？

保存：

1. 完整命令或 JSON；
2. `wg_version_string()` / `generator_version`；
3. `<输出>.manifest.json`；
4. 输出文件的 SHA-256；
5. 采样率、起始 UTC、样本格式。

重新运行时固定 `--start`，即可得到相同哈希（同一构建环境）。

## 14. 能否直接接入我的程序？

可以。两种方式：

- 读文件：按 [输出格式](output-format.md) 解析，文件头有 magic、格式和比例；
- 用 C API：`wg_run_stream()` 逐帧回调，`wg_frame_t.samples` 指向平面样本。

发生器不包含任何私有报文或被测设备格式，接入层由使用方自行实现。
