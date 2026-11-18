#ifndef WG_CONFIG_H
#define WG_CONFIG_H

#include <stdint.h>
#include <stdio.h>

#include "wg_types.h"

/* 填充默认配置（51200 Hz、f64、256 点/帧、Ua/Ub/Uc、220 V）。 */
void wg_config_default(wg_config_t *pst_cfg);

/* 读取单层 JSON 配置。失败返回非 0，pc_err 形如 "signal.freq_hz: expected number > 0"。 */
int32_t wg_config_load_json(const char *pc_path, wg_config_t *pst_cfg,
                            char *pc_err, size_t err_len);

/* 按标准测试编号（如 A1.2.2）套用预置场景。 */
int32_t wg_scenario_apply(const char *pc_test, int32_t s32_system_hz,
                          wg_config_t *pst_cfg, char *pc_err, size_t err_len);

/* 解析命令行覆盖项，pc_out_path 返回 --out 指定的输出文件。 */
int32_t wg_cli_parse(int argc, char **argv, wg_config_t *pst_cfg,
                     char *pc_out_path, size_t out_len,
                     char *pc_err, size_t err_len);

/* 打印内置场景清单。 */
void wg_scenario_print_all(FILE *p_file);

#endif /* WG_CONFIG_H */
