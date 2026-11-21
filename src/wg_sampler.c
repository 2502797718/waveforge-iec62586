#include <math.h>
#include <string.h>

#include "wg_internal.h"

/* 有段时取该段信号，无段时取顶层信号。 */
static const wg_signal_t *prv_signal_at_seg(const wg_handle_t *pst_wg, uint32_t u32_seg)
{
    if (pst_wg->cfg.segment_count == 0u) {
        return &pst_wg->cfg.signal;
    }
    return &pst_wg->cfg.segments[u32_seg].signal;
}

static double prv_duration_at_seg(const wg_handle_t *pst_wg, uint32_t u32_seg)
{
    if (pst_wg->cfg.segment_count == 0u) {
        return pst_wg->cfg.duration_s;
    }
    return pst_wg->cfg.segments[u32_seg].duration_s;
}

/* 样本序号所属段；超出最后一段边界的部分由末段保持。 */
static uint32_t prv_seg_at(const wg_handle_t *pst_wg, uint64_t u64_sample)
{
    uint32_t u32_idx;

    if (pst_wg->cfg.segment_count == 0u) {
        return 0u;
    }
    for (u32_idx = 0u; u32_idx + 1u < (uint32_t)pst_wg->cfg.segment_count; u32_idx++) {
        if (u64_sample < pst_wg->au64_seg_start[u32_idx + 1u]) {
            return u32_idx;
        }
    }
    return (uint32_t)pst_wg->cfg.segment_count - 1u;
}

/* 按格式量化/编码单点样本到小端字节。 */
static void prv_store_sample(const wg_handle_t *pst_wg, uint8_t u8_ch,
                             double f64_val, unsigned char *p_dst)
{
    if (pst_wg->u8_format == (uint8_t)WG_FMT_F64) {
        prv_store_f64_le(p_dst, f64_val);
    } else if (pst_wg->u8_format == (uint8_t)WG_FMT_F32) {
        prv_store_f32_le(p_dst, (float)f64_val);
    } else {
        double f64_scale;
        double f64_code;

        f64_scale = (u8_ch <= (uint8_t)WG_CH_UC) ? pst_wg->f64_u_scale
                                                 : pst_wg->f64_i_scale;
        if (!(f64_scale > 0.0)) {
            prv_store_i24_le(p_dst, 0);
            return;
        }
        f64_code = round(f64_val / f64_scale);
        if (f64_code > 8388607.0) {
            f64_code = 8388607.0;
        } else if (f64_code < -8388608.0) {
            f64_code = -8388608.0;
        }
        prv_store_i24_le(p_dst, (int32_t)f64_code);
    }
}

void prv_state_reset(wg_handle_t *pst_wg)
{
    uint32_t u32_idx;

    pst_wg->f64_phase = 0.0;
    pst_wg->u64_cursor = 0u;
    pst_wg->u16_evt_next = 0u;
    for (u32_idx = 0u; u32_idx < (uint32_t)WG_MAX_CHANNELS; u32_idx++) {
        pst_wg->af64_phase_offset[u32_idx] = 0.0;
        pst_wg->au8_amp_seg[u32_idx] = 0u;
        pst_wg->au8_amp_target[u32_idx] = 0u;
        pst_wg->au8_amp_pending[u32_idx] = 0u;
        pst_wg->af64_cos_prev[u32_idx] = 0.0;
        pst_wg->au8_prog_amp_set[u32_idx] = 0u;
        pst_wg->af64_prog_amp_pct[u32_idx] = 0.0;
    }
}

