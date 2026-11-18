#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wg_internal.h"

/* 通道编号对应的文件名。 */
static const char *const s_apc_channel_name[WG_MAX_CHANNELS] = {
    "Ua", "Ub", "Uc", "Ia", "Ib", "Ic", "In"
};

void prv_store_le16(unsigned char *p, uint16_t u16_val)
{
    p[0] = (unsigned char)(u16_val & 0xFFu);
    p[1] = (unsigned char)((u16_val >> 8) & 0xFFu);
}

void prv_store_le32(unsigned char *p, uint32_t u32_val)
{
    p[0] = (unsigned char)(u32_val & 0xFFu);
    p[1] = (unsigned char)((u32_val >> 8) & 0xFFu);
    p[2] = (unsigned char)((u32_val >> 16) & 0xFFu);
    p[3] = (unsigned char)((u32_val >> 24) & 0xFFu);
}

void prv_store_le64(unsigned char *p, uint64_t u64_val)
{
    p[0] = (unsigned char)(u64_val & 0xFFu);
    p[1] = (unsigned char)((u64_val >> 8) & 0xFFu);
    p[2] = (unsigned char)((u64_val >> 16) & 0xFFu);
    p[3] = (unsigned char)((u64_val >> 24) & 0xFFu);
    p[4] = (unsigned char)((u64_val >> 32) & 0xFFu);
    p[5] = (unsigned char)((u64_val >> 40) & 0xFFu);
    p[6] = (unsigned char)((u64_val >> 48) & 0xFFu);
    p[7] = (unsigned char)((u64_val >> 56) & 0xFFu);
}

void prv_store_f64_le(unsigned char *p, double f64_val)
{
    uint64_t u64_bits;

    (void)memcpy(&u64_bits, &f64_val, sizeof(u64_bits));
    prv_store_le64(p, u64_bits);
}

void prv_store_f32_le(unsigned char *p, float f32_val)
{
    uint32_t u32_bits;

    (void)memcpy(&u32_bits, &f32_val, sizeof(u32_bits));
    prv_store_le32(p, u32_bits);
}

void prv_store_i24_le(unsigned char *p, int32_t s32_val)
{
    uint32_t u32_bits = (uint32_t)s32_val;

    p[0] = (unsigned char)(u32_bits & 0xFFu);
    p[1] = (unsigned char)((u32_bits >> 8) & 0xFFu);
    p[2] = (unsigned char)((u32_bits >> 16) & 0xFFu);
}

static void prv_build_file_header(const wg_handle_t *pst_wg, unsigned char *p_hdr)
{
    (void)memset(p_hdr, 0, 64u);
    (void)memcpy(p_hdr, "WGIEC625", 8u);
    prv_store_le16(p_hdr + 8u, 1u);
    prv_store_le16(p_hdr + 10u, 64u);
    prv_store_le32(p_hdr + 12u, pst_wg->s32_fs);
    prv_store_le16(p_hdr + 16u, pst_wg->u16_channel_count);
    prv_store_le16(p_hdr + 18u, (uint16_t)pst_wg->u32_frame_samples);
    prv_store_le16(p_hdr + 20u, (uint16_t)pst_wg->u8_format);
    prv_store_le16(p_hdr + 22u, 0u);
    prv_store_le64(p_hdr + 24u, pst_wg->cfg.start_utc_ns);
    prv_store_f64_le(p_hdr + 32u, 1000000000.0 / (double)pst_wg->s32_fs);
    prv_store_f64_le(p_hdr + 40u, pst_wg->f64_u_scale);
    prv_store_f64_le(p_hdr + 48u, pst_wg->f64_i_scale);
}

typedef struct {
    FILE *p_file;
    uint32_t u32_bytes_per_sample;
    uint64_t u64_frame_count;
} prv_file_sink_ctx_t;

