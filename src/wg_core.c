#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wg_internal.h"

static void prv_set_err(char *pc_err, size_t z_err_len, const char *pc_msg)
{
    if (pc_err != NULL && z_err_len > 0u) {
        (void)snprintf(pc_err, z_err_len, "%s", pc_msg);
    }
}

static int32_t prv_check_profile(const wg_profile_f64_t *pst_prof, const char *pc_field,
                                 char *pc_err, size_t z_err_len)
{
    if (pst_prof->profile > (uint8_t)WG_PROFILE_TRIANGLE) {
        if (pc_err != NULL && z_err_len > 0u) {
            (void)snprintf(pc_err, z_err_len, "%s: invalid profile kind", pc_field);
        }
        return PRV_ERR_CONFIG;
    }
    return PRV_OK;
}

static int32_t prv_check_signal(const wg_signal_t *pst_sig, char *pc_err, size_t z_err_len)
{
    uint32_t u32_idx;

    if (prv_check_profile(&pst_sig->frequency, "signal.frequency", pc_err, z_err_len) != PRV_OK) {
        return PRV_ERR_CONFIG;
    }
    for (u32_idx = 0u; u32_idx < (uint32_t)WG_MAX_CHANNELS; u32_idx++) {
        if (prv_check_profile(&pst_sig->amp_pct[u32_idx], "signal.amp_pct",
                              pc_err, z_err_len) != PRV_OK) {
            return PRV_ERR_CONFIG;
        }
    }
    if (pst_sig->modulation.type > (uint8_t)WG_MOD_RECT) {
        prv_set_err(pc_err, z_err_len, "signal.modulation.type: invalid kind");
        return PRV_ERR_CONFIG;
    }
    if (pst_sig->harmonic_count > (uint16_t)WG_MAX_HARMONICS) {
        prv_set_err(pc_err, z_err_len, "signal.harmonics: too many entries");
        return PRV_ERR_CONFIG;
    }
    if (pst_sig->interharmonic_count > (uint16_t)WG_MAX_INTERHARMONICS) {
        prv_set_err(pc_err, z_err_len, "signal.interharmonics: too many entries");
        return PRV_ERR_CONFIG;
    }
    if (pst_sig->tone_count > (uint16_t)WG_MAX_TONES) {
        prv_set_err(pc_err, z_err_len, "signal.tones: too many entries");
        return PRV_ERR_CONFIG;
    }
    return PRV_OK;
}

/* 配置校验。错误信息只返回一条，便于命令行直接展示。 */
static int32_t prv_validate(const wg_config_t *pst_cfg, char *pc_err, size_t z_err_len)
{
    double f64_seg_total = 0.0;
    double f64_span;
    uint32_t u32_idx;

    if (pst_cfg->sample_rate_hz == 0u) {
        prv_set_err(pc_err, z_err_len, "sample_rate_hz: expected integer > 0");
        return PRV_ERR_CONFIG;
    }
    if (!isfinite(pst_cfg->duration_s) || !(pst_cfg->duration_s > 0.0)) {
        prv_set_err(pc_err, z_err_len, "duration_s: expected number > 0");
        return PRV_ERR_CONFIG;
    }
    f64_span = pst_cfg->duration_s * (double)pst_cfg->sample_rate_hz;
    if (!isfinite(f64_span) || f64_span > 9.0e18) {
        prv_set_err(pc_err, z_err_len, "duration_s: too large for sample rate");
        return PRV_ERR_CONFIG;
    }
    if (pst_cfg->channel_count == 0u || pst_cfg->channel_count > (uint8_t)WG_MAX_CHANNELS) {
        prv_set_err(pc_err, z_err_len, "channel_count: expected 1..7");
        return PRV_ERR_CONFIG;
    }
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        if (pst_cfg->channels[u32_idx] >= (uint8_t)WG_MAX_CHANNELS) {
            prv_set_err(pc_err, z_err_len, "channels: invalid channel index");
            return PRV_ERR_CONFIG;
        }
    }
    if (pst_cfg->format > (uint8_t)WG_FMT_I24) {
        prv_set_err(pc_err, z_err_len, "format: expected 0=f64, 1=f32, 2=i24");
        return PRV_ERR_CONFIG;
    }
    if (pst_cfg->frame_samples == 0u || pst_cfg->frame_samples > 65535u) {
        prv_set_err(pc_err, z_err_len, "frame_samples: expected 1..65535");
        return PRV_ERR_CONFIG;
    }
    if (pst_cfg->segment_count > (uint16_t)WG_MAX_SEGMENTS) {
        prv_set_err(pc_err, z_err_len, "segments: too many entries");
        return PRV_ERR_CONFIG;
    }

    if (prv_check_signal(&pst_cfg->signal, pc_err, z_err_len) != PRV_OK) {
        return PRV_ERR_CONFIG;
    }

    if (pst_cfg->program.component_count > 0u) {
        if (prv_program_validate(&pst_cfg->program, pst_cfg->duration_s,
                                 pc_err, z_err_len) != PRV_OK) {
            return PRV_ERR_CONFIG;
        }
    }

    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->segment_count; u32_idx++) {
        const wg_segment_t *pst_seg = &pst_cfg->segments[u32_idx];

        if (!isfinite(pst_seg->duration_s) || !(pst_seg->duration_s > 0.0)) {
            if (pc_err != NULL && z_err_len > 0u) {
                (void)snprintf(pc_err, z_err_len, "segments[%u].duration_s: expected number > 0",
                               (unsigned)u32_idx);
            }
            return PRV_ERR_CONFIG;
        }
        if (pst_seg->transition > (uint8_t)WG_TRANS_ZERO_CROSSING) {
            prv_set_err(pc_err, z_err_len, "segments[].transition: expected immediate|zero_crossing");
            return PRV_ERR_CONFIG;
        }
        if (prv_check_signal(&pst_seg->signal, pc_err, z_err_len) != PRV_OK) {
            return PRV_ERR_CONFIG;
        }
        f64_seg_total += pst_seg->duration_s;
    }

    if (pst_cfg->segment_count > 0u) {
        double f64_limit = pst_cfg->duration_s
                         + 1.0e-9 * (pst_cfg->duration_s > 1.0 ? pst_cfg->duration_s : 1.0);

        if (f64_seg_total > f64_limit) {
            prv_set_err(pc_err, z_err_len, "segments: total duration exceeds duration_s");
            return PRV_ERR_CONFIG;
        }
    }

    if (pst_cfg->format == (uint8_t)WG_FMT_I24) {
        for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
            uint8_t u8_ch = pst_cfg->channels[u32_idx];

            if (!isfinite(pst_cfg->nominal[u8_ch]) || !(pst_cfg->nominal[u8_ch] > 0.0)) {
                prv_set_err(pc_err, z_err_len,
                            "nominal: expected number > 0 for enabled channels in i24");
                return PRV_ERR_CONFIG;
            }
        }
    }

    return PRV_OK;
}

