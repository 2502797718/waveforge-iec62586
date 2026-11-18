#ifndef WG_H
#define WG_H

#include <stddef.h>
#include <stdint.h>

#include "wg_types.h"

typedef struct wg_handle wg_handle_t;
typedef struct wg_builder wg_builder_t;

/* 回调返回 0 继续，非 0 中止生成并作为 wg_run_stream 的返回值。 */
typedef int32_t (*wg_emit_fn)(const wg_frame_t *pst_frame, void *p_ctx);

/* 创建/销毁生成器。失败返回 NULL，pc_err 写入可读原因。 */
wg_handle_t *wg_create(const wg_config_t *pst_cfg, char *pc_err, size_t err_len);
void wg_destroy(wg_handle_t *pst_wg);

/*
 * 底层 builder：只构造 wg_program_t（分量 + 调制链 + 事件），不直接采样。
 * 各项失败返回负值；wg_create_from_builder 成功后 builder 即可销毁。
 */
wg_builder_t *wg_builder_create(uint32_t sample_rate_hz, uint64_t start_utc_ns);
void wg_builder_destroy(wg_builder_t *pst_b);
int32_t wg_builder_set_channels(wg_builder_t *pst_b, const uint8_t *pu8_ch,
                                uint8_t u8_count, const double *pf64_nominal);
int32_t wg_builder_set_duration(wg_builder_t *pst_b, double f64_duration_s);
int32_t wg_builder_set_output(wg_builder_t *pst_b, uint8_t u8_format,
                              uint32_t u32_frame_samples);
int32_t wg_builder_set_frequency(wg_builder_t *pst_b,
                                 const wg_profile_f64_t *pst_prof);
int32_t wg_builder_add_component(wg_builder_t *pst_b,
                                 const wg_component_t *pst_comp);
int32_t wg_builder_add_modulator(wg_builder_t *pst_b,
                                 uint32_t u32_component_index,
                                 const wg_modulator_t *pst_mod);
int32_t wg_builder_add_event(wg_builder_t *pst_b, const wg_event_t *pst_event);
wg_handle_t *wg_create_from_builder(wg_builder_t *pst_b, char *pc_err,
                                    size_t err_len);

/* 模式 1：生成完整波形文件。 */
int32_t wg_run_file(wg_handle_t *pst_wg, const char *pc_path);

/* 模式 2：逐帧回调，不落盘。 */
int32_t wg_run_stream(wg_handle_t *pst_wg, wg_emit_fn pfn_emit, void *p_ctx);

const char *wg_version_string(void);

#endif /* WG_H */