static int32_t prv_file_sink(const wg_frame_t *pst_frame, void *p_ctx)
{
    prv_file_sink_ctx_t *pst_ctx = (prv_file_sink_ctx_t *)p_ctx;
    unsigned char a_hdr[56];
    uint32_t u32_payload;

    u32_payload = pst_frame->frames * (uint32_t)pst_frame->channel_count
                * pst_ctx->u32_bytes_per_sample;

    (void)memset(a_hdr, 0, sizeof(a_hdr));
    (void)memcpy(a_hdr, "WGF1", 4u);
    prv_store_le16(a_hdr + 4u, 1u);
    prv_store_le16(a_hdr + 6u, 56u);
    prv_store_le64(a_hdr + 8u, pst_frame->frame_index);
    prv_store_le64(a_hdr + 16u, pst_frame->start_sample);
    prv_store_le64(a_hdr + 24u, pst_frame->start_utc_ns);
    prv_store_le64(a_hdr + 32u, pst_frame->end_utc_ns);
    prv_store_le32(a_hdr + 40u, pst_frame->frames);
    prv_store_le32(a_hdr + 44u, pst_frame->flags);
    prv_store_le16(a_hdr + 48u, pst_frame->channel_count);
    prv_store_le16(a_hdr + 50u, 0u);
    prv_store_le32(a_hdr + 52u, u32_payload);

    if (fwrite(a_hdr, 1u, sizeof(a_hdr), pst_ctx->p_file) != sizeof(a_hdr)) {
        return PRV_ERR_IO;
    }
    if (u32_payload > 0u
        && fwrite(pst_frame->samples, 1u, (size_t)u32_payload,
                  pst_ctx->p_file) != (size_t)u32_payload) {
        return PRV_ERR_IO;
    }
    pst_ctx->u64_frame_count++;
    return PRV_OK;
}

static int32_t prv_write_end_record(FILE *p_file, uint64_t u64_frames,
                                    uint64_t u64_samples, uint16_t u16_reason)
{
    unsigned char a_end[32];

    (void)memset(a_end, 0, sizeof(a_end));
    (void)memcpy(a_end, "WGE1", 4u);
    prv_store_le16(a_end + 4u, 1u);
    prv_store_le16(a_end + 6u, u16_reason);
    prv_store_le64(a_end + 8u, u64_frames);
    prv_store_le64(a_end + 16u, u64_samples);
    if (fwrite(a_end, 1u, sizeof(a_end), p_file) != sizeof(a_end)) {
        return PRV_ERR_IO;
    }
    return PRV_OK;
}

static void prv_json_string(FILE *p_file, const char *pc_str)
{
    const unsigned char *p_cur = (const unsigned char *)pc_str;

    (void)fputc('"', p_file);
    while (*p_cur != 0u) {
        unsigned char u8_c = *p_cur;

        if (u8_c == (unsigned char)'"' || u8_c == (unsigned char)'\\') {
            (void)fputc('\\', p_file);
            (void)fputc((int)u8_c, p_file);
        } else if (u8_c == (unsigned char)'\n') {
            (void)fputs("\\n", p_file);
        } else if (u8_c < 0x20u) {
            (void)fprintf(p_file, "\\u%04x", (unsigned)u8_c);
        } else {
            (void)fputc((int)u8_c, p_file);
        }
        p_cur++;
    }
    (void)fputc('"', p_file);
}

static const char *prv_format_name(uint8_t u8_format)
{
    if (u8_format == (uint8_t)WG_FMT_F32) {
        return "f32";
    }
    if (u8_format == (uint8_t)WG_FMT_I24) {
        return "i24";
    }
    return "f64";
}

static const char *prv_profile_name(uint8_t u8_profile)
{
    if (u8_profile == (uint8_t)WG_PROFILE_LINEAR) {
        return "linear";
    }
    if (u8_profile == (uint8_t)WG_PROFILE_TRIANGLE) {
        return "triangle";
    }
    return "const";
}

