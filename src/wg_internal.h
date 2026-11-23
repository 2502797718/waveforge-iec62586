#ifndef WG_INTERNAL_H
#define WG_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "wg.h"
#include "wg_types.h"

/* 内部常量：pi 与角度换算，避免依赖非标准 M_PI。 */
#define PRV_PI       3.14159265358979323846
#define PRV_TWO_PI   (2.0 * PRV_PI)
#define PRV_DEG2RAD  (PRV_PI / 180.0)
#define PRV_SQRT2    1.41421356237309504880

/* 内部返回码：0 成功，负值为错误。 */
#define PRV_OK           0
#define PRV_ERR_ARG      (-1)
#define PRV_ERR_IO       (-2)
#define PRV_ERR_ALLOC    (-3)
#define PRV_ERR_CONFIG   (-4)

/*
 * 生成器实例。帧缓冲始终按小端字节序编码，文件模式直接写缓冲，
 * 流模式把同一缓冲交给回调，因此两种模式逐字节一致。
 */
struct wg_handle {
    wg_config_t cfg;

    uint32_t s32_fs;
    uint64_t u64_total_samples;
    uint32_t u32_frame_samples;
    uint16_t u16_channel_count;
    uint8_t  u8_format;
    uint32_t u32_bytes_per_sample;
    size_t   z_frame_bytes;
    unsigned char *p_frame_buf;

    double f64_u_scale;
    double f64_i_scale;

    uint32_t u32_segment_count;
    uint64_t au64_seg_start[WG_MAX_SEGMENTS + 1];

    uint8_t au8_amp_seg[WG_MAX_CHANNELS];
    uint8_t au8_amp_target[WG_MAX_CHANNELS];
    uint8_t au8_amp_pending[WG_MAX_CHANNELS];
    double  af64_cos_prev[WG_MAX_CHANNELS];

    double   f64_phase;
    uint64_t u64_cursor;

    /* program（分量 + 调制链 + 事件）求值状态；旧路径不使用。 */
    uint8_t  u8_program_active;
    uint16_t u16_evt_next;
    double   af64_phase_offset[WG_MAX_CHANNELS];
    uint8_t  au8_prog_amp_set[WG_MAX_CHANNELS];
    double   af64_prog_amp_pct[WG_MAX_CHANNELS];
};

typedef int32_t (*prv_frame_sink_fn)(const wg_frame_t *pst_frame, void *p_ctx);

/* 采样状态复位：每次 run 从 t=0、φ=0 重新开始。 */
void prv_state_reset(wg_handle_t *pst_wg);

/* 生成下一帧样本并填充 pst_frame->samples（指向内部帧缓冲）。 */
void prv_gen_frame(wg_handle_t *pst_wg, uint32_t u32_frames, wg_frame_t *pst_frame);

/* 两种输出模式共用的逐帧循环。 */
int32_t prv_run_shared(wg_handle_t *pst_wg, prv_frame_sink_fn pfn_sink, void *p_ctx);

/* 整数纳秒时间，n 为全局样本序号。 */
uint64_t prv_ns_at(uint64_t u64_sample, uint32_t u32_fs);

/* 信号模型。 */
double prv_profile_eval(const wg_profile_f64_t *pst_prof, double f64_t_local,
                        double f64_duration);
double prv_freq_at(const wg_signal_t *pst_sig, double f64_t_local, double f64_duration);
double prv_mod_factor(const wg_modulation_t *pst_mod, double f64_t_abs);
double prv_channel_value(const wg_handle_t *pst_wg, uint8_t u8_ch,
                         const wg_signal_t *pst_sig, double f64_phase,
                         double f64_t_abs, double f64_amp_pct);

/* program 校验：字段路径写入 pc_err，非法返回 PRV_ERR_CONFIG。 */
int32_t prv_component_validate(const wg_component_t *pst_comp, uint32_t u32_idx,
                               char *pc_err, size_t z_err_len);
int32_t prv_modulator_validate(const wg_modulator_t *pst_mod,
                               uint8_t u8_comp_kind, uint8_t u8_comp_mask,
                               uint32_t u32_comp_idx, uint32_t u32_mod_idx,
                               char *pc_err, size_t z_err_len);
int32_t prv_event_validate(const wg_event_t *pst_event, uint32_t u32_idx,
                           double f64_duration, char *pc_err, size_t z_err_len);
int32_t prv_program_validate(const wg_program_t *pst_prog, double f64_duration,
                             char *pc_err, size_t z_err_len);

/* program 求值：频率轮廓、通道瞬时值、过零判据与事件推进。 */
double prv_program_freq_at(const wg_handle_t *pst_wg, const wg_signal_t *pst_sig,
                           double f64_t_abs, double f64_t_local,
                           double f64_duration);
double prv_program_channel_value(const wg_handle_t *pst_wg, uint8_t u8_ch,
                                 double f64_phase_eff, double f64_t_abs,
                                 int s32_has_seg_amp, double f64_seg_amp_pct);
double prv_program_fund_cos(const wg_handle_t *pst_wg, uint8_t u8_ch,
                            double f64_phase_eff);
void prv_program_advance_events(wg_handle_t *pst_wg, double f64_t_abs);

/* 小端编码。 */
void prv_store_le16(unsigned char *p, uint16_t u16_val);
void prv_store_le32(unsigned char *p, uint32_t u32_val);
void prv_store_le64(unsigned char *p, uint64_t u64_val);
void prv_store_f64_le(unsigned char *p, double f64_val);
void prv_store_f32_le(unsigned char *p, float f32_val);
void prv_store_i24_le(unsigned char *p, int32_t s32_val);

/* 对已关闭的输出文件计算 SHA-256，ac_hex 至少 65 字节。 */
int32_t prv_sha256_file(const char *pc_path, char ac_hex[65]);

#endif /* WG_INTERNAL_H */
