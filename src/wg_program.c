/*
 * wg_program.c - layered signal program: validation, builder and evaluation.
 *
 * The program (components + per-component modulator chains + time events) is
 * evaluated only when cfg.program.component_count > 0.  The legacy path in
 * wg_signal.c is untouched, so configurations without a program keep their
 * exact previous samples.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wg_config.h"
#include "wg_internal.h"

#define PRV2_AMP_PCT_MAX   1000.0
#define PRV2_ORDER_MAX     64.0

/* ------------------------------------------------------------------ */
/* builder                                                            */
/* ------------------------------------------------------------------ */

struct wg_builder {
    wg_config_t *pst_cfg;
};

static void prv2_set_err(char *pc_err, size_t z_err_len, const char *pc_msg)
{
    if (pc_err != NULL && z_err_len > 0u) {
        (void)snprintf(pc_err, z_err_len, "%s", pc_msg);
    }
}

static int32_t prv2_fail(char *pc_err, size_t z_err_len, const char *pc_field,
                         const char *pc_expect)
{
    if (pc_err != NULL && z_err_len > 0u) {
        (void)snprintf(pc_err, z_err_len, "%s: %s", pc_field, pc_expect);
    }
    return PRV_ERR_CONFIG;
}

static int prv2_is_finite(double f64_v)
{
    return isfinite(f64_v) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* validation                                                         */
/* ------------------------------------------------------------------ */

int32_t prv_component_validate(const wg_component_t *pst_comp, uint32_t u32_idx,
                               char *pc_err, size_t z_err_len)
{
    char a_path[96];
    uint32_t i;

    if (pst_comp->kind > (uint8_t)WG_COMP_TONE) {
        (void)snprintf(a_path, sizeof(a_path), "components[%u].kind",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected 0..3");
    }

    switch (pst_comp->kind) {
    case WG_COMP_FUNDAMENTAL:
        if (pst_comp->order != 1.0) {
            (void)snprintf(a_path, sizeof(a_path), "components[%u].order",
                           (unsigned)u32_idx);
            return prv2_fail(pc_err, z_err_len, a_path, "expected 1");
        }
        break;
    case WG_COMP_HARMONIC:
        if (!prv2_is_finite(pst_comp->order)
            || pst_comp->order != floor(pst_comp->order)
            || pst_comp->order < 2.0 || pst_comp->order > PRV2_ORDER_MAX) {
            (void)snprintf(a_path, sizeof(a_path), "components[%u].order",
                           (unsigned)u32_idx);
            return prv2_fail(pc_err, z_err_len, a_path,
                             "expected integer 2..64");
        }
        break;
    case WG_COMP_INTERHARMONIC:
        if (!prv2_is_finite(pst_comp->order) || !(pst_comp->order > 0.0)) {
            (void)snprintf(a_path, sizeof(a_path), "components[%u].order",
                           (unsigned)u32_idx);
            return prv2_fail(pc_err, z_err_len, a_path, "expected number > 0");
        }
        break;
    default: /* WG_COMP_TONE */
        if (!prv2_is_finite(pst_comp->frequency_hz)
            || !(pst_comp->frequency_hz > 0.0)) {
            (void)snprintf(a_path, sizeof(a_path), "components[%u].frequency_hz",
                           (unsigned)u32_idx);
            return prv2_fail(pc_err, z_err_len, a_path, "expected number > 0");
        }
        break;
    }

    if (pst_comp->kind != WG_COMP_TONE
        && (!prv2_is_finite(pst_comp->frequency_hz)
            || pst_comp->frequency_hz < 0.0)) {
        (void)snprintf(a_path, sizeof(a_path), "components[%u].frequency_hz",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected number >= 0");
    }

    for (i = 0u; i < (uint32_t)WG_MAX_CHANNELS; i++) {
        if (!prv2_is_finite(pst_comp->amp_pct[i])
            || pst_comp->amp_pct[i] < 0.0
            || pst_comp->amp_pct[i] > PRV2_AMP_PCT_MAX) {
            (void)snprintf(a_path, sizeof(a_path),
                           "components[%u].amp_pct[%u]", (unsigned)u32_idx,
                           (unsigned)i);
            return prv2_fail(pc_err, z_err_len, a_path,
                             "expected number in [0, 1000]");
        }
        if (!prv2_is_finite(pst_comp->phase_deg[i])) {
            (void)snprintf(a_path, sizeof(a_path),
                           "components[%u].phase_deg[%u]", (unsigned)u32_idx,
                           (unsigned)i);
            return prv2_fail(pc_err, z_err_len, a_path, "expected finite number");
        }
    }

    if ((pst_comp->channel_mask & 0x80u) != 0u) {
        (void)snprintf(a_path, sizeof(a_path), "components[%u].channel_mask",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected bits 0..6");
    }
    if (pst_comp->amplitude_ref > 1u) {
        (void)snprintf(a_path, sizeof(a_path), "components[%u].amplitude_ref",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected 0 or 1");
    }
    if (pst_comp->modulator_count > (uint8_t)WG_MAX_MODULATORS) {
        (void)snprintf(a_path, sizeof(a_path), "components[%u].modulators",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "too many entries");
    }
    for (i = 0u; i < (uint32_t)pst_comp->modulator_count; i++) {
        if (prv_modulator_validate(&pst_comp->modulators[i], pst_comp->kind,
                                   u32_idx, i, pc_err, z_err_len) != PRV_OK) {
            return PRV_ERR_CONFIG;
        }
    }
    return PRV_OK;
}

int32_t prv_modulator_validate(const wg_modulator_t *pst_mod, uint8_t u8_comp_kind,
                               uint32_t u32_comp_idx, uint32_t u32_mod_idx,
                               char *pc_err, size_t z_err_len)
{
    char a_path[128];
    int s32_freq_kind;

    if (pst_mod->kind > (uint8_t)WG_MOD2_FREQ_STEP) {
        (void)snprintf(a_path, sizeof(a_path),
                       "components[%u].modulators[%u].kind",
                       (unsigned)u32_comp_idx, (unsigned)u32_mod_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected 0..9");
    }

    s32_freq_kind = (pst_mod->kind == (uint8_t)WG_MOD2_FREQ_LINEAR
                  || pst_mod->kind == (uint8_t)WG_MOD2_FREQ_TRIANGLE
                  || pst_mod->kind == (uint8_t)WG_MOD2_FREQ_STEP) ? 1 : 0;

    if (s32_freq_kind != 0 && u8_comp_kind != (uint8_t)WG_COMP_FUNDAMENTAL) {
        (void)snprintf(a_path, sizeof(a_path),
                       "components[%u].modulators[%u].kind",
                       (unsigned)u32_comp_idx, (unsigned)u32_mod_idx);
        return prv2_fail(pc_err, z_err_len, a_path,
                         "FREQ_* only allowed on the fundamental component");
    }

#define PRV2_CHECK_NUM(field, cond, expect)                                  \
    do {                                                                     \
        if (!prv2_is_finite(pst_mod->field) || !(cond)) {                    \
            (void)snprintf(a_path, sizeof(a_path),                           \
                           "components[%u].modulators[%u]." #field,           \
                           (unsigned)u32_comp_idx, (unsigned)u32_mod_idx);   \
            return prv2_fail(pc_err, z_err_len, a_path, (expect));           \
        }                                                                    \
    } while (0)

    switch (pst_mod->kind) {
    case WG_MOD2_NONE:
        break;
    case WG_MOD2_AMP_CONST:
        PRV2_CHECK_NUM(from, pst_mod->from >= 0.0 && pst_mod->from <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        break;
    case WG_MOD2_AMP_LINEAR:
        PRV2_CHECK_NUM(from, pst_mod->from >= 0.0 && pst_mod->from <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        PRV2_CHECK_NUM(to, pst_mod->to >= 0.0 && pst_mod->to <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        PRV2_CHECK_NUM(duration_s, pst_mod->duration_s >= 0.0,
                       "expected number >= 0");
        break;
    case WG_MOD2_AMP_TRIANGLE:
        PRV2_CHECK_NUM(from, pst_mod->from >= 0.0 && pst_mod->from <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        PRV2_CHECK_NUM(to, pst_mod->to >= 0.0 && pst_mod->to <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        PRV2_CHECK_NUM(period_s, pst_mod->period_s >= 0.0,
                       "expected number >= 0");
        break;
    case WG_MOD2_AMP_STEP:
        PRV2_CHECK_NUM(from, pst_mod->from >= 0.0 && pst_mod->from <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        PRV2_CHECK_NUM(to, pst_mod->to >= 0.0 && pst_mod->to <= PRV2_AMP_PCT_MAX,
                       "expected number in [0, 1000]");
        PRV2_CHECK_NUM(start_s, pst_mod->start_s >= 0.0,
                       "expected number >= 0");
        break;
    case WG_MOD2_AMP_SINE:
        PRV2_CHECK_NUM(frequency_hz, pst_mod->frequency_hz >= 0.0,
                       "expected number >= 0");
        PRV2_CHECK_NUM(depth_pct, pst_mod->depth_pct >= 0.0,
                       "expected number >= 0");
        PRV2_CHECK_NUM(phase_deg, 1, "expected finite number");
        break;
    case WG_MOD2_AMP_RECT:
        PRV2_CHECK_NUM(frequency_hz, pst_mod->frequency_hz >= 0.0,
                       "expected number >= 0");
        PRV2_CHECK_NUM(depth_pct, pst_mod->depth_pct >= 0.0,
                       "expected number >= 0");
        PRV2_CHECK_NUM(duty_pct, pst_mod->duty_pct >= 0.0 && pst_mod->duty_pct <= 100.0,
                       "expected number in [0, 100]");
        PRV2_CHECK_NUM(phase_deg, 1, "expected finite number");
        break;
    case WG_MOD2_FREQ_LINEAR:
        PRV2_CHECK_NUM(from, pst_mod->from > 0.0, "expected number > 0");
        PRV2_CHECK_NUM(to, pst_mod->to > 0.0, "expected number > 0");
        PRV2_CHECK_NUM(duration_s, pst_mod->duration_s >= 0.0,
                       "expected number >= 0");
        break;
    case WG_MOD2_FREQ_TRIANGLE:
        PRV2_CHECK_NUM(from, pst_mod->from > 0.0, "expected number > 0");
        PRV2_CHECK_NUM(to, pst_mod->to > 0.0, "expected number > 0");
        PRV2_CHECK_NUM(period_s, pst_mod->period_s >= 0.0,
                       "expected number >= 0");
        break;
    case WG_MOD2_FREQ_STEP:
        PRV2_CHECK_NUM(from, pst_mod->from > 0.0, "expected number > 0");
        PRV2_CHECK_NUM(to, pst_mod->to > 0.0, "expected number > 0");
        PRV2_CHECK_NUM(start_s, pst_mod->start_s >= 0.0,
                       "expected number >= 0");
        break;
    default:
        break;
    }
#undef PRV2_CHECK_NUM

    return PRV_OK;
}

int32_t prv_event_validate(const wg_event_t *pst_event, uint32_t u32_idx,
                           double f64_duration, char *pc_err, size_t z_err_len)
{
    char a_path[96];

    if (pst_event->kind > (uint8_t)WG_EVENT_PHASE_STEP) {
        (void)snprintf(a_path, sizeof(a_path), "events[%u].kind",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected 0 or 1");
    }
    if ((pst_event->channel_mask & 0x80u) != 0u) {
        (void)snprintf(a_path, sizeof(a_path), "events[%u].channel_mask",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected bits 0..6");
    }
    if (!prv2_is_finite(pst_event->time_s)
        || pst_event->time_s < 0.0
        || pst_event->time_s > f64_duration) {
        (void)snprintf(a_path, sizeof(a_path), "events[%u].time_s",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path,
                         "expected number in [0, duration_s]");
    }
    if (!prv2_is_finite(pst_event->value)) {
        (void)snprintf(a_path, sizeof(a_path), "events[%u].value",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected finite number");
    }
    if (!prv2_is_finite(pst_event->duration_s) || pst_event->duration_s < 0.0) {
        (void)snprintf(a_path, sizeof(a_path), "events[%u].duration_s",
                       (unsigned)u32_idx);
        return prv2_fail(pc_err, z_err_len, a_path, "expected number >= 0");
    }
    return PRV_OK;
}

static int32_t prv2_count_freq_mods(const wg_program_t *pst_prog)
{
    uint32_t i;
    uint32_t k;
    int32_t s32_count = 0;

    for (i = 0u; i < (uint32_t)pst_prog->component_count; i++) {
        const wg_component_t *pst_comp = &pst_prog->components[i];

        for (k = 0u; k < (uint32_t)pst_comp->modulator_count; k++) {
            uint8_t u8_kind = pst_comp->modulators[k].kind;

            if (u8_kind == (uint8_t)WG_MOD2_FREQ_LINEAR
                || u8_kind == (uint8_t)WG_MOD2_FREQ_TRIANGLE
                || u8_kind == (uint8_t)WG_MOD2_FREQ_STEP) {
                s32_count++;
            }
        }
    }
    return s32_count;
}

int32_t prv_program_validate(const wg_program_t *pst_prog, double f64_duration,
                             char *pc_err, size_t z_err_len)
{
    uint32_t i;
    uint32_t u32_fundamentals = 0u;

    if (pst_prog->component_count > (uint16_t)WG_MAX_COMPONENTS) {
        prv2_set_err(pc_err, z_err_len, "program.components: too many entries");
        return PRV_ERR_CONFIG;
    }
    if (pst_prog->event_count > (uint16_t)WG_MAX_EVENTS) {
        prv2_set_err(pc_err, z_err_len, "program.events: too many entries");
        return PRV_ERR_CONFIG;
    }

    for (i = 0u; i < (uint32_t)pst_prog->component_count; i++) {
        if (prv_component_validate(&pst_prog->components[i], i,
                                   pc_err, z_err_len) != PRV_OK) {
            return PRV_ERR_CONFIG;
        }
        if (pst_prog->components[i].kind == (uint8_t)WG_COMP_FUNDAMENTAL) {
            u32_fundamentals++;
        }
    }
    if (u32_fundamentals == 0u) {
        prv2_set_err(pc_err, z_err_len,
                     "program.components: expected at least one fundamental");
        return PRV_ERR_CONFIG;
    }
    if (prv2_count_freq_mods(pst_prog) > 1) {
        prv2_set_err(pc_err, z_err_len,
                     "program.modulators: expected at most 1 FREQ_* entry");
        return PRV_ERR_CONFIG;
    }

    for (i = 0u; i < (uint32_t)pst_prog->event_count; i++) {
        if (prv_event_validate(&pst_prog->events[i], i, f64_duration,
                               pc_err, z_err_len) != PRV_OK) {
            return PRV_ERR_CONFIG;
        }
    }
    return PRV_OK;
}

/* ------------------------------------------------------------------ */
/* program append primitives                                          */
/* ------------------------------------------------------------------ */

int32_t wg_program_add_component(wg_program_t *pst_prog,
                                 const wg_component_t *pst_comp)
{
    if (pst_prog == NULL || pst_comp == NULL) {
        return PRV_ERR_ARG;
    }
    if (pst_prog->component_count >= (uint16_t)WG_MAX_COMPONENTS) {
        return PRV_ERR_CONFIG;
    }
    if (prv_component_validate(pst_comp,
                               (uint32_t)pst_prog->component_count,
                               NULL, 0u) != PRV_OK) {
        return PRV_ERR_CONFIG;
    }
    pst_prog->components[pst_prog->component_count] = *pst_comp;
    pst_prog->component_count++;
    return PRV_OK;
}

int32_t wg_program_add_modulator(wg_program_t *pst_prog, uint32_t u32_index,
                                 const wg_modulator_t *pst_mod)
{
    wg_component_t *pst_comp;
    uint8_t u8_kind;

    if (pst_prog == NULL || pst_mod == NULL) {
        return PRV_ERR_ARG;
    }
    if (u32_index >= (uint32_t)pst_prog->component_count) {
        return PRV_ERR_CONFIG;
    }
    pst_comp = &pst_prog->components[u32_index];
    if (pst_comp->modulator_count >= (uint8_t)WG_MAX_MODULATORS) {
        return PRV_ERR_CONFIG;
    }
    if (prv_modulator_validate(pst_mod, pst_comp->kind, u32_index,
                               (uint32_t)pst_comp->modulator_count,
                               NULL, 0u) != PRV_OK) {
        return PRV_ERR_CONFIG;
    }

    u8_kind = pst_mod->kind;
    if (u8_kind == (uint8_t)WG_MOD2_FREQ_LINEAR
        || u8_kind == (uint8_t)WG_MOD2_FREQ_TRIANGLE
        || u8_kind == (uint8_t)WG_MOD2_FREQ_STEP) {
        if (prv2_count_freq_mods(pst_prog) > 0) {
            return PRV_ERR_CONFIG;
        }
    }

    pst_comp->modulators[pst_comp->modulator_count] = *pst_mod;
    pst_comp->modulator_count++;
    return PRV_OK;
}

/*
 * 事件自身的字段在此校验；time_s 上界依赖配置总时长，由 builder 包装传入，
 * 公开原语不做该上界约束（wg_create 仍会按 duration_s 复检）。
 */
static int32_t prv2_program_add_event(wg_program_t *pst_prog,
                                      const wg_event_t *pst_event,
                                      double f64_max_time)
{
    if (pst_prog == NULL || pst_event == NULL) {
        return PRV_ERR_ARG;
    }
    if (pst_prog->event_count >= (uint16_t)WG_MAX_EVENTS) {
        return PRV_ERR_CONFIG;
    }
    if (prv_event_validate(pst_event, (uint32_t)pst_prog->event_count,
                           f64_max_time, NULL, 0u) != PRV_OK) {
        return PRV_ERR_CONFIG;
    }
    pst_prog->events[pst_prog->event_count] = *pst_event;
    pst_prog->event_count++;
    return PRV_OK;
}

int32_t wg_program_add_event(wg_program_t *pst_prog,
                             const wg_event_t *pst_event)
{
    return prv2_program_add_event(pst_prog, pst_event, HUGE_VAL);
}

/* ------------------------------------------------------------------ */
/* builder API                                                        */
/* ------------------------------------------------------------------ */

wg_builder_t *wg_builder_create(uint32_t u32_sample_rate_hz, uint64_t u64_start_utc_ns)
{
    wg_builder_t *pst_b;

    pst_b = (wg_builder_t *)calloc(1u, sizeof(*pst_b));
    if (pst_b == NULL) {
        return NULL;
    }
    pst_b->pst_cfg = (wg_config_t *)calloc(1u, sizeof(*pst_b->pst_cfg));
    if (pst_b->pst_cfg == NULL) {
        free(pst_b);
        return NULL;
    }
    wg_config_default(pst_b->pst_cfg);
    pst_b->pst_cfg->sample_rate_hz = u32_sample_rate_hz;
    pst_b->pst_cfg->start_utc_ns = u64_start_utc_ns;
    return pst_b;
}

void wg_builder_destroy(wg_builder_t *pst_b)
{
    if (pst_b == NULL) {
        return;
    }
    free(pst_b->pst_cfg);
    free(pst_b);
}

int32_t wg_builder_set_channels(wg_builder_t *pst_b, const uint8_t *pu8_ch,
                                uint8_t u8_count, const double *pf64_nominal)
{
    uint8_t i;
    uint8_t j;

    if (pst_b == NULL || pu8_ch == NULL || pf64_nominal == NULL) {
        return PRV_ERR_ARG;
    }
    if (u8_count == 0u || u8_count > (uint8_t)WG_MAX_CHANNELS) {
        return PRV_ERR_CONFIG;
    }
    for (i = 0u; i < u8_count; i++) {
        if (pu8_ch[i] >= (uint8_t)WG_MAX_CHANNELS) {
            return PRV_ERR_CONFIG;
        }
        if (!prv2_is_finite(pf64_nominal[i]) || !(pf64_nominal[i] > 0.0)) {
            return PRV_ERR_CONFIG;
        }
        for (j = 0u; j < i; j++) {
            if (pu8_ch[j] == pu8_ch[i]) {
                return PRV_ERR_CONFIG;
            }
        }
    }

    memset(pst_b->pst_cfg->channels, 0, sizeof(pst_b->pst_cfg->channels));
    for (i = 0u; i < u8_count; i++) {
        pst_b->pst_cfg->channels[i] = pu8_ch[i];
        pst_b->pst_cfg->nominal[pu8_ch[i]] = pf64_nominal[i];
    }
    pst_b->pst_cfg->channel_count = u8_count;
    return PRV_OK;
}

int32_t wg_builder_set_duration(wg_builder_t *pst_b, double f64_duration_s)
{
    if (pst_b == NULL) {
        return PRV_ERR_ARG;
    }
    if (!prv2_is_finite(f64_duration_s) || !(f64_duration_s > 0.0)) {
        return PRV_ERR_CONFIG;
    }
    pst_b->pst_cfg->duration_s = f64_duration_s;
    return PRV_OK;
}

int32_t wg_builder_set_output(wg_builder_t *pst_b, uint8_t u8_format,
                              uint32_t u32_frame_samples)
{
    if (pst_b == NULL) {
        return PRV_ERR_ARG;
    }
    if (u8_format > (uint8_t)WG_FMT_I24) {
        return PRV_ERR_CONFIG;
    }
    if (u32_frame_samples == 0u || u32_frame_samples > 65535u) {
        return PRV_ERR_CONFIG;
    }
    pst_b->pst_cfg->format = u8_format;
    pst_b->pst_cfg->frame_samples = u32_frame_samples;
    return PRV_OK;
}

int32_t wg_builder_set_frequency(wg_builder_t *pst_b,
                                 const wg_profile_f64_t *pst_prof)
{
    if (pst_b == NULL || pst_prof == NULL) {
        return PRV_ERR_ARG;
    }
    if (pst_prof->profile > (uint8_t)WG_PROFILE_TRIANGLE
        || !prv2_is_finite(pst_prof->from)
        || !prv2_is_finite(pst_prof->to)
        || !prv2_is_finite(pst_prof->period_s)) {
        return PRV_ERR_CONFIG;
    }
    pst_b->pst_cfg->signal.frequency = *pst_prof;
    return PRV_OK;
}

int32_t wg_builder_add_component(wg_builder_t *pst_b,
                                 const wg_component_t *pst_comp)
{
    if (pst_b == NULL) {
        return PRV_ERR_ARG;
    }
    return wg_program_add_component(&pst_b->pst_cfg->program, pst_comp);
}

int32_t wg_builder_add_modulator(wg_builder_t *pst_b,
                                 uint32_t u32_component_index,
                                 const wg_modulator_t *pst_mod)
{
    if (pst_b == NULL) {
        return PRV_ERR_ARG;
    }
    return wg_program_add_modulator(&pst_b->pst_cfg->program,
                                    u32_component_index, pst_mod);
}

int32_t wg_builder_add_event(wg_builder_t *pst_b, const wg_event_t *pst_event)
{
    if (pst_b == NULL) {
        return PRV_ERR_ARG;
    }
    return prv2_program_add_event(&pst_b->pst_cfg->program, pst_event,
                                  pst_b->pst_cfg->duration_s);
}

wg_handle_t *wg_create_from_builder(wg_builder_t *pst_b, char *pc_err,
                                    size_t err_len)
{
    if (pst_b == NULL) {
        prv2_set_err(pc_err, err_len, "builder: null pointer");
        return NULL;
    }
    return wg_create(pst_b->pst_cfg, pc_err, err_len);
}

/* ------------------------------------------------------------------ */
/* evaluation                                                         */
/* ------------------------------------------------------------------ */

static double prv2_clamp01(double f64_v)
{
    if (f64_v < 0.0) {
        return 0.0;
    }
    if (f64_v > 1.0) {
        return 1.0;
    }
    return f64_v;
}

/* Triangle with period_s as the full cycle, starting at `from`. */
static double prv2_triangle(double f64_from, double f64_to, double f64_period,
                            double f64_t)
{
    double f64_pos;
    double f64_half;
    double f64_ratio;

    if (!(f64_period > 0.0)) {
        return f64_from;
    }
    f64_pos = fmod(f64_t, f64_period);
    if (f64_pos < 0.0) {
        f64_pos += f64_period;
    }
    f64_half = f64_period * 0.5;
    if (f64_pos <= f64_half) {
        f64_ratio = f64_pos / f64_half;
    } else {
        f64_ratio = (f64_period - f64_pos) / f64_half;
    }
    return f64_from + (f64_to - f64_from) * f64_ratio;
}

static double prv2_amp_mod_factor(const wg_modulator_t *pst_mod, double f64_t)
{
    double f64_depth;

    switch (pst_mod->kind) {
    case WG_MOD2_AMP_CONST:
        return pst_mod->from / 100.0;
    case WG_MOD2_AMP_LINEAR:
        if (!(pst_mod->duration_s > 0.0)) {
            return pst_mod->from / 100.0;
        }
        return (pst_mod->from
                + (pst_mod->to - pst_mod->from)
                  * prv2_clamp01(f64_t / pst_mod->duration_s)) / 100.0;
    case WG_MOD2_AMP_TRIANGLE:
        return prv2_triangle(pst_mod->from, pst_mod->to, pst_mod->period_s,
                             f64_t) / 100.0;
    case WG_MOD2_AMP_STEP:
        return (f64_t >= pst_mod->start_s ? pst_mod->to : pst_mod->from) / 100.0;
    case WG_MOD2_AMP_SINE:
        f64_depth = pst_mod->depth_pct / 100.0;
        return 1.0 + f64_depth * cos(PRV_TWO_PI * pst_mod->frequency_hz * f64_t
                                     + pst_mod->phase_deg * PRV_DEG2RAD);
    case WG_MOD2_AMP_RECT: {
        double f64_duty;
        double f64_pos;

        if (!(pst_mod->frequency_hz > 0.0)) {
            return 1.0;
        }
        f64_duty = prv2_clamp01(pst_mod->duty_pct / 100.0);
        f64_pos = fmod(pst_mod->frequency_hz * f64_t
                       + pst_mod->phase_deg / 360.0, 1.0);
        if (f64_pos < 0.0) {
            f64_pos += 1.0;
        }
        f64_depth = pst_mod->depth_pct / 200.0;
        return (f64_pos < f64_duty) ? (1.0 + f64_depth) : (1.0 - f64_depth);
    }
    default:
        return 1.0;
    }
}

static double prv2_amp_chain(const wg_component_t *pst_comp, double f64_t)
{
    double f64_factor = 1.0;
    uint32_t i;

    for (i = 0u; i < (uint32_t)pst_comp->modulator_count; i++) {
        f64_factor *= prv2_amp_mod_factor(&pst_comp->modulators[i], f64_t);
    }
    return f64_factor;
}

static int prv2_freq_mod_value(const wg_modulator_t *pst_mod, double f64_t,
                               double *pf64_out)
{
    switch (pst_mod->kind) {
    case WG_MOD2_FREQ_LINEAR:
        if (!(pst_mod->duration_s > 0.0)) {
            *pf64_out = pst_mod->from;
        } else {
            *pf64_out = pst_mod->from
                      + (pst_mod->to - pst_mod->from)
                        * prv2_clamp01(f64_t / pst_mod->duration_s);
        }
        return 1;
    case WG_MOD2_FREQ_TRIANGLE:
        *pf64_out = prv2_triangle(pst_mod->from, pst_mod->to,
                                  pst_mod->period_s, f64_t);
        return 1;
    case WG_MOD2_FREQ_STEP:
        *pf64_out = (f64_t >= pst_mod->start_s) ? pst_mod->to : pst_mod->from;
        return 1;
    default:
        return 0;
    }
}

static int prv2_profile_active(const wg_profile_f64_t *pst_prof)
{
    if (pst_prof->profile == (uint8_t)WG_PROFILE_CONST) {
        return (pst_prof->from != 0.0) ? 1 : 0;
    }
    return (pst_prof->from != 0.0 || pst_prof->to != 0.0) ? 1 : 0;
}

static const wg_component_t *prv2_fundamental(const wg_program_t *pst_prog)
{
    uint32_t i;

    for (i = 0u; i < (uint32_t)pst_prog->component_count; i++) {
        if (pst_prog->components[i].kind == (uint8_t)WG_COMP_FUNDAMENTAL) {
            return &pst_prog->components[i];
        }
    }
    return NULL;
}

/*
 * Effective fundamental frequency for phase integration.
 * Segment frequency (non-zero) wins, then the program FREQ_* profile, then
 * cfg.signal.frequency.
 */
double prv_program_freq_at(const wg_handle_t *pst_wg, const wg_signal_t *pst_sig,
                           double f64_t_abs, double f64_t_local,
                           double f64_duration)
{
    const wg_program_t *pst_prog = &pst_wg->cfg.program;
    const wg_component_t *pst_fund;
    uint32_t i;

    if (pst_wg->cfg.segment_count > 0u
        && prv2_profile_active(&pst_sig->frequency) != 0) {
        return prv_profile_eval(&pst_sig->frequency, f64_t_local, f64_duration);
    }

    pst_fund = prv2_fundamental(pst_prog);
    if (pst_fund != NULL) {
        for (i = 0u; i < (uint32_t)pst_fund->modulator_count; i++) {
            double f64_value = 0.0;

            if (prv2_freq_mod_value(&pst_fund->modulators[i], f64_t_abs,
                                    &f64_value) != 0) {
                return f64_value;
            }
        }
    }

    return prv_profile_eval(&pst_wg->cfg.signal.frequency, f64_t_local,
                            f64_duration);
}

/* Fundamental instantaneous RMS amplitude for one channel. */
static double prv2_fund_rms(const wg_handle_t *pst_wg, uint8_t u8_ch,
                            double f64_t_abs, int s32_has_seg_amp,
                            double f64_seg_amp_pct)
{
    const wg_component_t *pst_fund =
        prv2_fundamental(&pst_wg->cfg.program);
    double f64_pct;

    if (pst_fund == NULL) {
        return 0.0;
    }
    f64_pct = pst_fund->amp_pct[u8_ch];
    if (s32_has_seg_amp != 0 && f64_seg_amp_pct != 0.0) {
        f64_pct = f64_seg_amp_pct;
    }
    if (pst_wg->au8_prog_amp_set[u8_ch] != 0u) {
        f64_pct = pst_wg->af64_prog_amp_pct[u8_ch];
    }
    return pst_wg->cfg.nominal[u8_ch] * f64_pct / 100.0
         * prv2_amp_chain(pst_fund, f64_t_abs);
}

static int prv2_channel_in_mask(uint8_t u8_mask, uint8_t u8_ch)
{
    if (u8_mask == 0u) {
        return 1;
    }
    return (((uint32_t)u8_mask >> u8_ch) & 1u) != 0u ? 1 : 0;
}

/*
 * Sum of all program components for one channel.
 * Each component contributes A_rms * sqrt(2) * cos(phi_component).
 */
double prv_program_channel_value(const wg_handle_t *pst_wg, uint8_t u8_ch,
                                 double f64_phase_eff, double f64_t_abs,
                                 int s32_has_seg_amp, double f64_seg_amp_pct)
{
    const wg_program_t *pst_prog = &pst_wg->cfg.program;
    double f64_fund_rms;
    double f64_sum = 0.0;
    uint32_t i;

    f64_fund_rms = prv2_fund_rms(pst_wg, u8_ch, f64_t_abs,
                                 s32_has_seg_amp, f64_seg_amp_pct);

    for (i = 0u; i < (uint32_t)pst_prog->component_count; i++) {
        const wg_component_t *pst_comp = &pst_prog->components[i];
        double f64_rms;
        double f64_arg;

        if (prv2_channel_in_mask(pst_comp->channel_mask, u8_ch) == 0) {
            continue;
        }

        if (pst_comp->kind == (uint8_t)WG_COMP_FUNDAMENTAL) {
            f64_rms = f64_fund_rms;
            f64_arg = f64_phase_eff + pst_comp->phase_deg[u8_ch] * PRV_DEG2RAD;
        } else if (pst_comp->kind == (uint8_t)WG_COMP_HARMONIC) {
            if (pst_comp->amplitude_ref != 0u) {
                f64_rms = f64_fund_rms * pst_comp->amp_pct[u8_ch] / 100.0;
            } else {
                f64_rms = pst_wg->cfg.nominal[u8_ch]
                        * pst_comp->amp_pct[u8_ch] / 100.0;
            }
            f64_rms *= prv2_amp_chain(pst_comp, f64_t_abs);
            f64_arg = pst_comp->order * f64_phase_eff
                    + pst_comp->phase_deg[u8_ch] * PRV_DEG2RAD;
        } else if (pst_comp->kind == (uint8_t)WG_COMP_INTERHARMONIC) {
            f64_rms = pst_wg->cfg.nominal[u8_ch]
                    * pst_comp->amp_pct[u8_ch] / 100.0
                    * prv2_amp_chain(pst_comp, f64_t_abs);
            f64_arg = pst_comp->order * f64_phase_eff
                    + pst_comp->phase_deg[u8_ch] * PRV_DEG2RAD;
        } else { /* WG_COMP_TONE */
            f64_rms = pst_wg->cfg.nominal[u8_ch]
                    * pst_comp->amp_pct[u8_ch] / 100.0
                    * prv2_amp_chain(pst_comp, f64_t_abs);
            f64_arg = PRV_TWO_PI * pst_comp->frequency_hz * f64_t_abs
                    + pst_comp->phase_deg[u8_ch] * PRV_DEG2RAD;
        }

        f64_sum += f64_rms * PRV_SQRT2 * cos(f64_arg);
    }
    return f64_sum;
}

/* Fundamental cosine used by the zero-crossing segment transition. */
double prv_program_fund_cos(const wg_handle_t *pst_wg, uint8_t u8_ch,
                            double f64_phase_eff)
{
    const wg_component_t *pst_fund =
        prv2_fundamental(&pst_wg->cfg.program);
    double f64_deg = (pst_fund != NULL) ? pst_fund->phase_deg[u8_ch]
                                        : pst_wg->cfg.signal.phase_deg[u8_ch];

    return cos(f64_phase_eff + f64_deg * PRV_DEG2RAD);
}

/* Apply every event whose time_s has been reached (events are sorted). */
void prv_program_advance_events(wg_handle_t *pst_wg, double f64_t_abs)
{
    const wg_program_t *pst_prog = &pst_wg->cfg.program;

    while (pst_wg->u16_evt_next < pst_prog->event_count) {
        const wg_event_t *pst_evt = &pst_prog->events[pst_wg->u16_evt_next];
        uint32_t i;

        if (!(f64_t_abs >= pst_evt->time_s)) {
            break;
        }

        if (pst_evt->kind == (uint8_t)WG_EVENT_PHASE_STEP) {
            pst_wg->f64_phase_offset += pst_evt->value * PRV_DEG2RAD;
        } else {
            for (i = 0u; i < (uint32_t)pst_wg->cfg.channel_count; i++) {
                uint8_t u8_ch = pst_wg->cfg.channels[i];

                if (prv2_channel_in_mask(pst_evt->channel_mask, u8_ch) == 0) {
                    continue;
                }
                pst_wg->au8_prog_amp_set[u8_ch] = 1u;
                pst_wg->af64_prog_amp_pct[u8_ch] = pst_evt->value;
            }
        }
        pst_wg->u16_evt_next++;
    }
}