/* 事件按 time_s 稳定升序；同一时刻保持数组顺序。 */
static void prv_sort_events(wg_program_t *pst_prog)
{
    uint16_t u16_i;

    for (u16_i = 1u; u16_i < pst_prog->event_count; u16_i++) {
        wg_event_t st_tmp = pst_prog->events[u16_i];
        uint16_t u16_j = u16_i;

        while (u16_j > 0u
               && pst_prog->events[u16_j - 1u].time_s > st_tmp.time_s) {
            pst_prog->events[u16_j] = pst_prog->events[u16_j - 1u];
            u16_j--;
        }
        pst_prog->events[u16_j] = st_tmp;
    }
}

static double prv_max_nominal(const wg_config_t *pst_cfg, int32_t s32_current)
{
    double f64_max = 0.0;
    uint32_t u32_idx;

    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];
        int32_t s32_is_current = (u8_ch >= (uint8_t)WG_CH_IA) ? 1 : 0;

        if (s32_is_current == s32_current && pst_cfg->nominal[u8_ch] > f64_max) {
            f64_max = pst_cfg->nominal[u8_ch];
        }
    }
    return f64_max;
}

wg_handle_t *wg_create(const wg_config_t *pst_cfg, char *pc_err, size_t err_len)
{
    wg_handle_t *pst_wg;
    uint32_t u32_idx;
    double f64_seg_acc = 0.0;

    if (pst_cfg == NULL) {
        prv_set_err(pc_err, err_len, "cfg: null pointer");
        return NULL;
    }
    if (prv_validate(pst_cfg, pc_err, err_len) != PRV_OK) {
        return NULL;
    }

    pst_wg = (wg_handle_t *)calloc(1u, sizeof(*pst_wg));
    if (pst_wg == NULL) {
        prv_set_err(pc_err, err_len, "out of memory");
        return NULL;
    }

    pst_wg->cfg = *pst_cfg;
    pst_wg->u8_program_active = (pst_wg->cfg.program.component_count > 0u)
                              ? 1u : 0u;
    if (pst_wg->u8_program_active != 0u) {
        prv_sort_events(&pst_wg->cfg.program);
    }

    pst_wg->s32_fs = pst_wg->cfg.sample_rate_hz;
    pst_wg->u64_total_samples = (uint64_t)llround(pst_wg->cfg.duration_s
                                                  * (double)pst_wg->s32_fs);
    if (pst_wg->u64_total_samples == 0u) {
        pst_wg->u64_total_samples = 1u;
    }
    pst_wg->u32_frame_samples = pst_wg->cfg.frame_samples;
    pst_wg->u16_channel_count = (uint16_t)pst_wg->cfg.channel_count;
    pst_wg->u8_format = pst_wg->cfg.format;
    if (pst_wg->u8_format == (uint8_t)WG_FMT_F64) {
        pst_wg->u32_bytes_per_sample = 8u;
    } else if (pst_wg->u8_format == (uint8_t)WG_FMT_F32) {
        pst_wg->u32_bytes_per_sample = 4u;
    } else {
        pst_wg->u32_bytes_per_sample = 3u;
    }

    pst_wg->u32_segment_count = pst_wg->cfg.segment_count;
    pst_wg->au64_seg_start[0] = 0u;
    for (u32_idx = 0u; u32_idx < pst_wg->u32_segment_count; u32_idx++) {
        f64_seg_acc += pst_wg->cfg.segments[u32_idx].duration_s;
        pst_wg->au64_seg_start[u32_idx + 1u] =
            (uint64_t)llround(f64_seg_acc * (double)pst_wg->s32_fs);
    }

    if (pst_wg->u8_format == (uint8_t)WG_FMT_I24) {
        double f64_u_max = prv_max_nominal(&pst_wg->cfg, 0);
        double f64_i_max = prv_max_nominal(&pst_wg->cfg, 1);

        pst_wg->f64_u_scale = f64_u_max * PRV_SQRT2 / 8388607.0;
        pst_wg->f64_i_scale = f64_i_max * PRV_SQRT2 / 8388607.0;
    }

    pst_wg->z_frame_bytes = (size_t)pst_wg->u32_frame_samples
                          * (size_t)pst_wg->u16_channel_count
                          * (size_t)pst_wg->u32_bytes_per_sample;
    pst_wg->p_frame_buf = (unsigned char *)malloc(pst_wg->z_frame_bytes);
    if (pst_wg->p_frame_buf == NULL) {
        prv_set_err(pc_err, err_len, "out of memory");
        free(pst_wg);
        return NULL;
    }

    prv_state_reset(pst_wg);
    return pst_wg;
}

