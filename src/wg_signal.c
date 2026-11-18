#include <math.h>

#include "wg_internal.h"

/*
 * 轮廓求值。f64_t_local 为相对段起点的秒数；linear 在段末饱和为 to，
 * triangle 以 period_s 为完整周期、从 from 出发在 from 与 to 之间往返。
 */
double prv_profile_eval(const wg_profile_f64_t *pst_prof, double f64_t_local,
                        double f64_duration)
{
    double f64_ratio;

    if (pst_prof->profile == (uint8_t)WG_PROFILE_CONST) {
        return pst_prof->from;
    }

    if (pst_prof->profile == (uint8_t)WG_PROFILE_LINEAR) {
        if (!(f64_duration > 0.0)) {
            return pst_prof->from;
        }
        f64_ratio = f64_t_local / f64_duration;
        if (f64_ratio < 0.0) {
            f64_ratio = 0.0;
        } else if (f64_ratio > 1.0) {
            f64_ratio = 1.0;
        }
        return pst_prof->from + (pst_prof->to - pst_prof->from) * f64_ratio;
    }

    if (pst_prof->profile == (uint8_t)WG_PROFILE_TRIANGLE) {
        double f64_period = pst_prof->period_s;
        double f64_pos;
        double f64_half;

        if (!(f64_period > 0.0)) {
            return pst_prof->from;
        }
        f64_pos = fmod(f64_t_local, f64_period);
        if (f64_pos < 0.0) {
            f64_pos += f64_period;
        }
        f64_half = f64_period * 0.5;
        if (f64_pos <= f64_half) {
            f64_ratio = f64_pos / f64_half;
        } else {
            f64_ratio = (f64_period - f64_pos) / f64_half;
        }
        return pst_prof->from + (pst_prof->to - pst_prof->from) * f64_ratio;
    }

    return pst_prof->from;
}

/* 段内瞬时基波频率，Hz。 */
double prv_freq_at(const wg_signal_t *pst_sig, double f64_t_local, double f64_duration)
{
    return prv_profile_eval(&pst_sig->frequency, f64_t_local, f64_duration);
}

/*
 * 调制因子，只乘在基波上。
 * sine: 1 + depth/100 * cos(2πfm t + φm)
 * rect: 占空比内 (1 + depth/200)，其余 (1 - depth/200)
 */
double prv_mod_factor(const wg_modulation_t *pst_mod, double f64_t_abs)
{
    double f64_depth;

    if (pst_mod->type == (uint8_t)WG_MOD_SINE) {
        f64_depth = pst_mod->depth_pct / 100.0;
        return 1.0 + f64_depth * cos(PRV_TWO_PI * pst_mod->frequency_hz * f64_t_abs
                                     + pst_mod->phase_deg * PRV_DEG2RAD);
    }

    if (pst_mod->type == (uint8_t)WG_MOD_RECT) {
        double f64_duty;
        double f64_pos;

        if (!(pst_mod->frequency_hz > 0.0)) {
            return 1.0;
        }
        f64_duty = pst_mod->duty_pct / 100.0;
        if (f64_duty < 0.0) {
            f64_duty = 0.0;
        } else if (f64_duty > 1.0) {
            f64_duty = 1.0;
        }
        f64_pos = fmod(pst_mod->frequency_hz * f64_t_abs
                       + pst_mod->phase_deg / 360.0, 1.0);
        if (f64_pos < 0.0) {
            f64_pos += 1.0;
        }
        f64_depth = pst_mod->depth_pct / 200.0;
        return (f64_pos < f64_duty) ? (1.0 + f64_depth) : (1.0 - f64_depth);
    }

    return 1.0;
}

/*
 * 单通道瞬时值：
 *   A(t)·m(t)·cos(φ+φch)
 * + Σ A(t)·a_k/100·cos(k·φ+φ_k+φch)
 * + Σ nominal·√2·a_i/100·cos(i·φ+φ_i)
 * + Σ nominal·√2·a_j/100·cos(2π f_j t+θ_j)
 * A(t)=nominal·√2·amp_pct/100。
 */
double prv_channel_value(const wg_handle_t *pst_wg, uint8_t u8_ch,
                         const wg_signal_t *pst_sig, double f64_phase,
                         double f64_t_abs, double f64_amp_pct)
{
    double f64_peak;
    double f64_ch_phase;
    double f64_mod;
    double f64_sum;
    uint16_t u16_idx;

    f64_peak = pst_wg->cfg.nominal[u8_ch] * PRV_SQRT2 * f64_amp_pct / 100.0;
    f64_ch_phase = pst_sig->phase_deg[u8_ch] * PRV_DEG2RAD;
    f64_mod = prv_mod_factor(&pst_sig->modulation, f64_t_abs);

    f64_sum = f64_peak * f64_mod * cos(f64_phase + f64_ch_phase);

    for (u16_idx = 0; u16_idx < pst_sig->harmonic_count; u16_idx++) {
        const wg_harmonic_t *pst_h = &pst_sig->harmonics[u16_idx];
        double f64_amp = f64_peak * pst_h->amplitude_pct / 100.0;
        double f64_arg = (double)pst_h->order * f64_phase
                       + pst_h->phase_deg * PRV_DEG2RAD + f64_ch_phase;
        f64_sum += f64_amp * cos(f64_arg);
    }

    for (u16_idx = 0; u16_idx < pst_sig->interharmonic_count; u16_idx++) {
        const wg_interharmonic_t *pst_ih = &pst_sig->interharmonics[u16_idx];
        double f64_amp = pst_wg->cfg.nominal[u8_ch] * PRV_SQRT2
                       * pst_ih->amplitude_pct / 100.0;
        double f64_arg = pst_ih->order * f64_phase
                       + pst_ih->phase_deg * PRV_DEG2RAD;
        f64_sum += f64_amp * cos(f64_arg);
    }

    for (u16_idx = 0; u16_idx < pst_sig->tone_count; u16_idx++) {
        const wg_tone_t *pst_tone = &pst_sig->tones[u16_idx];
        double f64_amp = pst_wg->cfg.nominal[u8_ch] * PRV_SQRT2
                       * pst_tone->amplitude_pct / 100.0;
        double f64_arg = PRV_TWO_PI * pst_tone->frequency_hz * f64_t_abs
                       + pst_tone->phase_deg * PRV_DEG2RAD;
        f64_sum += f64_amp * cos(f64_arg);
    }

    return f64_sum;
}