void prv_gen_frame(wg_handle_t *pst_wg, uint32_t u32_frames, wg_frame_t *pst_frame)
{
    const uint64_t u64_start = pst_wg->u64_cursor;
    const double f64_fs = (double)pst_wg->s32_fs;
    const uint32_t u32_ch_count = (uint32_t)pst_wg->u16_channel_count;
    uint32_t u32_i;
    uint32_t u32_pos;

    for (u32_i = 0u; u32_i < u32_frames; u32_i++) {
        const uint64_t u64_n = u64_start + (uint64_t)u32_i;
        const uint32_t u32_seg = prv_seg_at(pst_wg, u64_n);
        const wg_signal_t *pst_sig = prv_signal_at_seg(pst_wg, u32_seg);
        const double f64_t_abs = (double)u64_n / f64_fs;
        const double f64_local = (double)(u64_n - pst_wg->au64_seg_start[u32_seg]) / f64_fs;
        const double f64_freq = (pst_wg->u8_program_active != 0u)
            ? prv_program_freq_at(pst_wg, pst_sig, f64_t_abs, f64_local,
                                  prv_duration_at_seg(pst_wg, u32_seg))
            : prv_freq_at(pst_sig, f64_local,
                          prv_duration_at_seg(pst_wg, u32_seg));

        if (pst_wg->u8_program_active != 0u) {
            prv_program_advance_events(pst_wg, f64_t_abs);
        }

        /* 段边界：立即切换，或挂起等待各通道下一个上升过零点。 */
        if (u32_seg > 0u && u64_n == pst_wg->au64_seg_start[u32_seg]) {
            const wg_segment_t *pst_seg = &pst_wg->cfg.segments[u32_seg];

            for (u32_pos = 0u; u32_pos < u32_ch_count; u32_pos++) {
                const uint8_t u8_ch = pst_wg->cfg.channels[u32_pos];

                pst_wg->au8_amp_target[u8_ch] = (uint8_t)u32_seg;
                if (pst_seg->transition == (uint8_t)WG_TRANS_IMMEDIATE) {
                    pst_wg->au8_amp_seg[u8_ch] = (uint8_t)u32_seg;
                    pst_wg->au8_amp_pending[u8_ch] = 0u;
                } else {
                    pst_wg->au8_amp_pending[u8_ch] = 1u;
                }
            }
        }

        for (u32_pos = 0u; u32_pos < u32_ch_count; u32_pos++) {
            const uint8_t u8_ch = pst_wg->cfg.channels[u32_pos];
            const double f64_phase_eff = pst_wg->f64_phase
                                       + pst_wg->af64_phase_offset[u8_ch];
            const double f64_cos = (pst_wg->u8_program_active != 0u)
                ? prv_program_fund_cos(pst_wg, u8_ch, f64_phase_eff)
                : cos(pst_wg->f64_phase
                      + pst_sig->phase_deg[u8_ch] * PRV_DEG2RAD);
            uint32_t u32_amp_seg;
            const wg_signal_t *pst_amp_sig;
            double f64_amp_pct;
            double f64_val;
            unsigned char *p_dst;

            if (pst_wg->au8_amp_pending[u8_ch] != 0u) {
                const double f64_delay = pst_wg->cfg.segments[u32_seg].channel_delay_s[u8_ch];

                if (f64_local >= f64_delay
                    && pst_wg->af64_cos_prev[u8_ch] < 0.0
                    && f64_cos >= 0.0) {
                    pst_wg->au8_amp_seg[u8_ch] = pst_wg->au8_amp_target[u8_ch];
                    pst_wg->au8_amp_pending[u8_ch] = 0u;
                }
            }

            u32_amp_seg = (uint32_t)pst_wg->au8_amp_seg[u8_ch];
            pst_amp_sig = prv_signal_at_seg(pst_wg, u32_amp_seg);
            f64_amp_pct = prv_profile_eval(&pst_amp_sig->amp_pct[u8_ch],
                                           (double)(u64_n - pst_wg->au64_seg_start[u32_amp_seg])
                                               / f64_fs,
                                           prv_duration_at_seg(pst_wg, u32_amp_seg));
            f64_val = (pst_wg->u8_program_active != 0u)
                ? prv_program_channel_value(pst_wg, u8_ch, f64_phase_eff,
                                            f64_t_abs,
                                            (pst_wg->cfg.segment_count > 0u
                                             && f64_amp_pct != 0.0) ? 1 : 0,
                                            f64_amp_pct)
                : prv_channel_value(pst_wg, u8_ch, pst_amp_sig,
                                    pst_wg->f64_phase, f64_t_abs, f64_amp_pct);

            p_dst = pst_wg->p_frame_buf
                  + ((size_t)u32_pos * (size_t)u32_frames + (size_t)u32_i)
                    * (size_t)pst_wg->u32_bytes_per_sample;
            prv_store_sample(pst_wg, u8_ch, f64_val, p_dst);

            pst_wg->af64_cos_prev[u8_ch] = f64_cos;
        }

        /* 相位按采样点积分：φ[n+1] = φ[n] + 2π·f(t_n)/fs。 */
        pst_wg->f64_phase += PRV_TWO_PI * f64_freq / f64_fs;
    }

    pst_wg->u64_cursor = u64_start + (uint64_t)u32_frames;
    pst_frame->samples = pst_wg->p_frame_buf;
    pst_frame->reserved = 0u;
}