void wg_destroy(wg_handle_t *pst_wg)
{
    if (pst_wg == NULL) {
        return;
    }
    free(pst_wg->p_frame_buf);
    free(pst_wg);
}

uint64_t prv_ns_at(uint64_t u64_sample, uint32_t u32_fs)
{
    uint64_t u64_q = u64_sample / (uint64_t)u32_fs;
    uint64_t u64_r = u64_sample % (uint64_t)u32_fs;

    return u64_q * 1000000000ull
         + (u64_r * 1000000000ull + (uint64_t)(u32_fs / 2u)) / (uint64_t)u32_fs;
}

int32_t prv_run_shared(wg_handle_t *pst_wg, prv_frame_sink_fn pfn_sink, void *p_ctx)
{
    uint64_t u64_sample = 0u;
    uint64_t u64_frame_index = 0u;

    if (pst_wg == NULL || pfn_sink == NULL) {
        return PRV_ERR_ARG;
    }

    prv_state_reset(pst_wg);

    while (u64_sample < pst_wg->u64_total_samples) {
        uint64_t u64_left = pst_wg->u64_total_samples - u64_sample;
        uint32_t u32_frames = (u64_left < (uint64_t)pst_wg->u32_frame_samples)
                            ? (uint32_t)u64_left : pst_wg->u32_frame_samples;
        wg_frame_t st_frame;
        int32_t s32_rc;

        (void)memset(&st_frame, 0, sizeof(st_frame));
        st_frame.frame_index = u64_frame_index;
        st_frame.start_sample = u64_sample;
        st_frame.start_utc_ns = pst_wg->cfg.start_utc_ns
                              + prv_ns_at(u64_sample, pst_wg->s32_fs);
        st_frame.end_utc_ns = pst_wg->cfg.start_utc_ns
                            + prv_ns_at(u64_sample + (uint64_t)u32_frames, pst_wg->s32_fs);
        st_frame.frames = u32_frames;
        st_frame.flags = 0u;
        if (u64_sample == 0u) {
            st_frame.flags |= WG_FRAME_FLAG_FIRST;
        }
        if (u64_sample + (uint64_t)u32_frames == pst_wg->u64_total_samples) {
            st_frame.flags |= WG_FRAME_FLAG_LAST;
        }
        st_frame.channel_count = pst_wg->u16_channel_count;
        st_frame.reserved = 0u;

        prv_gen_frame(pst_wg, u32_frames, &st_frame);

        s32_rc = pfn_sink(&st_frame, p_ctx);
        if (s32_rc != PRV_OK) {
            return s32_rc;
        }

        u64_sample += (uint64_t)u32_frames;
        u64_frame_index++;
    }

    return PRV_OK;
}

int32_t wg_run_stream(wg_handle_t *pst_wg, wg_emit_fn pfn_emit, void *p_ctx)
{
    if (pst_wg == NULL || pfn_emit == NULL) {
        return PRV_ERR_ARG;
    }
    return prv_run_shared(pst_wg, pfn_emit, p_ctx);
}
