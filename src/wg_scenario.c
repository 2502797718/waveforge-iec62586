/*
 * wg_scenario.c - table-driven IEC 62586 test-scenario presets.
 *
 * Presets are built only from the layered signal primitives (components,
 * per-component modulators and time events).  Every numeric value is a
 * default that the caller may override; scenarios whose standard default is
 * not available report "NO_DEFAULT: <field path>" instead of inventing one.
 */
#include "wg_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* builders                                                           */
/* ------------------------------------------------------------------ */

/*
 * 旧 wg_signal_t 字段仍然填充：segments 用 signal.amp_pct 作为基波幅值
 * 覆盖，signal.frequency 也是基波频率的默认轮廓；谐波/间谐波/载波/调制
 * 一律写入 program，旧数组不再参与波形生成。
 */

/* 三相相位：通道号 mod 3 依次为 0 / -120 / +120 度。 */
static double ch_phase_deg(int i)
{
    if (i % 3 == 1) {
        return -120.0;
    }
    if (i % 3 == 2) {
        return 120.0;
    }
    return 0.0;
}

/* 通过公开原语追加分量；成功返回新分量下标，失败返回 -1。 */
static int32_t comp_append(wg_config_t *pst_cfg,
                           const wg_component_t *pst_comp)
{
    if (wg_program_add_component(&pst_cfg->program, pst_comp) != 0) {
        return -1;
    }
    return (int32_t)(pst_cfg->program.component_count - 1u);
}

/* 基波分量固定在 components[0]。 */
static wg_component_t *fund_comp(wg_config_t *pst_cfg)
{
    if (pst_cfg->program.component_count == 0u) {
        return NULL;
    }
    return &pst_cfg->program.components[0];
}

/* 通过公开原语在指定分量上追加一个三角包络调制器。 */
static void comp_triangle(wg_config_t *pst_cfg, int32_t s32_idx, double from,
                          double to, double period_s)
{
    wg_modulator_t st_m;

    if (s32_idx < 0
        || (uint32_t)s32_idx >= (uint32_t)pst_cfg->program.component_count) {
        return;
    }
    memset(&st_m, 0, sizeof(st_m));
    st_m.kind = (uint8_t)WG_MOD2_AMP_TRIANGLE;
    st_m.from = from;
    st_m.to = to;
    st_m.period_s = period_s;
    (void)wg_program_add_modulator(&pst_cfg->program, (uint32_t)s32_idx, &st_m);
}

/* 清零 signal/program，并添加基波 100 %、0/-120/+120 度。 */
static void sig_init(wg_config_t *pst_cfg, double f_hz)
{
    wg_component_t st_fund;
    int i;

    memset(&pst_cfg->signal, 0, sizeof(pst_cfg->signal));
    pst_cfg->signal.frequency.profile = WG_PROFILE_CONST;
    pst_cfg->signal.frequency.from = f_hz;
    pst_cfg->signal.frequency.to = f_hz;
    pst_cfg->signal.frequency.period_s = 0.0;
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        pst_cfg->signal.amp_pct[i].profile = WG_PROFILE_CONST;
        pst_cfg->signal.amp_pct[i].from = 100.0;
        pst_cfg->signal.amp_pct[i].to = 100.0;
        pst_cfg->signal.amp_pct[i].period_s = 0.0;
        pst_cfg->signal.phase_deg[i] = ch_phase_deg(i);
    }
    pst_cfg->signal.modulation.type = (uint8_t)WG_MOD_NONE;
    pst_cfg->signal.modulation.duty_pct = 50.0;

    memset(&pst_cfg->program, 0, sizeof(pst_cfg->program));
    memset(&st_fund, 0, sizeof(st_fund));
    st_fund.kind = (uint8_t)WG_COMP_FUNDAMENTAL;
    st_fund.order = 1.0;
    st_fund.amplitude_ref = 0u;
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        st_fund.amp_pct[i] = 100.0;
        st_fund.phase_deg[i] = ch_phase_deg(i);
    }
    (void)wg_program_add_component(&pst_cfg->program, &st_fund);
}

/* 更新基波幅值（所有通道）；signal 副本同步，供 segments 读取。 */
static void amp_all(wg_config_t *pst_cfg, double pct)
{
    wg_component_t *pst_fund = fund_comp(pst_cfg);
    int i;

    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        pst_cfg->signal.amp_pct[i].profile = WG_PROFILE_CONST;
        pst_cfg->signal.amp_pct[i].from = pct;
        pst_cfg->signal.amp_pct[i].to = pct;
        pst_cfg->signal.amp_pct[i].period_s = 0.0;
        if (pst_fund != NULL) {
            pst_fund->amp_pct[i] = pct;
        }
    }
}

/* 基波幅值三角包络（如 10-80 %，period_s 为完整周期）。 */
static void amp_triangle_all(wg_config_t *pst_cfg, double from, double to,
                             double period_s)
{
    comp_triangle(pst_cfg, 0, from, to, period_s);
}

static void amp_sine_all(wg_config_t *pst_cfg, double fm_hz, double depth_pct)
{
    wg_modulator_t st_m;

    memset(&st_m, 0, sizeof(st_m));
    st_m.kind = (uint8_t)WG_MOD2_AMP_SINE;
    st_m.frequency_hz = fm_hz;
    st_m.depth_pct = depth_pct;
    (void)wg_program_add_modulator(&pst_cfg->program, 0u, &st_m);
}

static void amp_rect_all(wg_config_t *pst_cfg, double fm_hz, double depth_pct,
                         double duty_pct)
{
    wg_modulator_t st_m;

    memset(&st_m, 0, sizeof(st_m));
    st_m.kind = (uint8_t)WG_MOD2_AMP_RECT;
    st_m.frequency_hz = fm_hz;
    st_m.depth_pct = depth_pct;
    st_m.duty_pct = duty_pct;
    (void)wg_program_add_modulator(&pst_cfg->program, 0u, &st_m);
}

/* 段场景的基波频率轮廓（也作为 program 的频率回退）。 */
static void set_freq(wg_config_t *pst_cfg, double f_hz)
{
    pst_cfg->signal.frequency.profile = WG_PROFILE_CONST;
    pst_cfg->signal.frequency.from = f_hz;
    pst_cfg->signal.frequency.to = f_hz;
    pst_cfg->signal.frequency.period_s = 0.0;
}

/* 谐波分量（相对基波）；逐通道相位叠加基波相位移。 */
static int32_t add_h(wg_config_t *pst_cfg, uint32_t order, double amp,
                     double phase)
{
    const wg_component_t *pst_fund = fund_comp(pst_cfg);
    wg_component_t st_c;
    int32_t s32_idx;
    int i;

    memset(&st_c, 0, sizeof(st_c));
    st_c.kind = (uint8_t)WG_COMP_HARMONIC;
    st_c.order = (double)order;
    st_c.amplitude_ref = 1u;
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        double f64_ch = (pst_fund != NULL) ? pst_fund->phase_deg[i]
                                           : ch_phase_deg(i);

        st_c.amp_pct[i] = amp;
        st_c.phase_deg[i] = phase + f64_ch;
    }
    s32_idx = comp_append(pst_cfg, &st_c);
    if (s32_idx < 0) {
        return -1;
    }
    /* 旧数组只用于 manifest/向后兼容，不参与采样。 */
    if (pst_cfg->signal.harmonic_count < (uint16_t)WG_MAX_HARMONICS) {
        wg_harmonic_t *pst_h =
            &pst_cfg->signal.harmonics[pst_cfg->signal.harmonic_count];

        pst_h->order = order;
        pst_h->amplitude_pct = amp;
        pst_h->phase_deg = phase;
        pst_cfg->signal.harmonic_count += 1u;
    }
    return s32_idx;
}