static const char *prv_modulation_name(uint8_t u8_type)
{
    if (u8_type == (uint8_t)WG_MOD_SINE) {
        return "sine";
    }
    if (u8_type == (uint8_t)WG_MOD_RECT) {
        return "rect";
    }
    return "none";
}

/* 幅值对象，键为已启用通道名，值为有效配置中的常数幅值（%）。 */
static void prv_json_amp_pct(FILE *p_file, const wg_config_t *pst_cfg,
                             const wg_signal_t *pst_sig)
{
    uint32_t u32_idx;

    (void)fputc('{', p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        prv_json_string(p_file, s_apc_channel_name[u8_ch]);
        (void)fprintf(p_file, ": %.17g", pst_sig->amp_pct[u8_ch].from);
    }
    (void)fputc('}', p_file);
}

static void prv_json_phase_deg(FILE *p_file, const wg_config_t *pst_cfg,
                               const wg_signal_t *pst_sig)
{
    uint32_t u32_idx;

    (void)fputc('{', p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        prv_json_string(p_file, s_apc_channel_name[u8_ch]);
        (void)fprintf(p_file, ": %.17g", pst_sig->phase_deg[u8_ch]);
    }
    (void)fputc('}', p_file);
}

/* 信号对象，字段与 docs/08 的 signal 结构对齐。 */
static void prv_json_signal(FILE *p_file, const wg_config_t *pst_cfg,
                            const wg_signal_t *pst_sig, const char *pc_indent)
{
    char a_inner[64];
    uint32_t u32_idx;

    (void)snprintf(a_inner, sizeof(a_inner), "%s  ", pc_indent);
    (void)fprintf(p_file, "{\n");
    (void)fprintf(p_file, "%s\"freq_hz\": %.17g,\n", a_inner,
                  pst_sig->frequency.from);
    (void)fprintf(p_file, "%s\"freq_profile\": \"%s\",\n", a_inner,
                  prv_profile_name(pst_sig->frequency.profile));
    (void)fprintf(p_file, "%s\"freq_to_hz\": %.17g,\n", a_inner,
                  pst_sig->frequency.to);
    (void)fprintf(p_file, "%s\"freq_period_s\": %.17g,\n", a_inner,
                  pst_sig->frequency.period_s);
    (void)fprintf(p_file, "%s\"amp_pct\": ", a_inner);
    prv_json_amp_pct(p_file, pst_cfg, pst_sig);
    (void)fprintf(p_file, ",\n%s\"phase_deg\": ", a_inner);
    prv_json_phase_deg(p_file, pst_cfg, pst_sig);

    (void)fprintf(p_file, ",\n%s\"harmonics\": [", a_inner);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_sig->harmonic_count; u32_idx++) {
        const wg_harmonic_t *pst_h = &pst_sig->harmonics[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        (void)fprintf(p_file,
                      "{\"order\": %u, \"amp_pct\": %.17g, \"phase_deg\": %.17g}",
                      (unsigned)pst_h->order, pst_h->amplitude_pct,
                      pst_h->phase_deg);
    }
    (void)fprintf(p_file, "],\n%s\"interharmonics\": [", a_inner);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_sig->interharmonic_count;
         u32_idx++) {
        const wg_interharmonic_t *pst_ih = &pst_sig->interharmonics[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        (void)fprintf(p_file,
                      "{\"order\": %.17g, \"amp_pct\": %.17g, \"phase_deg\": %.17g}",
                      pst_ih->order, pst_ih->amplitude_pct, pst_ih->phase_deg);
    }
    (void)fprintf(p_file, "],\n%s\"tones\": [", a_inner);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_sig->tone_count; u32_idx++) {
        const wg_tone_t *pst_tone = &pst_sig->tones[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        (void)fprintf(p_file,
                      "{\"frequency_hz\": %.17g, \"amp_pct\": %.17g, \"phase_deg\": %.17g}",
                      pst_tone->frequency_hz, pst_tone->amplitude_pct,
                      pst_tone->phase_deg);
    }
    (void)fprintf(p_file, "],\n%s\"modulation\": ", a_inner);
    if (pst_sig->modulation.type == (uint8_t)WG_MOD_NONE) {
        (void)fputs("null", p_file);
    } else {
        (void)fprintf(p_file,
                      "{\"type\": \"%s\", \"frequency_hz\": %.17g, "
                      "\"depth_pct\": %.17g, \"duty_pct\": %.17g, "
                      "\"phase_deg\": %.17g}",
                      prv_modulation_name(pst_sig->modulation.type),
                      pst_sig->modulation.frequency_hz,
                      pst_sig->modulation.depth_pct,
                      pst_sig->modulation.duty_pct,
                      pst_sig->modulation.phase_deg);
    }
    (void)fprintf(p_file, "\n%s}", pc_indent);
}