/* 间谐波分量（相对通道 nominal）。 */
static int32_t add_ih(wg_config_t *pst_cfg, double order, double amp,
                      double phase)
{
    wg_component_t st_c;
    int32_t s32_idx;
    int i;

    memset(&st_c, 0, sizeof(st_c));
    st_c.kind = (uint8_t)WG_COMP_INTERHARMONIC;
    st_c.order = order;
    st_c.amplitude_ref = 0u;
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        st_c.amp_pct[i] = amp;
        st_c.phase_deg[i] = phase;
    }
    s32_idx = comp_append(pst_cfg, &st_c);
    if (s32_idx < 0) {
        return -1;
    }
    if (pst_cfg->signal.interharmonic_count
        < (uint16_t)WG_MAX_INTERHARMONICS) {
        wg_interharmonic_t *pst_ih =
            &pst_cfg->signal.interharmonics[pst_cfg->signal.interharmonic_count];

        pst_ih->order = order;
        pst_ih->amplitude_pct = amp;
        pst_ih->phase_deg = phase;
        pst_cfg->signal.interharmonic_count += 1u;
    }
    return s32_idx;
}

/* 独立载波频点（相对通道 nominal）。 */
static int32_t add_tone(wg_config_t *pst_cfg, double freq, double amp,
                        double phase)
{
    wg_component_t st_c;
    int32_t s32_idx;
    int i;

    memset(&st_c, 0, sizeof(st_c));
    st_c.kind = (uint8_t)WG_COMP_TONE;
    st_c.frequency_hz = freq;
    st_c.amplitude_ref = 0u;
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        st_c.amp_pct[i] = amp;
        st_c.phase_deg[i] = phase;
    }
    s32_idx = comp_append(pst_cfg, &st_c);
    if (s32_idx < 0) {
        return -1;
    }
    if (pst_cfg->signal.tone_count < (uint16_t)WG_MAX_TONES) {
        wg_tone_t *pst_t = &pst_cfg->signal.tones[pst_cfg->signal.tone_count];

        pst_t->frequency_hz = freq;
        pst_t->amplitude_pct = amp;
        pst_t->phase_deg = phase;
        pst_cfg->signal.tone_count += 1u;
    }
    return s32_idx;
}

/* Table 4 S1 voltage harmonic group (crest factor 2). */
static void add_s1(wg_config_t *pst_cfg)
{
    (void)add_h(pst_cfg, 3u, 10.0, 180.0);
    (void)add_h(pst_cfg, 7u, 10.0, 180.0);
    (void)add_h(pst_cfg, 11u, 10.0, 180.0);
    (void)add_h(pst_cfg, 15u, 4.0, 180.0);
    (void)add_h(pst_cfg, 19u, 5.0, 180.0);
    (void)add_h(pst_cfg, 23u, 5.0, 180.0);
}

static void seg_put(wg_config_t *pst_cfg, const wg_signal_t *pst_sig,
                    double dur_s, uint8_t transition, const double *p_delay)
{
    wg_segment_t *pst_seg;
    int i;

    if (pst_cfg->segment_count >= (uint16_t)WG_MAX_SEGMENTS) {
        return;
    }
    pst_seg = &pst_cfg->segments[pst_cfg->segment_count];
    memset(pst_seg, 0, sizeof(*pst_seg));
    pst_seg->duration_s = dur_s;
    pst_seg->transition = transition;
    pst_seg->signal = *pst_sig;
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        pst_seg->channel_delay_s[i] = (p_delay != NULL) ? p_delay[i] : 0.0;
    }
    pst_cfg->segment_count += 1u;
}

/* 段副本上的旧 amp_pct；不触碰 program（program 只在 sig_init/amp_* 改）。 */
static void seg_amp_all(wg_signal_t *pst_sig, double pct)
{
    int i;

    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        pst_sig->amp_pct[i].profile = WG_PROFILE_CONST;
        pst_sig->amp_pct[i].from = pct;
        pst_sig->amp_pct[i].to = pct;
        pst_sig->amp_pct[i].period_s = 0.0;
    }
}

/* Add a constant-amplitude segment for every channel. */
static void seg_step(wg_config_t *pst_cfg, const wg_signal_t *pst_base,
                     double dur_s, double amp, uint8_t transition,
                     const double *p_delay)
{
    wg_signal_t st_sig = *pst_base;

    seg_amp_all(&st_sig, amp);
    seg_put(pst_cfg, &st_sig, dur_s, transition, p_delay);
}

/* Add a segment with per-phase amplitudes (Ua, Ub, Uc). */
static void seg_step3(wg_config_t *pst_cfg, const wg_signal_t *pst_base,
                      double dur_s, const double amp[3], uint8_t transition,
                      const double delay[3])
{
    wg_signal_t st_sig = *pst_base;
    double d[WG_MAX_CHANNELS];
    int i;

    for (i = 0; i < 3; ++i) {
        st_sig.amp_pct[i].profile = WG_PROFILE_CONST;
        st_sig.amp_pct[i].from = amp[i];
        st_sig.amp_pct[i].to = amp[i];
        st_sig.amp_pct[i].period_s = 0.0;
    }
    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        d[i] = (i < 3) ? delay[i] : 0.0;
    }
    seg_put(pst_cfg, &st_sig, dur_s, transition, d);
}