static const char *prv_component_kind_name(uint8_t u8_kind)
{
    if (u8_kind == (uint8_t)WG_COMP_HARMONIC) {
        return "harmonic";
    }
    if (u8_kind == (uint8_t)WG_COMP_INTERHARMONIC) {
        return "interharmonic";
    }
    if (u8_kind == (uint8_t)WG_COMP_TONE) {
        return "tone";
    }
    return "fundamental";
}

static const char *prv_modulator_kind_name(uint8_t u8_kind)
{
    switch (u8_kind) {
    case WG_MOD2_AMP_CONST:
        return "amp_const";
    case WG_MOD2_AMP_LINEAR:
        return "amp_linear";
    case WG_MOD2_AMP_TRIANGLE:
        return "amp_triangle";
    case WG_MOD2_AMP_STEP:
        return "amp_step";
    case WG_MOD2_AMP_SINE:
        return "amp_sine";
    case WG_MOD2_AMP_RECT:
        return "amp_rect";
    case WG_MOD2_FREQ_LINEAR:
        return "freq_linear";
    case WG_MOD2_FREQ_TRIANGLE:
        return "freq_triangle";
    case WG_MOD2_FREQ_STEP:
        return "freq_step";
    default:
        return "none";
    }
}

static const char *prv_event_kind_name(uint8_t u8_kind)
{
    return (u8_kind == (uint8_t)WG_EVENT_PHASE_STEP) ? "phase_step"
                                                     : "amp_step";
}

static void prv_json_modulator(FILE *p_file, const wg_modulator_t *pst_mod)
{
    (void)fputs("{\"kind\": ", p_file);
    prv_json_string(p_file, prv_modulator_kind_name(pst_mod->kind));

    switch (pst_mod->kind) {
    case WG_MOD2_AMP_CONST:
        (void)fprintf(p_file, ", \"from\": %.17g", pst_mod->from);
        break;
    case WG_MOD2_AMP_LINEAR:
        (void)fprintf(p_file, ", \"from\": %.17g, \"to\": %.17g, "
                      "\"duration_s\": %.17g", pst_mod->from, pst_mod->to,
                      pst_mod->duration_s);
        break;
    case WG_MOD2_AMP_TRIANGLE:
        (void)fprintf(p_file, ", \"from\": %.17g, \"to\": %.17g, "
                      "\"period_s\": %.17g", pst_mod->from, pst_mod->to,
                      pst_mod->period_s);
        break;
    case WG_MOD2_AMP_STEP:
        (void)fprintf(p_file, ", \"from\": %.17g, \"to\": %.17g, "
                      "\"start_s\": %.17g", pst_mod->from, pst_mod->to,
                      pst_mod->start_s);
        break;
    case WG_MOD2_AMP_SINE:
        (void)fprintf(p_file, ", \"frequency_hz\": %.17g, "
                      "\"depth_pct\": %.17g, \"phase_deg\": %.17g",
                      pst_mod->frequency_hz, pst_mod->depth_pct,
                      pst_mod->phase_deg);
        break;
    case WG_MOD2_AMP_RECT:
        (void)fprintf(p_file, ", \"frequency_hz\": %.17g, "
                      "\"depth_pct\": %.17g, \"duty_pct\": %.17g, "
                      "\"phase_deg\": %.17g", pst_mod->frequency_hz,
                      pst_mod->depth_pct, pst_mod->duty_pct,
                      pst_mod->phase_deg);
        break;
    case WG_MOD2_FREQ_LINEAR:
        (void)fprintf(p_file, ", \"from\": %.17g, \"to\": %.17g, "
                      "\"duration_s\": %.17g", pst_mod->from, pst_mod->to,
                      pst_mod->duration_s);
        break;
    case WG_MOD2_FREQ_TRIANGLE:
        (void)fprintf(p_file, ", \"from\": %.17g, \"to\": %.17g, "
                      "\"period_s\": %.17g", pst_mod->from, pst_mod->to,
                      pst_mod->period_s);
        break;
    case WG_MOD2_FREQ_STEP:
        (void)fprintf(p_file, ", \"from\": %.17g, \"to\": %.17g, "
                      "\"start_s\": %.17g", pst_mod->from, pst_mod->to,
                      pst_mod->start_s);
        break;
    default:
        break;
    }
    (void)fputc('}', p_file);
}