/* Add a linear-amplitude ramp segment. */
static void seg_ramp(wg_config_t *pst_cfg, const wg_signal_t *pst_base,
                     double dur_s, double from, double to)
{
    wg_signal_t st_sig = *pst_base;
    int i;

    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        st_sig.amp_pct[i].profile = WG_PROFILE_LINEAR;
        st_sig.amp_pct[i].from = from;
        st_sig.amp_pct[i].to = to;
        st_sig.amp_pct[i].period_s = 0.0;
    }
    seg_put(pst_cfg, &st_sig, dur_s, (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
}

/* Fill the remaining duration with a steady segment. */
static void seg_rest(wg_config_t *pst_cfg, const wg_signal_t *pst_base,
                     double amp)
{
    double total = 0.0;
    uint16_t i;

    for (i = 0u; i < pst_cfg->segment_count; ++i) {
        total += pst_cfg->segments[i].duration_s;
    }
    if (pst_cfg->duration_s > total + 1e-9) {
        seg_step(pst_cfg, pst_base, pst_cfg->duration_s - total, amp,
                 (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* simple steady presets                                              */
/* ------------------------------------------------------------------ */

#define SC_STEADY(fname, f50, f60, amp, dur)                                  \
    static int32_t fname(wg_config_t *pst_cfg, int32_t s32_sys,                \
                         char *pc_err, size_t err_len)                         \
    {                                                                         \
        (void)pc_err;                                                         \
        (void)err_len;                                                        \
        sig_init(pst_cfg, (s32_sys == 60) ? (f60) : (f50));                   \
        amp_all(pst_cfg, (amp));                                              \
        pst_cfg->duration_s = (dur);                                          \
        pst_cfg->segment_count = 0u;                                          \
        return 0;                                                             \
    }

#define SC_STEADY_S1(fname, f50, f60, amp, dur)                               \
    static int32_t fname(wg_config_t *pst_cfg, int32_t s32_sys,                \
                         char *pc_err, size_t err_len)                         \
    {                                                                         \
        (void)pc_err;                                                         \
        (void)err_len;                                                        \
        sig_init(pst_cfg, (s32_sys == 60) ? (f60) : (f50));                   \
        amp_all(pst_cfg, (amp));                                              \
        add_s1(pst_cfg);                                                      \
        pst_cfg->duration_s = (dur);                                          \
        pst_cfg->segment_count = 0u;                                          \
        return 0;                                                             \
    }

#define SC_TONE(fname, f50, f60, hz, amp, dur)                                \
    static int32_t fname(wg_config_t *pst_cfg, int32_t s32_sys,                \
                         char *pc_err, size_t err_len)                         \
    {                                                                         \
        (void)pc_err;                                                         \
        (void)err_len;                                                        \
        sig_init(pst_cfg, (s32_sys == 60) ? (f60) : (f50));                   \
        (void)add_tone(pst_cfg, (hz), (amp), 0.0);                            \
        pst_cfg->duration_s = (dur);                                          \
        pst_cfg->segment_count = 0u;                                          \
        return 0;                                                             \
    }

SC_STEADY(sc_a121, 42.5, 51.0, 100.0, 60.0)
SC_STEADY(sc_a122, 50.05, 59.95, 100.0, 60.0)
SC_STEADY(sc_a123, 57.5, 69.0, 100.0, 60.0)
SC_STEADY(sc_a131, 50.05, 59.95, 10.0, 60.0)
SC_STEADY(sc_a221, 50.0, 60.0, 10.0, 2.0)
SC_STEADY(sc_a222, 50.0, 60.0, 80.0, 2.0)
SC_STEADY(sc_a223, 50.0, 60.0, 150.0, 2.0)
SC_STEADY(sc_a231, 42.5, 51.0, 80.0, 2.0)
SC_STEADY(sc_a241, 49.99, 59.99, 80.0, 660.0)
SC_STEADY(sc_a721, 50.0, 60.0, 100.0, 10.0)
SC_STEADY(sc_a911, 50.0, 60.0, 150.0, 2.0)
SC_STEADY(sc_a912, 50.0, 60.0, 100.0, 2.0)
SC_STEADY(sc_a913, 50.0, 60.0, 10.0, 2.0)
SC_STEADY(sc_a1011, 50.0, 60.0, 100.0, 7200.0)

SC_STEADY_S1(sc_a132, 50.05, 59.95, 100.0, 60.0)
SC_STEADY_S1(sc_a232, 50.0, 60.0, 80.0, 2.0)
SC_STEADY_S1(sc_a615, 50.0, 60.0, 100.0, 10.0)
SC_STEADY_S1(sc_a631, 42.5, 51.0, 100.0, 10.0)
SC_STEADY_S1(sc_a834, 50.0, 60.0, 100.0, 10.0)

SC_TONE(sc_a811, 50.0, 60.0, 1060.0, 1.0, 10.0)
SC_TONE(sc_a812, 50.0, 60.0, 1060.0, 1.0, 120.0)
SC_TONE(sc_a813, 50.0, 60.0, 1060.0, 3.0, 10.0)
SC_TONE(sc_a816, 50.0, 60.0, 316.67, 1.0, 10.0)
SC_TONE(sc_a817, 50.0, 60.0, 316.67, 1.0, 120.0)
SC_TONE(sc_a821, 50.0, 60.0, 316.67, 3.0, 2.0)
SC_TONE(sc_a822, 50.0, 60.0, 1060.0, 3.0, 2.0)
SC_TONE(sc_a823, 50.0, 60.0, 2975.0, 3.0, 2.0)
SC_TONE(sc_a831, 42.5, 51.0, 1060.0, 3.0, 10.0)
SC_TONE(sc_a832, 50.0, 60.0, 1060.0, 3.0, 10.0)

/* 2 h availability-only steady default: 100 %, rated frequency. */
static int32_t sc_steady_2h(wg_config_t *pst_cfg, int32_t s32_sys,
                            char *pc_err, size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    amp_all(pst_cfg, 100.0);
    pst_cfg->duration_s = 7200.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.1 frequency                                                      */
/* ------------------------------------------------------------------ */

static int32_t sc_a111(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    double lo = (s32_sys == 60) ? 51.0 : 42.5;
    double hi = (s32_sys == 60) ? 69.0 : 57.5;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, lo);
    pst_cfg->signal.frequency.profile = WG_PROFILE_TRIANGLE;
    pst_cfg->signal.frequency.from = lo;
    pst_cfg->signal.frequency.to = hi;
    pst_cfg->signal.frequency.period_s = 10.0; /* 5 s one way */
    pst_cfg->duration_s = 120.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.2 magnitude                                                      */
/* ------------------------------------------------------------------ */

static int32_t sc_a211(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    amp_sine_all(pst_cfg, 2.3, 10.0);
    pst_cfg->duration_s = 20.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a251(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.15 : 50.125);
    amp_triangle_all(pst_cfg, 10.0, 80.0, 120.0); /* 60 s one way */
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a261(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    amp_triangle_all(pst_cfg, 10.0, 80.0, 120.0);
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.3 flicker                                                        */
/* ------------------------------------------------------------------ */

static int32_t sc_flicker(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                          size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    /* IEC 61000-4-15 Table 5, CPM 1, 230 V lamp. */
    amp_rect_all(pst_cfg, 1.0 / 120.0, (s32_sys == 60) ? 2.719 : 2.715,
                 50.0);
    pst_cfg->duration_s = 60.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.4 dips / swells / interruptions                                  */
/* ------------------------------------------------------------------ */

/* 单通道幅值阶跃事件：time_s 起该通道幅值变为 pct。 */
static void add_amp_step(wg_config_t *pst_cfg, uint8_t u8_ch, double time_s,
                         double pct)
{
    wg_event_t st_e;

    memset(&st_e, 0, sizeof(st_e));
    st_e.kind = (uint8_t)WG_EVENT_AMP_STEP;
    st_e.channel_mask = (uint8_t)(1u << u8_ch);
    st_e.time_s = time_s;
    st_e.value = pct;
    (void)wg_program_add_event(&pst_cfg->program, &st_e);
}

/* T1 -> 0 %, T1+2T -> 90 %, T1+4T -> 94 %（通道独立）。 */
static void add_step_triplet(wg_config_t *pst_cfg, uint8_t u8_ch, double t0,
                             double period_s)
{
    add_amp_step(pst_cfg, u8_ch, t0, 0.0);
    add_amp_step(pst_cfg, u8_ch, t0 + 2.0 * period_s, 90.0);
    add_amp_step(pst_cfg, u8_ch, t0 + 4.0 * period_s, 94.0);
}

/* A4.1.1: per-channel order, T1 = 1 s, 10/20 cycle stagger, 15 s. */
static int32_t sc_a411(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    double t_cycle = 1.0 / (double)s32_sys;
    double t1 = 1.0;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 15.0;
    pst_cfg->segment_count = 0u;

    add_step_triplet(pst_cfg, (uint8_t)WG_CH_UA, t1, t_cycle);
    add_step_triplet(pst_cfg, (uint8_t)WG_CH_UB,
                     t1 + 10.0 * t_cycle + t_cycle / 3.0, t_cycle);
    add_step_triplet(pst_cfg, (uint8_t)WG_CH_UC,
                     t1 + 20.0 * t_cycle - t_cycle / 3.0, t_cycle);
    return 0;
}

static int32_t sc_a412(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    static const double dur_cycles[6] = { 1.0, 1.5, 2.5, 10.0, 30.0, 150.0 };
    double t_cycle = 1.0 / (double)s32_sys;
    size_t i;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 15.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    for (i = 0u; i < 6u; ++i) {
        seg_step(pst_cfg, &pst_cfg->signal, dur_cycles[i] * t_cycle, 200.0,
                 (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
        seg_step(pst_cfg, &pst_cfg->signal, dur_cycles[i] * t_cycle, 20.0,
                 (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    }
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a414(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    double t_cycle = 1.0 / (double)s32_sys;
    double f_lo = (s32_sys == 60) ? 51.0 : 42.5;
    double f_hi = (s32_sys == 60) ? 69.0 : 57.5;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, f_lo);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 2.0 * t_cycle, 20.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);

    set_freq(pst_cfg, f_hi);
    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 30.0 * t_cycle, 20.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a415(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len);
static int32_t sc_a421(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len);

static int32_t sc_a415(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    return sc_a421(pst_cfg, s32_sys, pc_err, err_len);
}

static int32_t sc_a416(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    double t_cycle = 1.0 / (double)s32_sys;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 910.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 300.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 300.0, 95.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 300.0, 87.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 5.0 * t_cycle, 50.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a417(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    /* 0 % 段不会被 program 的段幅值覆盖识别，基波基准必须为 0。 */
    amp_all(pst_cfg, 0.0);
    pst_cfg->duration_s = 360.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 300.0, 0.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a421(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    double t_cycle = 1.0 / (double)s32_sys;
    double dip_amp[3] = { 0.0, 0.0, 0.0 };
    double dip_delay[3];
    double rec_amp[3] = { 100.0, 100.0, 100.0 };
    double rec_delay[3];

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    /* 0 % 暂降段以 0 基波为基准（段幅值 0 不构成 program 覆盖）。 */
    amp_all(pst_cfg, 0.0);
    pst_cfg->duration_s = 15.0;
    pst_cfg->segment_count = 0u;

    dip_delay[0] = 0.0;
    dip_delay[1] = t_cycle;
    dip_delay[2] = 2.0 * t_cycle;
    rec_delay[0] = 2.0 * t_cycle;
    rec_delay[1] = t_cycle;
    rec_delay[2] = 0.0;

    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step3(pst_cfg, &pst_cfg->signal, 8.5 * t_cycle, dip_amp,
              (uint8_t)WG_TRANS_ZERO_CROSSING, dip_delay);
    seg_step3(pst_cfg, &pst_cfg->signal, 3.5 * t_cycle, rec_amp,
              (uint8_t)WG_TRANS_ZERO_CROSSING, rec_delay);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a431(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    double t_cycle = 1.0 / (double)s32_sys;
    double swell_amp[3] = { 130.0, 130.0, 150.0 };
    double swell_delay[3];
    double rec1_amp[3] = { 100.0, 130.0, 100.0 };
    double rec1_delay[3] = { 0.0, 1.0, 0.0 };
    double rec2_amp[3] = { 100.0, 100.0, 100.0 };
    double rec2_delay[3] = { 0.0, 0.0, 0.0 };

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 15.0;
    pst_cfg->segment_count = 0u;

    swell_delay[0] = 0.0;
    swell_delay[1] = t_cycle;
    swell_delay[2] = 2.0 * t_cycle;
    rec1_delay[1] = t_cycle;
    rec2_delay[0] = 0.0;

    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step3(pst_cfg, &pst_cfg->signal, 4.0 * t_cycle, swell_amp,
              (uint8_t)WG_TRANS_ZERO_CROSSING, swell_delay);
    seg_step3(pst_cfg, &pst_cfg->signal, 1.0 * t_cycle, rec1_amp,
              (uint8_t)WG_TRANS_ZERO_CROSSING, rec1_delay);
    seg_step3(pst_cfg, &pst_cfg->signal, 1.0 * t_cycle, rec2_amp,
              (uint8_t)WG_TRANS_ZERO_CROSSING, rec2_delay);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.5 unbalance                                                      */
/* ------------------------------------------------------------------ */

static int32_t sc_unbalance(wg_config_t *pst_cfg, int32_t s32_sys, double a0,
                            double a1, double a2, double ph0, double ph1,
                            double ph2)
{
    wg_component_t *pst_fund;

    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_fund = fund_comp(pst_cfg);
    pst_cfg->signal.amp_pct[0].from = a0;
    pst_cfg->signal.amp_pct[0].to = a0;
    pst_cfg->signal.amp_pct[1].from = a1;
    pst_cfg->signal.amp_pct[1].to = a1;
    pst_cfg->signal.amp_pct[2].from = a2;
    pst_cfg->signal.amp_pct[2].to = a2;
    pst_cfg->signal.phase_deg[0] = ph0;
    pst_cfg->signal.phase_deg[1] = ph1;
    pst_cfg->signal.phase_deg[2] = ph2;
    if (pst_fund != NULL) {
        pst_fund->amp_pct[0] = a0;
        pst_fund->amp_pct[1] = a1;
        pst_fund->amp_pct[2] = a2;
        pst_fund->phase_deg[0] = ph0;
        pst_fund->phase_deg[1] = ph1;
        pst_fund->phase_deg[2] = ph2;
    }
    pst_cfg->duration_s = 60.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a511(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_unbalance(pst_cfg, s32_sys, 100.0, 100.0, 100.0, 0.0, -120.0,
                        120.0);
}

static int32_t sc_a512(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_unbalance(pst_cfg, s32_sys, 73.0, 80.0, 87.0, 0.0, -120.0,
                        120.0);
}

static int32_t sc_a513(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_unbalance(pst_cfg, s32_sys, 152.0, 140.0, 128.0, 0.0, -120.0,
                        120.0);
}

static int32_t sc_a514(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_unbalance(pst_cfg, s32_sys, 100.0, 90.0, 100.0, 0.0, -122.0,
                        118.0);
}

/* ------------------------------------------------------------------ */
/* 6.6 harmonics                                                      */
/* ------------------------------------------------------------------ */

/* Annex F.3: V1 = 100 %, sine AM fm = 2.3 Hz, depth 10 %, >= 20 s. */
static int32_t sc_a611(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    amp_sine_all(pst_cfg, 2.3, 10.0);
    pst_cfg->duration_s = 20.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a612(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_h(pst_cfg, 2u, 5.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a621(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_h(pst_cfg, 2u, 5.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a622(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_h(pst_cfg, 3u, 10.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a623(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_h(pst_cfg, 50u, 1.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a632(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    amp_all(pst_cfg, 200.0);
    add_h(pst_cfg, 3u, 10.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a641(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 59.99 : 49.99);
    add_h(pst_cfg, 3u, 10.0, 0.0);
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* H3 amplitude envelope 10 % -> 0 -> 10 %, 1 %/s (20 s full cycle). */
static int32_t sc_a651(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    int32_t s32_h3;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.15 : 50.125);
    /* 分量 100 % × 三角因子 10 %→0→10 % = 基波的 10 %→0→10 %。 */
    s32_h3 = add_h(pst_cfg, 3u, 100.0, 0.0);
    comp_triangle(pst_cfg, s32_h3, 10.0, 0.0, 20.0);
    /* 包络无法在旧数组中表达，manifest 不记录静态 10 %。 */
    if (pst_cfg->signal.harmonic_count > 0u) {
        pst_cfg->signal.harmonic_count -= 1u;
    }
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a661(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    int32_t s32_h3;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 59.99 : 49.99);
    /* 分量 100 % × 三角因子 10 %→0→10 % = 基波的 10 %→0→10 %。 */
    s32_h3 = add_h(pst_cfg, 3u, 100.0, 0.0);
    comp_triangle(pst_cfg, s32_h3, 10.0, 0.0, 20.0);
    /* 包络无法在旧数组中表达，manifest 不记录静态 10 %。 */
    if (pst_cfg->signal.harmonic_count > 0u) {
        pst_cfg->signal.harmonic_count -= 1u;
    }
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.7 interharmonics                                                 */
/* ------------------------------------------------------------------ */

static int32_t sc_ih_single(wg_config_t *pst_cfg, int32_t s32_sys, double order,
                            double amp, double dur)
{
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_ih(pst_cfg, order, amp, 0.0);
    pst_cfg->duration_s = dur;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a712(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_ih_single(pst_cfg, s32_sys, 1.5, 5.0, 10.0);
}

static int32_t sc_a722(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_ih_single(pst_cfg, s32_sys, 1.5, 5.0, 10.0);
}

static int32_t sc_a723(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_ih_single(pst_cfg, s32_sys, 7.5, 10.0, 10.0);
}

static int32_t sc_a724(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    return sc_ih_single(pst_cfg, s32_sys, 49.5, 1.0, 10.0);
}

static int32_t sc_a731(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 51.0 : 42.5);
    add_ih(pst_cfg, 1.5, 5.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a732(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    amp_all(pst_cfg, 200.0);
    add_ih(pst_cfg, 7.5, 10.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a741(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 59.99 : 49.99);
    add_ih(pst_cfg, 7.5, 10.0, 0.0);
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* 7.5th interharmonic envelope 10 % -> 0 -> 10 %, 1 %/s. */
static int32_t sc_a751(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    int32_t s32_ih;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.15 : 50.125);
    /* 分量 100 % × 三角因子 10 %→0→10 % = nominal 的 10 %→0→10 %。 */
    s32_ih = add_ih(pst_cfg, 7.5, 100.0, 0.0);
    comp_triangle(pst_cfg, s32_ih, 10.0, 0.0, 20.0);
    /* 包络无法在旧数组中表达，manifest 不记录静态 10 %。 */
    if (pst_cfg->signal.interharmonic_count > 0u) {
        pst_cfg->signal.interharmonic_count -= 1u;
    }
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a761(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    int32_t s32_ih;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 59.99 : 49.99);
    /* 分量 100 % × 三角因子 10 %→0→10 % = nominal 的 10 %→0→10 %。 */
    s32_ih = add_ih(pst_cfg, 7.5, 100.0, 0.0);
    comp_triangle(pst_cfg, s32_ih, 10.0, 0.0, 20.0);
    /* 包络无法在旧数组中表达，manifest 不记录静态 10 %。 */
    if (pst_cfg->signal.interharmonic_count > 0u) {
        pst_cfg->signal.interharmonic_count -= 1u;
    }
    pst_cfg->duration_s = 660.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.8 carrier signals                                                */
/* ------------------------------------------------------------------ */

static int32_t sc_a814(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_tone(pst_cfg, 315.0, 3.0, 0.0);
    add_tone(pst_cfg, 320.0, 3.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

static int32_t sc_a815(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    add_tone(pst_cfg, 1060.0, 3.0, 0.0);
    add_tone(pst_cfg, 315.0, 3.0, 0.0);
    add_tone(pst_cfg, 320.0, 3.0, 0.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.9 under/over deviation                                           */
/* ------------------------------------------------------------------ */

static int32_t sc_a914(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    static const double cycles[8] = { 20.0, 24.0, 40.0, 48.0,
                                      60.0, 72.0, 80.0, 96.0 };
    double t_cycle = 1.0 / (double)s32_sys;
    size_t i;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    for (i = 0u; i < 8u; ++i) {
        seg_step(pst_cfg, &pst_cfg->signal, cycles[i] * t_cycle,
                 (i % 2u == 0u) ? 150.0 : 100.0,
                 (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    }
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a915(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                       size_t err_len)
{
    static const double cycles[8] = { 20.0, 24.0, 40.0, 48.0,
                                      60.0, 72.0, 80.0, 96.0 };
    double t_cycle = 1.0 / (double)s32_sys;
    size_t i;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    for (i = 0u; i < 8u; ++i) {
        seg_step(pst_cfg, &pst_cfg->signal, cycles[i] * t_cycle,
                 (i % 2u == 0u) ? 100.0 : 10.0,
                 (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    }
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6.10 flagging                                                      */
/* ------------------------------------------------------------------ */

static int32_t sc_flag_event(wg_config_t *pst_cfg, int32_t s32_sys,
                             const double amp[3])
{
    double delay[3] = { 0.0, 0.0, 0.0 };

    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 7200.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 1.0, 100.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_step3(pst_cfg, &pst_cfg->signal, 0.1, amp,
              (uint8_t)WG_TRANS_IMMEDIATE, delay);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a1012(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    static const double amp[3] = { 100.0, 70.0, 100.0 };

    (void)pc_err;
    (void)err_len;
    return sc_flag_event(pst_cfg, s32_sys, amp);
}

static int32_t sc_a1013(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    return sc_a1012(pst_cfg, s32_sys, pc_err, err_len);
}

static int32_t sc_a1014(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    static const double amp[3] = { 120.0, 100.0, 120.0 };

    (void)pc_err;
    (void)err_len;
    return sc_flag_event(pst_cfg, s32_sys, amp);
}

static int32_t sc_a1015(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    static const double amp[3] = { 0.0, 0.0, 0.0 };
    int32_t s32_rc;

    (void)pc_err;
    (void)err_len;
    s32_rc = sc_flag_event(pst_cfg, s32_sys, amp);
    if (s32_rc == 0) {
        /* 0 % 段需要 0 基波，否则会被基波默认 100 % 补回。 */
        amp_all(pst_cfg, 0.0);
    }
    return s32_rc;
}

/* ------------------------------------------------------------------ */
/* 6.13 rapid voltage changes                                         */
/* ------------------------------------------------------------------ */

static int32_t sc_a1311(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    double half = 0.5 / (double)s32_sys;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 100.0 * half, 100.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_ramp(pst_cfg, &pst_cfg->signal, 200.0 * half, 100.0, 92.0);
    seg_step(pst_cfg, &pst_cfg->signal, 100.0 * half, 92.0,
             (uint8_t)WG_TRANS_IMMEDIATE, NULL);
    seg_ramp(pst_cfg, &pst_cfg->signal, 200.0 * half, 92.0, 100.0);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a1312(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    double half = 0.5 / (double)s32_sys;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 100.0 * half, 100.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 50.0 * half, 97.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a1313(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    double half = 0.5 / (double)s32_sys;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 100.0 * half, 100.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 50.0 * half, 93.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 100.0 * half, 85.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_rest(pst_cfg, &pst_cfg->signal, 100.0);
    return 0;
}

static int32_t sc_a1321(wg_config_t *pst_cfg, int32_t s32_sys, char *pc_err,
                        size_t err_len)
{
    double half = 0.5 / (double)s32_sys;

    (void)pc_err;
    (void)err_len;
    sig_init(pst_cfg, (s32_sys == 60) ? 60.0 : 50.0);
    pst_cfg->duration_s = 10.0;
    pst_cfg->segment_count = 0u;

    seg_step(pst_cfg, &pst_cfg->signal, 100.0 * half, 100.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_step(pst_cfg, &pst_cfg->signal, 50.0 * half, 93.0,
             (uint8_t)WG_TRANS_ZERO_CROSSING, NULL);
    seg_rest(pst_cfg, &pst_cfg->signal, 93.0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* scenario table                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *pc_id;
    const char *pc_sys;
    const char *pc_desc;
    const char *pc_nodefault;
    int32_t (*pfn_apply)(wg_config_t *, int32_t, char *, size_t);
} wg_scen_t;

static const wg_scen_t g_scen[] = {
    /* 6.1 */
    { "A1.1.1", "50/60",
      "freq triangle 42.5-57.5 Hz (60 Hz 51-69), 5 s one-way, 120 s",
      NULL, sc_a111 },
    { "A1.2.1", "50/60", "steady 42.5 Hz (60 Hz 51 Hz), 60 s",
      NULL, sc_a121 },
    { "A1.2.2", "50/60", "steady 50.05 Hz (60 Hz 59.95 Hz), 60 s",
      NULL, sc_a122 },
    { "A1.2.3", "50/60", "steady 57.5 Hz (60 Hz 69 Hz), 60 s",
      NULL, sc_a123 },
    { "A1.3.1", "50/60", "50.05/59.95 Hz at 10 % Udin, 60 s",
      NULL, sc_a131 },
    { "A1.3.2", "50/60", "50.05/59.95 Hz with S1 harmonic group, 60 s",
      NULL, sc_a132 },
    { "A1.4.1", "50/60",
      "CUSTOM: reference-channel test, user supplies per-channel stimulus",
      "CUSTOM: signal.reference_channel: user must supply per-channel stimulus",
      NULL },
    { "freq-min", "50/60", "alias of A1.2.1", NULL, sc_a121 },
    { "freq-nominal", "50/60", "alias of A1.2.2", NULL, sc_a122 },
    { "freq-max", "50/60", "alias of A1.2.3", NULL, sc_a123 },

    /* 6.2 */
    { "A2.1.1", "50/60", "sine AM 2.3 Hz, depth 10 %, 20 s",
      NULL, sc_a211 },
    { "A2.2.1", "50/60", "steady 10 % Udin, 2 s", NULL, sc_a221 },
    { "A2.2.2", "50/60", "steady 80 % Udin, 2 s", NULL, sc_a222 },
    { "A2.2.3", "50/60", "steady 150 % Udin, 2 s", NULL, sc_a223 },
    { "A2.3.1", "50/60", "80 % Udin at 42.5/51 Hz, 2 s", NULL, sc_a231 },
    { "A2.3.2", "50/60", "80 % Udin with S1 harmonic group, 2 s",
      NULL, sc_a232 },
    { "A2.4.1", "50/60", "80 % Udin at 49.99/59.99 Hz, 660 s",
      NULL, sc_a241 },
    { "A2.5.1", "50/60",
      "amp triangle 10-80 % (60 s one-way) at 50.125/60.15 Hz, 660 s",
      NULL, sc_a251 },
    { "A2.6.1", "50/60",
      "amp triangle 10-80 % (60 s one-way) at 50/60 Hz, 660 s",
      NULL, sc_a261 },
    { "A2.7.1", "50/60",
      "steady 100 % at rated frequency, 7200 s (availability)",
      NULL, sc_steady_2h },

    /* 6.3 */
    { "FLICKER-4-15", "50/60",
      "rect AM, CPM 1, depth 2.715/2.719 %, duty 50 %, 60 s",
      NULL, sc_flicker },

    /* 6.4 */
    { "A4.1.1", "50/60",
      "per-channel steps 0/90/94 % at T1+0/2/4 T, T1=1 s, 10/20 T stagger, 15 s",
      NULL, sc_a411 },
    { "A4.1.2", "50/60",
      "swell 200 % / dip 20 % for 1, 1.5, 2.5, 10, 30, 150 cycles",
      NULL, sc_a412 },
    { "A4.1.3", "50/60",
      "threshold-margin step, no default delta (NO_DEFAULT)",
      "amp_pct.delta: threshold margin not in docs/01", NULL },
    { "A4.1.4", "50/60", "freq P1/P3 with 20 % dip for 2 and 30 cycles",
      NULL, sc_a414 },
    { "A4.1.5", "50/60", "combination, mapped to A4.2.1", NULL, sc_a415 },
    { "A4.1.6", "50/60",
      "100/95/87 % in 5 min steps, then 50 % for 5 cycles, 910 s",
      NULL, sc_a416 },
    { "A4.1.7", "50/60", "0 V held 300 s then Udin, 360 s",
      NULL, sc_a417 },
    { "A4.2.1", "50/60",
      "3-phase sequential 0 % dip and recovery, zero crossing, 15 s",
      NULL, sc_a421 },
    { "A4.3.1", "50/60",
      "3-phase sequential swell 130/130/150 %, zero crossing, 15 s",
      NULL, sc_a431 },

    /* 6.5 */
    { "A5.1.1", "50/60", "100/100/100 %, 0/-120/+120 deg",
      NULL, sc_a511 },
    { "A5.1.2", "50/60", "73/80/87 %, symmetric phase",
      NULL, sc_a512 },
    { "A5.1.3", "50/60", "152/140/128 %, symmetric phase",
      NULL, sc_a513 },
    { "A5.1.4", "50/60", "100/90/100 %, 0/-122/+118 deg",
      NULL, sc_a514 },
    { "A5.3", "50/60",
      "steady 100 % at rated frequency, 7200 s (availability)",
      NULL, sc_steady_2h },

    /* 6.6 */
    { "A6.1.1", "50/60",
      "Annex F.3: V1 100 %, sine AM 2.3 Hz 10 %, 20 s (F.4/F.5 optional)",
      NULL, sc_a611 },
    { "A6.1.2", "50/60",
      "2nd harmonic 5 % (variants 1.5xf1 5 %, 2nd+5 Hz 4 %), 10 s",
      NULL, sc_a612 },
    { "A6.1.3", "50/60",
      "CUSTOM: harmonic-count capability, user supplies 50 orders",
      "CUSTOM: harmonics[1..50].amp_pct: user must supply 50 orders", NULL },
    { "A6.1.4", "50/60",
      "P5 harmonic set, 41-50th Class 3 levels missing (NO_DEFAULT)",
      "harmonics[41..50].amp_pct: Class 3 levels not defined", NULL },
    { "A6.1.5", "50/60", "S1 group H3/7/11 10 %, H15 4 %, H19/23 5 %, 180 deg",
      NULL, sc_a615 },
    { "A6.1.6", "50/60",
      "EXCLUDED: anti-alias proof belongs to analog front end",
      "EXCLUDED: aliasing.margin: analog front-end property", NULL },
    { "A6.2.1", "50/60", "2nd harmonic 5 %", NULL, sc_a621 },
    { "A6.2.2", "50/60", "3rd harmonic 10 %", NULL, sc_a622 },
    { "A6.2.3", "50/60", "50th harmonic 1 %", NULL, sc_a623 },
    { "A6.2.4", "50/60",
      "P4 harmonic set, 41-50th Class 3 levels missing (NO_DEFAULT)",
      "harmonics[41..50].amp_pct: Class 3 levels not defined", NULL },
    { "A6.2.5", "50/60",
      "P5 harmonic set, 41-50th Class 3 levels missing (NO_DEFAULT)",
      "harmonics[41..50].amp_pct: Class 3 levels not defined", NULL },
    { "A6.3.1", "50/60", "42.5/51 Hz with S1 harmonic group, 10 s",
      NULL, sc_a631 },
    { "A6.3.2", "50/60", "200 % Udin with 3rd harmonic 10 %, 10 s",
      NULL, sc_a632 },
    { "A6.4.1", "50/60", "3rd harmonic 10 % at 49.99/59.99 Hz, 660 s",
      NULL, sc_a641 },
    { "A6.5.1", "50/60",
      "3rd harmonic 10->0->10 % at 1 %/s, 50.125/60.15 Hz, 660 s",
      NULL, sc_a651 },
    { "A6.6.1", "50/60",
      "3rd harmonic 10->0->10 % at 1 %/s, 49.99/59.99 Hz, 660 s",
      NULL, sc_a661 },
    { "A6.7.1", "50/60",
      "steady 100 % at rated frequency, 7200 s (availability)",
      NULL, sc_steady_2h },

    /* 6.7 */
    { "A7.1.1", "50/60",
      "Annex F modulated interharmonic, values missing (NO_DEFAULT)",
      "interharmonics[].amp_pct: Annex F values not available", NULL },
    { "A7.1.2", "50/60", "1.5xf1 interharmonic 5 %, 10 s",
      NULL, sc_a712 },
    { "A7.1.3", "50/60",
      "CUSTOM: >=50 interharmonic capability, user supplies orders",
      "CUSTOM: interharmonics[1..50].amp_pct: user must supply 50 orders",
      NULL },
    { "A7.2.1", "50/60", "reference steady 100 % Udin, 10 s",
      NULL, sc_a721 },
    { "A7.2.2", "50/60", "P1: 1.5xf1 5 %", NULL, sc_a722 },
    { "A7.2.3", "50/60", "P2: 7.5xf1 10 %", NULL, sc_a723 },
    { "A7.2.4", "50/60", "P3: 49.5xf1 1 %", NULL, sc_a724 },
    { "A7.2.5", "50/60",
      "P4 interharmonic set, Class 3 curve missing (NO_DEFAULT)",
      "interharmonics[].amp_pct: Class 3 curve (Figure 3) not obtained",
      NULL },
    { "A7.2.6", "50/60",
      "P5 interharmonic set, Class 3 curve missing (NO_DEFAULT)",
      "interharmonics[].amp_pct: Class 3 curve (Figure 3) not obtained",
      NULL },
    { "A7.3.1", "50/60", "42.5/51 Hz with 1.5xf1 5 %, 10 s",
      NULL, sc_a731 },
    { "A7.3.2", "50/60", "200 % Udin with 7.5xf1 10 %, 10 s",
      NULL, sc_a732 },
    { "A7.4.1", "50/60", "7.5xf1 10 % at 49.99/59.99 Hz, 660 s",
      NULL, sc_a741 },
    { "A7.5.1", "50/60",
      "7.5xf1 10->0->10 % at 1 %/s, 50.125/60.15 Hz, 660 s",
      NULL, sc_a751 },
    { "A7.6.1", "50/60",
      "7.5xf1 10->0->10 % at 1 %/s, 49.99/59.99 Hz, 660 s",
      NULL, sc_a761 },
    { "A7.7.1", "50/60",
      "steady 100 % at rated frequency, 7200 s (availability)",
      NULL, sc_steady_2h },

    /* 6.8 */
    { "A8.1.1", "50/60", "carrier 1060 Hz at 1 %, 10 s", NULL, sc_a811 },
    { "A8.1.2", "50/60", "carrier 1060 Hz at 1 %, 120 s", NULL, sc_a812 },
    { "A8.1.3", "50/60", "method 1: 1060 Hz P3 (3 %), 10 s",
      NULL, sc_a813 },
    { "A8.1.4", "50/60", "method 2: 315+320 Hz, 3 % each, 10 s",
      NULL, sc_a814 },
    { "A8.1.5", "50/60", "method 1+2: 1060 and 315+320 Hz, 10 s",
      NULL, sc_a815 },
    { "A8.1.6", "50/60", "316.67 Hz P2 (1 %); P1 variant is 0 %",
      NULL, sc_a816 },
    { "A8.1.7", "50/60", "316.67 Hz 1 %, 120 s", NULL, sc_a817 },
    { "A8.2.1", "50/60", "316.67 Hz P3 (P2-P5: 1/3/9/15 %), 2 s",
      NULL, sc_a821 },
    { "A8.2.2", "50/60", "1060 Hz P3 (P2-P5: 1/3/9/15 %), 2 s",
      NULL, sc_a822 },
    { "A8.2.3", "50/60", "2975 Hz P3 (P2-P5: 1/3/9/15 %), 2 s",
      NULL, sc_a823 },
    { "A8.3.1", "50/60", "1060 Hz P3 at 42.5/51 Hz, 10 s",
      NULL, sc_a831 },
    { "A8.3.2", "50/60", "1060 Hz P3 at 10 % Udin, 10 s", NULL, sc_a832 },
    { "A8.3.4", "50/60", "1060 Hz P3 with S1 harmonic group, 10 s",
      NULL, sc_a834 },

    /* 6.9 */
    { "A9.1.1", "50/60", "steady 150 % Udin, 2 s", NULL, sc_a911 },
    { "A9.1.2", "50/60", "steady Udin (reference +/-1 %), 2 s",
      NULL, sc_a912 },
    { "A9.1.3", "50/60", "steady 10 % Udin, 2 s", NULL, sc_a913 },
    { "A9.1.4", "50/60",
      "150/100 % alternate: 20/24, 40/48, 60/72, 80/96 cycles",
      NULL, sc_a914 },
    { "A9.1.5", "50/60",
      "100/10 % alternate: 20/24, 40/48, 60/72, 80/96 cycles",
      NULL, sc_a915 },
    { "A9.1.6", "50/60",
      "CUSTOM: channel/reference selection, user supplies wiring mode",
      "CUSTOM: signal.reference_channel: user must supply wiring/reference channel",
      NULL },
    { "A9.2.1", "50/60",
      "150/10 % alternation, period not specified (NO_DEFAULT)",
      "amp_pct.alternation_period: not specified in docs/01", NULL },
    { "A9.2.2", "50/60",
      "150/10 % alternation, period not specified (NO_DEFAULT)",
      "amp_pct.alternation_period: not specified in docs/01", NULL },
    { "A9.3.1", "50/60",
      "150/10 % alternation, period not specified (NO_DEFAULT)",
      "amp_pct.alternation_period: not specified in docs/01", NULL },
    { "A9.4.1", "50/60",
      "steady 100 % at rated frequency, 7200 s (availability)",
      NULL, sc_steady_2h },

    /* 6.10 */
    { "A10.1.1", "50/60", "steady Udin, 7200 s", NULL, sc_a1011 },
    { "A10.1.2", "50/60", "L2 70 % Udin for 100 ms, 7200 s",
      NULL, sc_a1012 },
    { "A10.1.3", "50/60", "same as A10.1.2, 7200 s", NULL, sc_a1013 },
    { "A10.1.4", "50/60", "L1+L3 120 % Udin for 100 ms, 7200 s",
      NULL, sc_a1014 },
    { "A10.1.5", "50/60", "L1+L2+L3 0 % Udin for 100 ms, 7200 s",
      NULL, sc_a1015 },

    /* 6.11 */
    { "A11.1.1", "50/60",
      "interruption test, amplitude/duration missing (NO_DEFAULT)",
      "amp_pct/duration_s: interruption values not in docs/01", NULL },

    /* 6.13 */
    { "A13.1.1", "50/60",
      "ramp 100->92 % over half-cycles 100-300, back over 400-600", NULL,
      sc_a1311 },
    { "A13.1.2", "50/60", "step 100->97 % at hc 100, back at hc 150",
      NULL, sc_a1312 },
    { "A13.1.3", "50/60", "100->93->85->100 % at hc 100/150/250",
      NULL, sc_a1313 },
    { "A13.2.1", "50/60", "step 100->93 % at hc 100", NULL, sc_a1321 },
    { "A13.3.1", "50/60",
      "Table 12 RVC values, transcription uncertain (NO_DEFAULT)",
      "amp_pct: Table 12 values uncertain in docs/01", NULL },
    { "A13.4.1", "50/60",
      "Table 13 RVC values, transcription uncertain (NO_DEFAULT)",
      "amp_pct: Table 13 values uncertain in docs/01", NULL },
    { "A13.5.1", "50/60",
      "Table 14 RVC values, transcription uncertain (NO_DEFAULT)",
      "amp_pct: Table 14 values uncertain in docs/01", NULL },
    { "A13.5.2", "50/60",
      "half-cycle event times missing (NO_DEFAULT)",
      "events[].time_s: half-cycle times not in docs/01", NULL }
};

/* 表项错误串可能已自带分类前缀，此时不再重复添加 NO_DEFAULT。 */
static int has_class_prefix(const char *pc_s)
{
    return (strncmp(pc_s, "NO_DEFAULT:", 11u) == 0
            || strncmp(pc_s, "CUSTOM:", 7u) == 0
            || strncmp(pc_s, "EXCLUDED:", 9u) == 0) ? 1 : 0;
}

static int eq_ci(const char *pc_a, const char *pc_b)
{
    while (*pc_a != '\0' && *pc_b != '\0') {
        int ca = tolower((unsigned char)*pc_a);
        int cb = tolower((unsigned char)*pc_b);

        if (ca != cb) {
            return 0;
        }
        pc_a += 1;
        pc_b += 1;
    }
    return (*pc_a == '\0' && *pc_b == '\0') ? 1 : 0;
}

int32_t wg_scenario_apply(const char *pc_test, int32_t s32_system_hz,
                          wg_config_t *pst_cfg, char *pc_err, size_t err_len)
{
    size_t i;
    const wg_scen_t *pst_e = NULL;

    if (pc_err != NULL && err_len > 0u) {
        pc_err[0] = '\0';
    }
    if (pc_test == NULL || pst_cfg == NULL || pc_err == NULL || err_len == 0u) {
        return -1;
    }

    if (s32_system_hz == 0) {
        s32_system_hz = 50;
    }
    if (s32_system_hz != 50 && s32_system_hz != 60) {
        snprintf(pc_err, err_len, "system_hz: expected 50 or 60");
        return -1;
    }

    for (i = 0u; i < sizeof(g_scen) / sizeof(g_scen[0]); ++i) {
        if (eq_ci(g_scen[i].pc_id, pc_test) != 0) {
            pst_e = &g_scen[i];
            break;
        }
    }
    if (pst_e == NULL) {
        snprintf(pc_err, err_len, "test: unknown scenario '%s'", pc_test);
        return -1;
    }
    if (pst_e->pc_nodefault != NULL) {
        if (has_class_prefix(pst_e->pc_nodefault) != 0) {
            snprintf(pc_err, err_len, "%s", pst_e->pc_nodefault);
        } else {
            snprintf(pc_err, err_len, "NO_DEFAULT: %s", pst_e->pc_nodefault);
        }
        return 1;
    }
    return pst_e->pfn_apply(pst_cfg, s32_system_hz, pc_err, err_len);
}

void wg_scenario_print_all(FILE *p_file)
{
    size_t i;

    if (p_file == NULL) {
        return;
    }
    for (i = 0u; i < sizeof(g_scen) / sizeof(g_scen[0]); ++i) {
        fprintf(p_file, "%-12s  %-5s  %s\n", g_scen[i].pc_id,
                g_scen[i].pc_sys, g_scen[i].pc_desc);
    }
}