/* 已启用且被 channel_mask 选中的通道名，键为 JSON 数组。 */
static void prv_json_comp_channels(FILE *p_file, const wg_config_t *pst_cfg,
                                   const wg_component_t *pst_comp)
{
    uint32_t u32_idx;
    int first = 1;

    (void)fputc('[', p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        if (pst_comp->channel_mask != 0u
            && (pst_comp->channel_mask & (1u << u8_ch)) == 0u) {
            continue;
        }
        if (first == 0) {
            (void)fputs(", ", p_file);
        }
        prv_json_string(p_file, s_apc_channel_name[u8_ch]);
        first = 0;
    }
    (void)fputc(']', p_file);
}

static void prv_json_comp_amp(FILE *p_file, const wg_config_t *pst_cfg,
                              const wg_component_t *pst_comp)
{
    uint32_t u32_idx;

    (void)fputc('{', p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        prv_json_string(p_file, s_apc_channel_name[u8_ch]);
        (void)fprintf(p_file, ": %.17g", pst_comp->amp_pct[u8_ch]);
    }
    (void)fputc('}', p_file);
}

static void prv_json_comp_phase(FILE *p_file, const wg_config_t *pst_cfg,
                                const wg_component_t *pst_comp)
{
    uint32_t u32_idx;

    (void)fputc('{', p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        prv_json_string(p_file, s_apc_channel_name[u8_ch]);
        (void)fprintf(p_file, ": %.17g", pst_comp->phase_deg[u8_ch]);
    }
    (void)fputc('}', p_file);
}

static void prv_json_component(FILE *p_file, const wg_config_t *pst_cfg,
                               const wg_component_t *pst_comp)
{
    uint32_t u32_idx;

    (void)fputs("{\"kind\": ", p_file);
    prv_json_string(p_file, prv_component_kind_name(pst_comp->kind));
    if (pst_comp->kind == (uint8_t)WG_COMP_TONE) {
        (void)fprintf(p_file, ", \"frequency_hz\": %.17g",
                      pst_comp->frequency_hz);
    } else {
        (void)fprintf(p_file, ", \"order\": %.17g", pst_comp->order);
    }

    (void)fputs(", \"amp_pct\": ", p_file);
    prv_json_comp_amp(p_file, pst_cfg, pst_comp);
    (void)fputs(", \"phase_deg\": ", p_file);
    prv_json_comp_phase(p_file, pst_cfg, pst_comp);
    (void)fputs(", \"channels\": ", p_file);
    prv_json_comp_channels(p_file, pst_cfg, pst_comp);
    (void)fprintf(p_file, ", \"amplitude_ref\": \"%s\"",
                  (pst_comp->amplitude_ref != 0u) ? "fundamental" : "nominal");

    (void)fputs(", \"modulators\": [", p_file);
    for (u32_idx = 0u;
         u32_idx < (uint32_t)pst_comp->modulator_count; u32_idx++) {
        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        prv_json_modulator(p_file, &pst_comp->modulators[u32_idx]);
    }
    (void)fputs("]}", p_file);
}

/* program 对象，字段与 docs/08 §6.1 的 JSON 输入一致。 */
static void prv_json_program(FILE *p_file, const wg_config_t *pst_cfg)
{
    const wg_program_t *pst_prog = &pst_cfg->program;
    uint32_t u32_idx;

    (void)fputs("{\n    \"components\": [", p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_prog->component_count;
         u32_idx++) {
        if (u32_idx > 0u) {
            (void)fputc(',', p_file);
        }
        (void)fputs("\n      ", p_file);
        prv_json_component(p_file, pst_cfg, &pst_prog->components[u32_idx]);
    }
    if (pst_prog->component_count > 0u) {
        (void)fputs("\n    ", p_file);
    }
    (void)fputs("],\n    \"events\": [", p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_prog->event_count; u32_idx++) {
        const wg_event_t *pst_evt = &pst_prog->events[u32_idx];

        if (u32_idx > 0u) {
            (void)fputc(',', p_file);
        }
        (void)fputs("\n      {\"kind\": ", p_file);
        prv_json_string(p_file, prv_event_kind_name(pst_evt->kind));
        (void)fprintf(p_file, ", \"time_s\": %.17g, \"channel_mask\": %u, "
                      "\"value\": %.17g, \"duration_s\": %.17g}",
                      pst_evt->time_s, (unsigned)pst_evt->channel_mask,
                      pst_evt->value, pst_evt->duration_s);
    }
    if (pst_prog->event_count > 0u) {
        (void)fputs("\n    ", p_file);
    }
    (void)fputs("]\n  }", p_file);
}

static void prv_json_channel_delay(FILE *p_file, const wg_config_t *pst_cfg,
                                   const wg_segment_t *pst_seg)
{
    uint32_t u32_idx;

    (void)fputc('{', p_file);
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        if (u32_idx > 0u) {
            (void)fputs(", ", p_file);
        }
        prv_json_string(p_file, s_apc_channel_name[u8_ch]);
        (void)fprintf(p_file, ": %.17g", pst_seg->channel_delay_s[u8_ch]);
    }
    (void)fputc('}', p_file);
}

/* 段数组；无段时输出空数组。 */
static void prv_json_segments(FILE *p_file, const wg_config_t *pst_cfg)
{
    uint32_t u32_idx;

    (void)fprintf(p_file, "  \"segments\": [");
    if (pst_cfg->segment_count == 0u) {
        (void)fprintf(p_file, "],\n");
        return;
    }
    (void)fprintf(p_file, "\n");
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->segment_count; u32_idx++) {
        const wg_segment_t *pst_seg = &pst_cfg->segments[u32_idx];

        (void)fprintf(p_file, "    {\n");
        (void)fprintf(p_file, "      \"duration_s\": %.17g,\n",
                      pst_seg->duration_s);
        (void)fprintf(p_file, "      \"transition\": \"%s\",\n",
                      (pst_seg->transition == (uint8_t)WG_TRANS_ZERO_CROSSING)
                      ? "zero_crossing" : "immediate");
        (void)fprintf(p_file, "      \"channel_delay_s\": ");
        prv_json_channel_delay(p_file, pst_cfg, pst_seg);
        (void)fprintf(p_file, ",\n      \"signal\": ");
        prv_json_signal(p_file, pst_cfg, &pst_seg->signal, "      ");
        (void)fprintf(p_file, "\n    }%s\n",
                      (u32_idx + 1u < (uint32_t)pst_cfg->segment_count)
                      ? "," : "");
    }
    (void)fprintf(p_file, "  ],\n");
}

static int32_t prv_write_manifest(const wg_handle_t *pst_wg, const char *pc_output_path,
                                  const char *pc_sha256, uint64_t u64_frames)
{
    size_t z_len = strlen(pc_output_path);
    char *pc_manifest = (char *)malloc(z_len + sizeof(".manifest.json"));
    FILE *p_file;
    uint32_t u32_idx;
    int32_t s32_rc = PRV_OK;

    if (pc_manifest == NULL) {
        return PRV_ERR_ALLOC;
    }
    (void)memcpy(pc_manifest, pc_output_path, z_len);
    (void)memcpy(pc_manifest + z_len, ".manifest.json", sizeof(".manifest.json"));

    p_file = fopen(pc_manifest, "wb");
    if (p_file == NULL) {
        free(pc_manifest);
        return PRV_ERR_IO;
    }

    (void)fprintf(p_file, "{\n");
    (void)fprintf(p_file, "  \"generator_version\": ");
    prv_json_string(p_file, wg_version_string());
    (void)fprintf(p_file, ",\n");
    (void)fprintf(p_file, "  \"config\": {\n");
    (void)fprintf(p_file, "    \"duration_s\": %.17g,\n", pst_wg->cfg.duration_s);
    (void)fprintf(p_file, "    \"sample_rate_hz\": %u,\n", (unsigned)pst_wg->s32_fs);
    (void)fprintf(p_file, "    \"start_utc_ns\": %llu,\n",
                  (unsigned long long)pst_wg->cfg.start_utc_ns);
    (void)fprintf(p_file, "    \"segment_count\": %u,\n",
                  (unsigned)pst_wg->u32_segment_count);
    (void)fprintf(p_file, "    \"format\": \"%s\",\n", prv_format_name(pst_wg->u8_format));
    (void)fprintf(p_file, "    \"frame_samples\": %u,\n",
                  (unsigned)pst_wg->u32_frame_samples);
    (void)fprintf(p_file, "    \"channel_count\": %u,\n",
                  (unsigned)pst_wg->u16_channel_count);
    (void)fprintf(p_file, "    \"channels\": [");
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_wg->u16_channel_count; u32_idx++) {
        if (u32_idx > 0u) {
            (void)fprintf(p_file, ", ");
        }
        prv_json_string(p_file, s_apc_channel_name[pst_wg->cfg.channels[u32_idx]]);
    }
    (void)fprintf(p_file, "],\n");
    (void)fprintf(p_file, "    \"nominal\": [");
    for (u32_idx = 0u; u32_idx < (uint32_t)pst_wg->u16_channel_count; u32_idx++) {
        if (u32_idx > 0u) {
            (void)fprintf(p_file, ", ");
        }
        (void)fprintf(p_file, "%.17g",
                      pst_wg->cfg.nominal[pst_wg->cfg.channels[u32_idx]]);
    }
    (void)fprintf(p_file, "]\n");
    (void)fprintf(p_file, "  },\n");
    (void)fprintf(p_file, "  \"final_params\": {\n");
    (void)fprintf(p_file, "    \"sample_rate_hz\": %u,\n", (unsigned)pst_wg->s32_fs);
    (void)fprintf(p_file, "    \"start_utc_ns\": %llu,\n",
                  (unsigned long long)pst_wg->cfg.start_utc_ns);
    (void)fprintf(p_file, "    \"format\": \"%s\",\n", prv_format_name(pst_wg->u8_format));
    (void)fprintf(p_file, "    \"frame_samples\": %u,\n",
                  (unsigned)pst_wg->u32_frame_samples);
    (void)fprintf(p_file, "    \"channel_count\": %u\n",
                  (unsigned)pst_wg->u16_channel_count);
    (void)fprintf(p_file, "  },\n");
    (void)fprintf(p_file, "  \"signal\": ");
    prv_json_signal(p_file, &pst_wg->cfg, &pst_wg->cfg.signal, "  ");
    (void)fprintf(p_file, ",\n");
    if (pst_wg->cfg.program.component_count > 0u) {
        (void)fprintf(p_file, "  \"program\": ");
        prv_json_program(p_file, &pst_wg->cfg);
        (void)fprintf(p_file, ",\n");
    }
    prv_json_segments(p_file, &pst_wg->cfg);
    (void)fprintf(p_file, "  \"sample_rate_hz\": %u,\n", (unsigned)pst_wg->s32_fs);
    (void)fprintf(p_file, "  \"start_utc_ns\": %llu,\n",
                  (unsigned long long)pst_wg->cfg.start_utc_ns);
    (void)fprintf(p_file, "  \"output_file\": ");
    prv_json_string(p_file, pc_output_path);
    (void)fprintf(p_file, ",\n");
    (void)fprintf(p_file, "  \"output_sha256\": ");
    prv_json_string(p_file, pc_sha256);
    (void)fprintf(p_file, ",\n");
    (void)fprintf(p_file, "  \"sample_count\": %llu,\n",
                  (unsigned long long)pst_wg->u64_total_samples);
    (void)fprintf(p_file, "  \"total_samples\": %llu,\n",
                  (unsigned long long)pst_wg->u64_total_samples);
    (void)fprintf(p_file, "  \"total_frames\": %llu\n",
                  (unsigned long long)u64_frames);
    (void)fprintf(p_file, "}\n");

    if (ferror(p_file) != 0) {
        s32_rc = PRV_ERR_IO;
    }
    if (fclose(p_file) != 0) {
        s32_rc = PRV_ERR_IO;
    }
    free(pc_manifest);
    return s32_rc;
}

int32_t wg_run_file(wg_handle_t *pst_wg, const char *pc_path)
{
    prv_file_sink_ctx_t st_ctx;
    unsigned char a_hdr[64];
    FILE *p_file;
    int32_t s32_rc;

    if (pst_wg == NULL || pc_path == NULL) {
        return PRV_ERR_ARG;
    }

    p_file = fopen(pc_path, "wb");
    if (p_file == NULL) {
        return PRV_ERR_IO;
    }

    prv_build_file_header(pst_wg, a_hdr);
    if (fwrite(a_hdr, 1u, sizeof(a_hdr), p_file) != sizeof(a_hdr)) {
        (void)fclose(p_file);
        return PRV_ERR_IO;
    }

    st_ctx.p_file = p_file;
    st_ctx.u32_bytes_per_sample = pst_wg->u32_bytes_per_sample;
    st_ctx.u64_frame_count = 0u;

    s32_rc = prv_run_shared(pst_wg, prv_file_sink, &st_ctx);

    if (prv_write_end_record(p_file, st_ctx.u64_frame_count,
                             pst_wg->u64_total_samples,
                             (uint16_t)(s32_rc == PRV_OK ? 0u : 1u)) != PRV_OK) {
        s32_rc = PRV_ERR_IO;
    }

    if (ferror(p_file) != 0) {
        s32_rc = PRV_ERR_IO;
    }
    if (fclose(p_file) != 0) {
        s32_rc = PRV_ERR_IO;
    }

    if (s32_rc == PRV_OK) {
        char a_sha[65];

        if (prv_sha256_file(pc_path, a_sha) != PRV_OK) {
            return PRV_ERR_IO;
        }
        if (prv_write_manifest(pst_wg, pc_path, a_sha,
                               st_ctx.u64_frame_count) != PRV_OK) {
            return PRV_ERR_IO;
        }
    }

    return s32_rc;
}
