/*
 * wg_cli.c - command-line parsing and merge order for the waveform generator.
 *
 * Merge order: defaults -> scenario (--test) -> config file (--config) ->
 * command-line overrides, in the order the options were given.
 */
#include "wg_config.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WG_MAX_OPTS 64

enum {
    OPT_TEST = 0,
    OPT_SYSTEM,
    OPT_CONFIG,
    OPT_DURATION,
    OPT_ALLOW_LONG,
    OPT_FREQ,
    OPT_AMP,
    OPT_PHASE,
    OPT_HARM,
    OPT_IHARM,
    OPT_TONE,
    OPT_RATE,
    OPT_FORMAT,
    OPT_FRAME,
    OPT_START,
    OPT_OUT,
    OPT_MOD,
    OPT_PHASE_STEP,
    OPT_LIST
};

typedef struct {
    int id;
    const char *val;
} cli_opt;

static const char *const g_chan_names[WG_MAX_CHANNELS] = {
    "Ua", "Ub", "Uc", "Ia", "Ib", "Ic", "In"
};

static const struct {
    const char *name;
    int id;
    int has_val;
} g_opt_tab[] = {
    { "--test", OPT_TEST, 1 },
    { "-t", OPT_TEST, 1 },
    { "--system", OPT_SYSTEM, 1 },
    { "--config", OPT_CONFIG, 1 },
    { "-c", OPT_CONFIG, 1 },
    { "--duration", OPT_DURATION, 1 },
    { "-d", OPT_DURATION, 1 },
    { "--allow-long", OPT_ALLOW_LONG, 0 },
    { "--freq", OPT_FREQ, 1 },
    { "--amp", OPT_AMP, 1 },
    { "--phase", OPT_PHASE, 1 },
    { "--harmonics", OPT_HARM, 1 },
    { "--interharmonics", OPT_IHARM, 1 },
    { "--tone", OPT_TONE, 1 },
    { "--tones", OPT_TONE, 1 },
    { "--mod", OPT_MOD, 1 },
    { "--phase-step", OPT_PHASE_STEP, 1 },
    { "--rate", OPT_RATE, 1 },
    { "--sample-rate", OPT_RATE, 1 },
    { "--format", OPT_FORMAT, 1 },
    { "--frame", OPT_FRAME, 1 },
    { "--frame-samples", OPT_FRAME, 1 },
    { "--start", OPT_START, 1 },
    { "--start-utc", OPT_START, 1 },
    { "--out", OPT_OUT, 1 },
    { "-o", OPT_OUT, 1 },
    { "--list", OPT_LIST, 0 }
};

/* ------------------------------------------------------------------ */
/* small string helpers                                               */
/* ------------------------------------------------------------------ */

static int chan_index(const char *pc_name)
{
    int i;

    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        if (strcmp(pc_name, g_chan_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static char *trim_right(char *pc_s)
{
    size_t n = strlen(pc_s);

    while (n > 0u) {
        char c = pc_s[n - 1u];

        if (c != ' ' && c != '\t') {
            break;
        }
        pc_s[n - 1u] = '\0';
        n -= 1u;
    }
    return pc_s;
}

/* Fetch the next separator-delimited item. 1 = item, 0 = end, -1 = too long. */
static int split_next(const char **ppc_s, char sep, char *pc_buf, size_t cap)
{
    const char *p = *ppc_s;
    const char *q;
    size_t n;

    while (*p == ' ' || *p == '\t') {
        p += 1;
    }
    if (*p == '\0') {
        return 0;
    }
    q = strchr(p, sep);
    n = (q != NULL) ? (size_t)(q - p) : strlen(p);
    if (n == 0u) {
        *ppc_s = (q != NULL) ? q + 1 : p;
        return 1;
    }
    if (n >= cap) {
        return -1;
    }
    memcpy(pc_buf, p, n);
    pc_buf[n] = '\0';
    trim_right(pc_buf);
    *ppc_s = (q != NULL) ? q + 1 : p + n;
    return 1;
}

static int parse_double_exact(const char *pc_s, double *p_d)
{
    char *end = NULL;
    double d;

    if (pc_s == NULL || pc_s[0] == '\0') {
        return -1;
    }
    errno = 0;
    d = strtod(pc_s, &end);
    if (errno == ERANGE || end == pc_s || *end != '\0') {
        return -1;
    }
    *p_d = d;
    return 0;
}

/* Number with an optional trailing '%'. */
static int parse_double_pct(const char *pc_s, double *p_d)
{
    char *end = NULL;
    double d;

    if (pc_s == NULL || pc_s[0] == '\0') {
        return -1;
    }
    errno = 0;
    d = strtod(pc_s, &end);
    if (errno == ERANGE || end == pc_s) {
        return -1;
    }
    if (*end == '%' && end[1] == '\0') {
        *p_d = d;
        return 0;
    }
    if (*end != '\0') {
        return -1;
    }
    *p_d = d;
    return 0;
}

static int parse_int_exact(const char *pc_s, long *p_l)
{
    char *end = NULL;
    long v;

    if (pc_s == NULL || pc_s[0] == '\0') {
        return -1;
    }
    errno = 0;
    v = strtol(pc_s, &end, 10);
    if (errno == ERANGE || end == pc_s || *end != '\0') {
        return -1;
    }
    *p_l = v;
    return 0;
}

static int parse_duration(const char *pc_s, double *p_d)
{
    char *end = NULL;
    double d;

    if (pc_s == NULL || pc_s[0] == '\0') {
        return -1;
    }
    errno = 0;
    d = strtod(pc_s, &end);
    if (errno == ERANGE || end == pc_s || !isfinite(d) || d < 0.0) {
        return -1;
    }
    if (*end == '\0' || strcmp(end, "s") == 0) {
        *p_d = d;
        return 0;
    }
    if (strcmp(end, "ms") == 0) {
        *p_d = d / 1000.0;
        return 0;
    }
    if (strcmp(end, "min") == 0) {
        *p_d = d * 60.0;
        return 0;
    }
    if (strcmp(end, "h") == 0) {
        *p_d = d * 3600.0;
        return 0;
    }
    return -1;
}

/* 仅用于 --allow-long 的提示：按样本格式、通道数与采样率估算文件大小。 */
static double cli_estimate_bytes(const wg_config_t *pst_cfg)
{
    double f64_bytes_per_sample;

    switch (pst_cfg->format) {
    case WG_FMT_F32:
        f64_bytes_per_sample = 4.0;
        break;
    case WG_FMT_I24:
        f64_bytes_per_sample = 3.0;
        break;
    default:
        f64_bytes_per_sample = 8.0;
        break;
    }
    return pst_cfg->duration_s * (double)pst_cfg->sample_rate_hz *
           (double)pst_cfg->channel_count * f64_bytes_per_sample;
}

/* ------------------------------------------------------------------ */
/* timestamp                                                          */
/* ------------------------------------------------------------------ */

static long long days_from_civil(int y, unsigned m, unsigned d)
{
    long long era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;
    unsigned mp;

    y -= (m <= 2u) ? 1 : 0;
    era = (y >= 0) ? (long long)(y / 400) : (long long)((y - 399) / 400);
    yoe = (unsigned)(y - (int)(era * 400));
    mp = (m > 2u) ? (m - 3u) : (m + 9u);
    doy = (153u * mp + 2u) / 5u + d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + (long long)doe - 719468LL;
}

static int parse_start_utc(const char *pc_s, uint64_t *p_ns)
{
    if (strchr(pc_s, 'T') != NULL) {
        int y = 0;
        int mo = 0;
        int d = 0;
        int h = 0;
        int mi = 0;
        int se = 0;
        int n = 0;
        const char *p;
        long long frac = 0;
        long long offset = 0;
        long long sec;
        int k;

        if (sscanf(pc_s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi,
                   &se, &n) != 6) {
            return -1;
        }
        if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
            mi < 0 || mi > 59 || se < 0 || se > 60) {
            return -1;
        }
        p = pc_s + n;
        if (*p == '.') {
            p += 1;
            if (*p < '0' || *p > '9') {
                return -1;
            }
            k = 0;
            while (*p >= '0' && *p <= '9') {
                if (k < 9) {
                    frac = frac * 10 + (long long)(*p - '0');
                    k += 1;
                }
                p += 1;
            }
            while (k < 9) {
                frac *= 10;
                k += 1;
            }
        }
        if (*p == 'Z') {
            p += 1;
        } else if (*p == '+' || *p == '-') {
            int oh = 0;
            int om = 0;
            int sign = (*p == '-') ? -1 : 1;

            if (sscanf(p + 1, "%2d:%2d", &oh, &om) != 2) {
                return -1;
            }
            offset = (long long)sign * (long long)(oh * 3600 + om * 60);
            p += 6;
        }
        if (*p != '\0') {
            return -1;
        }

        sec = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400LL +
              (long long)h * 3600LL + (long long)mi * 60LL + (long long)se -
              offset;
        if (sec < 0) {
            return -1;
        }
        *p_ns = (uint64_t)sec * 1000000000ull + (uint64_t)frac;
        return 0;
    }

    {
        char *end = NULL;
        double d;

        errno = 0;
        d = strtod(pc_s, &end);
        if (errno == ERANGE || end == pc_s || *end != '\0' || d < 0.0) {
            return -1;
        }
        if (d > 9.0e9) {
            return -1;
        }
        *p_ns = (uint64_t)(d * 1.0e9);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* signal overrides                                                   */
/* ------------------------------------------------------------------ */

static int apply_freq(wg_config_t *pst_cfg, const char *pc_val, char *pc_err,
                      size_t err_len)
{
    double f;

    if (parse_double_exact(pc_val, &f) != 0 || f <= 0.0) {
        snprintf(pc_err, err_len, "freq_hz: expected number > 0");
        return -1;
    }
    pst_cfg->signal.frequency.profile = WG_PROFILE_CONST;
    pst_cfg->signal.frequency.from = f;
    pst_cfg->signal.frequency.to = f;
    pst_cfg->signal.frequency.period_s = 0.0;
    return 0;
}

static void set_amp_const(wg_config_t *pst_cfg, int ch, double pct)
{
    pst_cfg->signal.amp_pct[ch].profile = WG_PROFILE_CONST;
    pst_cfg->signal.amp_pct[ch].from = pct;
    pst_cfg->signal.amp_pct[ch].to = pct;
    pst_cfg->signal.amp_pct[ch].period_s = 0.0;
}

static int amp_value(const char *pc_expr, const wg_config_t *pst_cfg, int ch,
                     double *p_pct, char *pc_err, size_t err_len)
{
    char *end = NULL;
    double v;
    double pct;

    errno = 0;
    v = strtod(pc_expr, &end);
    if (errno == ERANGE || end == pc_expr) {
        snprintf(pc_err, err_len,
                 "amp_pct.%s: expected number with %% or V/A",
                 g_chan_names[ch]);
        return -1;
    }

    if (*end == '%' && end[1] == '\0') {
        pct = v;
    } else if ((*end == 'V' || *end == 'v' || *end == 'A' || *end == 'a') &&
               end[1] == '\0') {
        if (pst_cfg->nominal[ch] <= 0.0) {
            snprintf(pc_err, err_len, "amp_pct.%s: expected nonzero nominal",
                     g_chan_names[ch]);
            return -1;
        }
        pct = v / pst_cfg->nominal[ch] * 100.0;
    } else if (*end == '\0') {
        if (pst_cfg->nominal[ch] <= 0.0) {
            snprintf(pc_err, err_len, "amp_pct.%s: expected nonzero nominal",
                     g_chan_names[ch]);
            return -1;
        }
        pct = v / pst_cfg->nominal[ch] * 100.0;
    } else {
        snprintf(pc_err, err_len,
                 "amp_pct.%s: expected number with %% or V/A",
                 g_chan_names[ch]);
        return -1;
    }

    if (pct < 0.0 || pct > 1000.0) {
        snprintf(pc_err, err_len, "amp_pct.%s: expected number in [0, 1000]",
                 g_chan_names[ch]);
        return -1;
    }
    *p_pct = pct;
    return 0;
}

static int apply_amp(wg_config_t *pst_cfg, const char *pc_val, char *pc_err,
                     size_t err_len)
{
    const char *p = pc_val;
    char item[128];
    int rc;

    while ((rc = split_next(&p, ',', item, sizeof(item))) > 0) {
        const char *eq = strchr(item, '=');

        if (eq != NULL) {
            char name[16];
            size_t n = (size_t)(eq - item);
            int ch;
            double pct;

            if (n == 0u || n >= sizeof(name)) {
                snprintf(pc_err, err_len,
                         "amp_pct: expected channel=value list");
                return -1;
            }
            memcpy(name, item, n);
            name[n] = '\0';
            ch = chan_index(name);
            if (ch < 0) {
                snprintf(pc_err, err_len,
                         "amp_pct.%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In",
                         name);
                return -1;
            }
            if (amp_value(eq + 1, pst_cfg, ch, &pct, pc_err, err_len) != 0) {
                return -1;
            }
            set_amp_const(pst_cfg, ch, pct);
        } else {
            uint8_t i;

            for (i = 0u; i < pst_cfg->channel_count; ++i) {
                int ch = (int)pst_cfg->channels[i];
                double pct;

                if (amp_value(item, pst_cfg, ch, &pct, pc_err, err_len) != 0) {
                    return -1;
                }
                set_amp_const(pst_cfg, ch, pct);
            }
        }
    }
    if (rc < 0) {
        snprintf(pc_err, err_len, "amp_pct: expected channel=value list");
        return -1;
    }
    return 0;
}

static int apply_phase(wg_config_t *pst_cfg, const char *pc_val, char *pc_err,
                       size_t err_len)
{
    const char *p = pc_val;
    char item[64];
    int pos = 0;
    int rc;

    while ((rc = split_next(&p, ',', item, sizeof(item))) > 0) {
        const char *eq = strchr(item, '=');
        int ch;
        double deg;

        if (eq != NULL) {
            char name[16];
            size_t n = (size_t)(eq - item);

            if (n == 0u || n >= sizeof(name)) {
                snprintf(pc_err, err_len,
                         "phase_deg: expected channel=value list");
                return -1;
            }
            memcpy(name, item, n);
            name[n] = '\0';
            ch = chan_index(name);
            if (ch < 0) {
                snprintf(pc_err, err_len,
                         "phase_deg.%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In",
                         name);
                return -1;
            }
            if (parse_double_exact(eq + 1, &deg) != 0) {
                snprintf(pc_err, err_len, "phase_deg.%s: expected number",
                         name);
                return -1;
            }
        } else {
            if (pos >= (int)pst_cfg->channel_count) {
                snprintf(pc_err, err_len,
                         "phase_deg: expected at most %u values",
                         (unsigned)pst_cfg->channel_count);
                return -1;
            }
            ch = (int)pst_cfg->channels[pos];
            pos += 1;
            if (parse_double_exact(item, &deg) != 0) {
                snprintf(pc_err, err_len, "phase_deg: expected number");
                return -1;
            }
        }
        pst_cfg->signal.phase_deg[ch] = deg;
    }
    if (rc < 0) {
        snprintf(pc_err, err_len, "phase_deg: expected number list");
        return -1;
    }
    return 0;
}

static int split_key_value(const char *pc_item, char *pc_key, size_t key_cap,
                           const char **ppc_rest)
{
    const char *eq = strchr(pc_item, '=');
    size_t n;

    if (eq == NULL) {
        return -1;
    }
    n = (size_t)(eq - pc_item);
    if (n == 0u || n >= key_cap) {
        return -1;
    }
    memcpy(pc_key, pc_item, n);
    pc_key[n] = '\0';
    *ppc_rest = eq + 1;
    return 0;
}

static int parse_amp_phase(const char *pc_expr, double *p_amp, double *p_phase)
{
    const char *comma = strchr(pc_expr, ',');
    char buf[64];
    size_t n;

    if (comma != NULL) {
        n = (size_t)(comma - pc_expr);
        if (n == 0u || n >= sizeof(buf)) {
            return -1;
        }
        memcpy(buf, pc_expr, n);
        buf[n] = '\0';
        if (parse_double_pct(buf, p_amp) != 0) {
            return -1;
        }
        return parse_double_exact(comma + 1, p_phase);
    }
    return parse_double_pct(pc_expr, p_amp);
}

static void push_harmonic(wg_config_t *pst_cfg, uint32_t order, double amp,
                          double phase)
{
    wg_harmonic_t *pst_h;

    if (pst_cfg->signal.harmonic_count >= (uint16_t)WG_MAX_HARMONICS) {
        return;
    }
    pst_h = &pst_cfg->signal.harmonics[pst_cfg->signal.harmonic_count];
    pst_h->order = order;
    pst_h->amplitude_pct = amp;
    pst_h->phase_deg = phase;
    pst_cfg->signal.harmonic_count += 1u;
}

static void push_interharmonic(wg_config_t *pst_cfg, double order, double amp,
                               double phase)
{
    wg_interharmonic_t *pst_ih;

    if (pst_cfg->signal.interharmonic_count >=
        (uint16_t)WG_MAX_INTERHARMONICS) {
        return;
    }
    pst_ih = &pst_cfg->signal.interharmonics[pst_cfg->signal.interharmonic_count];
    pst_ih->order = order;
    pst_ih->amplitude_pct = amp;
    pst_ih->phase_deg = phase;
    pst_cfg->signal.interharmonic_count += 1u;
}

static void push_tone(wg_config_t *pst_cfg, double freq, double amp,
                      double phase)
{
    wg_tone_t *pst_t;

    if (pst_cfg->signal.tone_count >= (uint16_t)WG_MAX_TONES) {
        return;
    }
    pst_t = &pst_cfg->signal.tones[pst_cfg->signal.tone_count];
    pst_t->frequency_hz = freq;
    pst_t->amplitude_pct = amp;
    pst_t->phase_deg = phase;
    pst_cfg->signal.tone_count += 1u;
}

static int apply_harmonics(wg_config_t *pst_cfg, const char *pc_val,
                           char *pc_err, size_t err_len, int *p_cleared)
{
    const char *p = pc_val;
    char item[128];
    size_t idx = 0u;
    int rc;

    if (*p_cleared == 0) {
        pst_cfg->signal.harmonic_count = 0u;
        *p_cleared = 1;
    }

    while ((rc = split_next(&p, ';', item, sizeof(item))) > 0) {
        char key[64];
        const char *rest = NULL;
        char path[96];
        long order = 0;
        double amp = 0.0;
        double phase = 0.0;

        snprintf(path, sizeof(path), "harmonics[%zu]", idx);
        if (split_key_value(item, key, sizeof(key), &rest) != 0 ||
            parse_int_exact(key, &order) != 0 || order < 2 || order > 64) {
            snprintf(pc_err, err_len, "%s.order: expected integer in [2, 64]",
                     path);
            return -1;
        }
        if (parse_amp_phase(rest, &amp, &phase) != 0 ||
            amp < 0.0 || amp > 1000.0) {
            snprintf(pc_err, err_len, "%s.amp_pct: expected number in [0, 1000]",
                     path);
            return -1;
        }
        push_harmonic(pst_cfg, (uint32_t)order, amp, phase);
        idx += 1u;
    }
    if (rc < 0) {
        snprintf(pc_err, err_len, "harmonics: expected order=amp[,phase] list");
        return -1;
    }
    return 0;
}

static int apply_interharmonics(wg_config_t *pst_cfg, const char *pc_val,
                                char *pc_err, size_t err_len, int *p_cleared)
{
    const char *p = pc_val;
    char item[128];
    size_t idx = 0u;
    int rc;

    if (*p_cleared == 0) {
        pst_cfg->signal.interharmonic_count = 0u;
        *p_cleared = 1;
    }

    while ((rc = split_next(&p, ';', item, sizeof(item))) > 0) {
        char key[64];
        const char *rest = NULL;
        char path[96];
        double order = 0.0;
        double amp = 0.0;
        double phase = 0.0;

        snprintf(path, sizeof(path), "interharmonics[%zu]", idx);
        if (split_key_value(item, key, sizeof(key), &rest) != 0 ||
            parse_double_exact(key, &order) != 0 || order <= 0.0) {
            snprintf(pc_err, err_len, "%s.order: expected number > 0", path);
            return -1;
        }
        if (parse_amp_phase(rest, &amp, &phase) != 0 ||
            amp < 0.0 || amp > 1000.0) {
            snprintf(pc_err, err_len, "%s.amp_pct: expected number in [0, 1000]",
                     path);
            return -1;
        }
        push_interharmonic(pst_cfg, order, amp, phase);
        idx += 1u;
    }
    if (rc < 0) {
        snprintf(pc_err, err_len,
                 "interharmonics: expected order=amp[,phase] list");
        return -1;
    }
    return 0;
}

static int apply_tones(wg_config_t *pst_cfg, const char *pc_val, char *pc_err,
                       size_t err_len, int *p_cleared)
{
    char key[64];
    const char *rest = NULL;
    double freq = 0.0;
    double amp = 0.0;
    double phase = 0.0;

    if (*p_cleared == 0) {
        pst_cfg->signal.tone_count = 0u;
        *p_cleared = 1;
    }

    if (split_key_value(pc_val, key, sizeof(key), &rest) != 0 ||
        parse_double_exact(key, &freq) != 0 || freq <= 0.0) {
        snprintf(pc_err, err_len,
                 "tones[%u].frequency_hz: expected number > 0",
                 (unsigned)pst_cfg->signal.tone_count);
        return -1;
    }
    if (parse_amp_phase(rest, &amp, &phase) != 0 || amp < 0.0 || amp > 1000.0) {
        snprintf(pc_err, err_len, "tones[%u].amp_pct: expected number in [0, 1000]",
                 (unsigned)pst_cfg->signal.tone_count);
        return -1;
    }
    push_tone(pst_cfg, freq, amp, phase);
    return 0;
}

/* ------------------------------------------------------------------ */
/* program overrides (--mod / --phase-step)                            */
/* ------------------------------------------------------------------ */

static int split_colon(const char *pc_s, char a_tok[][64], int max)
{
    const char *p = pc_s;
    int n = 0;

    while (*p != '\0') {
        const char *q = strchr(p, ':');
        size_t len = (q != NULL) ? (size_t)(q - p) : strlen(p);

        if (len == 0u || len >= 64u || n >= max) {
            return -1;
        }
        memcpy(a_tok[n], p, len);
        a_tok[n][len] = '\0';
        n += 1;
        if (q == NULL) {
            break;
        }
        p = q + 1;
    }
    return n;
}

/* 通道名列表（逗号分隔），返回位掩码。 */
static int parse_channel_list(const char *pc_list, uint8_t *pu8_mask)
{
    const char *p = pc_list;
    uint8_t u8_mask = 0u;

    if (*p == '\0') {
        return -1;
    }
    while (*p != '\0') {
        char a_name[8];
        size_t n = 0u;
        int ch;

        while (*p == ' ' || *p == '\t') {
            p += 1;
        }
        while (*p != '\0' && *p != ',') {
            if (n + 1u < sizeof(a_name)) {
                a_name[n] = *p;
                n += 1u;
            }
            p += 1;
        }
        while (n > 0u && (a_name[n - 1u] == ' ' || a_name[n - 1u] == '\t')) {
            n -= 1u;
        }
        a_name[n] = '\0';
        ch = chan_index(a_name);
        if (ch < 0) {
            return -1;
        }
        u8_mask = (uint8_t)(u8_mask | (1u << ch));
        if (*p == ',') {
            p += 1;
        }
    }
    *pu8_mask = u8_mask;
    return 0;
}

/*
 * sine:FM:DEPTH[:PHASE][:channels=Ia,Ib,Ic]
 * rect:FM:DEPTH:DUTY[:PHASE][:channels=Ia,Ib,Ic]
 */
static int parse_mod_option(const char *pc_val, wg_modulator_t *pst_mod,
                            char *pc_err, size_t err_len)
{
    char a_tok[6][64];
    int n = split_colon(pc_val, a_tok, 6);
    double fm = 0.0;
    double depth = 0.0;
    double duty = 50.0;
    double phase = 0.0;
    uint8_t u8_mask = 0u;

    memset(pst_mod, 0, sizeof(*pst_mod));
    if (n < 1) {
        snprintf(pc_err, err_len, "mod: expected at least a kind");
        return -1;
    }
    if (strncmp(a_tok[n - 1], "channels=", 9u) == 0) {
        if (parse_channel_list(a_tok[n - 1] + 9, &u8_mask) != 0) {
            snprintf(pc_err, err_len,
                     "mod: channels= expected Ua|Ub|Uc|Ia|Ib|Ic|In list");
            return -1;
        }
        n -= 1;
    }
    pst_mod->channel_mask = u8_mask;

    if (n >= 3 && strcmp(a_tok[0], "sine") == 0 && (n == 3 || n == 4)) {
        if (parse_double_exact(a_tok[1], &fm) == 0 && fm > 0.0 &&
            parse_double_exact(a_tok[2], &depth) == 0 &&
            depth >= 0.0 && depth <= 1000.0 &&
            (n == 3 || parse_double_exact(a_tok[3], &phase) == 0)) {
            pst_mod->kind = (uint8_t)WG_MOD2_AMP_SINE;
            pst_mod->frequency_hz = fm;
            pst_mod->depth_pct = depth;
            pst_mod->phase_deg = phase;
            return 0;
        }
    }

    if (n >= 4 && strcmp(a_tok[0], "rect") == 0 && (n == 4 || n == 5)) {
        if (parse_double_exact(a_tok[1], &fm) == 0 && fm > 0.0 &&
            parse_double_exact(a_tok[2], &depth) == 0 &&
            depth >= 0.0 && depth <= 1000.0 &&
            parse_double_exact(a_tok[3], &duty) == 0 &&
            duty >= 0.0 && duty <= 100.0 &&
            (n == 4 || parse_double_exact(a_tok[4], &phase) == 0)) {
            pst_mod->kind = (uint8_t)WG_MOD2_AMP_RECT;
            pst_mod->frequency_hz = fm;
            pst_mod->depth_pct = depth;
            pst_mod->duty_pct = duty;
            pst_mod->phase_deg = phase;
            return 0;
        }
    }

    snprintf(pc_err, err_len,
             "mod: expected sine:FM:DEPTH[:PHASE][:channels=...] or "
             "rect:FM:DEPTH:DUTY[:PHASE][:channels=...]");
    return -1;
}

/* TIME_S:ANGLE_DEG */
static int parse_phase_step(const char *pc_val, wg_event_t *pst_evt,
                            char *pc_err, size_t err_len)
{
    char a_tok[2][64];
    double t = 0.0;
    double deg = 0.0;

    memset(pst_evt, 0, sizeof(*pst_evt));
    if (split_colon(pc_val, a_tok, 2) != 2 ||
        parse_double_exact(a_tok[0], &t) != 0 || t < 0.0 ||
        parse_double_exact(a_tok[1], &deg) != 0) {
        snprintf(pc_err, err_len,
                 "phase_step: expected TIME_S:ANGLE_DEG");
        return -1;
    }
    pst_evt->kind = (uint8_t)WG_EVENT_PHASE_STEP;
    pst_evt->time_s = t;
    pst_evt->value = deg;
    return 0;
}

static uint8_t cli_channel_mask(const wg_config_t *pst_cfg)
{
    uint8_t u8_mask = 0u;
    uint8_t i;

    for (i = 0u; i < pst_cfg->channel_count; ++i) {
        u8_mask = (uint8_t)(u8_mask | (1u << pst_cfg->channels[i]));
    }
    return u8_mask;
}

static int cli_add_mod(wg_component_t *pst_comp, const wg_modulator_t *pst_mod,
                       char *pc_err, size_t err_len)
{
    if (pst_comp->modulator_count >= (uint8_t)WG_MAX_MODULATORS) {
        snprintf(pc_err, err_len,
                 "mod: expected at most %u modulators",
                 (unsigned)WG_MAX_MODULATORS);
        return -1;
    }
    pst_comp->modulators[pst_comp->modulator_count] = *pst_mod;
    pst_comp->modulator_count = (uint8_t)(pst_comp->modulator_count + 1u);
    return 0;
}

/*
 * Mirror the legacy signal into a program so CLI modulators/events can attach
 * to the fundamental.  Frequency stays in signal.frequency (the program
 * evaluator falls back to it); amplitude profiles become AMP_* modulators.
 */
static int cli_materialize_program(wg_config_t *pst_cfg, char *pc_err,
                                   size_t err_len)
{
    const wg_signal_t *pst_sig = &pst_cfg->signal;
    wg_program_t *pst_prog = &pst_cfg->program;
    wg_component_t *pst_fund;
    const wg_profile_f64_t *pst_ref;
    uint8_t u8_mask = cli_channel_mask(pst_cfg);
    uint32_t u32_total;
    uint8_t i;
    int all_const = 1;
    int same_profile = 1;

    u32_total = 1u + (uint32_t)pst_sig->harmonic_count
              + (uint32_t)pst_sig->interharmonic_count
              + (uint32_t)pst_sig->tone_count;
    if (u32_total > (uint32_t)WG_MAX_COMPONENTS) {
        snprintf(pc_err, err_len,
                 "program.components: expected at most %u entries",
                 (unsigned)WG_MAX_COMPONENTS);
        return -1;
    }

    memset(pst_prog, 0, sizeof(*pst_prog));

    pst_fund = &pst_prog->components[0];
    memset(pst_fund, 0, sizeof(*pst_fund));
    pst_fund->kind = (uint8_t)WG_COMP_FUNDAMENTAL;
    pst_fund->order = 1.0;
    pst_fund->frequency_hz = pst_sig->frequency.from;
    pst_fund->channel_mask = u8_mask;
    pst_fund->amplitude_ref = 0u;

    pst_ref = &pst_sig->amp_pct[pst_cfg->channels[0]];
    for (i = 0u; i < pst_cfg->channel_count; ++i) {
        const wg_profile_f64_t *p =
            &pst_sig->amp_pct[pst_cfg->channels[i]];

        if (p->profile != (uint8_t)WG_PROFILE_CONST) {
            all_const = 0;
        }
        if (p->profile != pst_ref->profile || p->from != pst_ref->from ||
            p->to != pst_ref->to || p->period_s != pst_ref->period_s) {
            same_profile = 0;
        }
        pst_fund->phase_deg[pst_cfg->channels[i]] =
            pst_sig->phase_deg[pst_cfg->channels[i]];
    }

    if (all_const == 0 && same_profile != 0) {
        wg_modulator_t st_mod;

        for (i = 0u; i < pst_cfg->channel_count; ++i) {
            pst_fund->amp_pct[pst_cfg->channels[i]] = 100.0;
        }
        memset(&st_mod, 0, sizeof(st_mod));
        if (pst_ref->profile == (uint8_t)WG_PROFILE_LINEAR) {
            st_mod.kind = (uint8_t)WG_MOD2_AMP_LINEAR;
            st_mod.from = pst_ref->from;
            st_mod.to = pst_ref->to;
            st_mod.duration_s = pst_cfg->duration_s;
        } else {
            st_mod.kind = (uint8_t)WG_MOD2_AMP_TRIANGLE;
            st_mod.from = pst_ref->from;
            st_mod.to = pst_ref->to;
            st_mod.period_s = pst_ref->period_s;
        }
        if (cli_add_mod(pst_fund, &st_mod, pc_err, err_len) != 0) {
            return -1;
        }
    } else {
        for (i = 0u; i < pst_cfg->channel_count; ++i) {
            pst_fund->amp_pct[pst_cfg->channels[i]] =
                pst_sig->amp_pct[pst_cfg->channels[i]].from;
        }
    }

    if (pst_sig->modulation.type == (uint8_t)WG_MOD_SINE) {
        wg_modulator_t st_mod;

        memset(&st_mod, 0, sizeof(st_mod));
        st_mod.kind = (uint8_t)WG_MOD2_AMP_SINE;
        st_mod.frequency_hz = pst_sig->modulation.frequency_hz;
        st_mod.depth_pct = pst_sig->modulation.depth_pct;
        st_mod.phase_deg = pst_sig->modulation.phase_deg;
        if (cli_add_mod(pst_fund, &st_mod, pc_err, err_len) != 0) {
            return -1;
        }
    } else if (pst_sig->modulation.type == (uint8_t)WG_MOD_RECT) {
        wg_modulator_t st_mod;

        memset(&st_mod, 0, sizeof(st_mod));
        st_mod.kind = (uint8_t)WG_MOD2_AMP_RECT;
        st_mod.frequency_hz = pst_sig->modulation.frequency_hz;
        st_mod.depth_pct = pst_sig->modulation.depth_pct;
        st_mod.duty_pct = pst_sig->modulation.duty_pct;
        st_mod.phase_deg = pst_sig->modulation.phase_deg;
        if (cli_add_mod(pst_fund, &st_mod, pc_err, err_len) != 0) {
            return -1;
        }
    }
    pst_prog->component_count = 1u;

    for (i = 0u; i < pst_sig->harmonic_count; ++i) {
        const wg_harmonic_t *pst_h = &pst_sig->harmonics[i];
        wg_component_t *pst_c = &pst_prog->components[pst_prog->component_count];
        uint8_t k;

        memset(pst_c, 0, sizeof(*pst_c));
        pst_c->kind = (uint8_t)WG_COMP_HARMONIC;
        pst_c->order = (double)pst_h->order;
        pst_c->channel_mask = u8_mask;
        pst_c->amplitude_ref = 1u;
        for (k = 0u; k < pst_cfg->channel_count; ++k) {
            pst_c->amp_pct[pst_cfg->channels[k]] = pst_h->amplitude_pct;
            pst_c->phase_deg[pst_cfg->channels[k]] = pst_h->phase_deg;
        }
        pst_prog->component_count = (uint16_t)(pst_prog->component_count + 1u);
    }

    for (i = 0u; i < pst_sig->interharmonic_count; ++i) {
        const wg_interharmonic_t *pst_ih = &pst_sig->interharmonics[i];
        wg_component_t *pst_c = &pst_prog->components[pst_prog->component_count];
        uint8_t k;

        memset(pst_c, 0, sizeof(*pst_c));
        pst_c->kind = (uint8_t)WG_COMP_INTERHARMONIC;
        pst_c->order = pst_ih->order;
        pst_c->channel_mask = u8_mask;
        pst_c->amplitude_ref = 0u;
        for (k = 0u; k < pst_cfg->channel_count; ++k) {
            pst_c->amp_pct[pst_cfg->channels[k]] = pst_ih->amplitude_pct;
            pst_c->phase_deg[pst_cfg->channels[k]] = pst_ih->phase_deg;
        }
        pst_prog->component_count = (uint16_t)(pst_prog->component_count + 1u);
    }

    for (i = 0u; i < pst_sig->tone_count; ++i) {
        const wg_tone_t *pst_tone = &pst_sig->tones[i];
        wg_component_t *pst_c = &pst_prog->components[pst_prog->component_count];
        uint8_t k;

        memset(pst_c, 0, sizeof(*pst_c));
        pst_c->kind = (uint8_t)WG_COMP_TONE;
        pst_c->frequency_hz = pst_tone->frequency_hz;
        pst_c->channel_mask = u8_mask;
        pst_c->amplitude_ref = 0u;
        for (k = 0u; k < pst_cfg->channel_count; ++k) {
            pst_c->amp_pct[pst_cfg->channels[k]] = pst_tone->amplitude_pct;
            pst_c->phase_deg[pst_cfg->channels[k]] = pst_tone->phase_deg;
        }
        pst_prog->component_count = (uint16_t)(pst_prog->component_count + 1u);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* public entry point                                                 */
/* ------------------------------------------------------------------ */

int32_t wg_cli_parse(int argc, char **argv, wg_config_t *pst_cfg,
                     char *pc_out_path, size_t out_len,
                     char *pc_err, size_t err_len)
{
    cli_opt a_opt[WG_MAX_OPTS];
    wg_modulator_t a_mod[WG_MAX_MODULATORS];
    wg_event_t a_evt[WG_MAX_EVENTS];
    size_t mod_n = 0u;
    size_t evt_n = 0u;
    size_t opt_n = 0u;
    int start = 0;
    int i;
    int32_t s32_sys = 50;
    int has_test = 0;
    int has_cfg = 0;
    int allow_long = 0;
    const char *pc_test = NULL;
    const char *pc_cfg_path = NULL;

    if (pc_err == NULL || err_len == 0u) {
        return -1;
    }
    pc_err[0] = '\0';
    if (pst_cfg == NULL || pc_out_path == NULL || out_len == 0u) {
        snprintf(pc_err, err_len, "cli: invalid argument");
        return -1;
    }
    pc_out_path[0] = '\0';

    wg_config_default(pst_cfg);

    /* Accept either full argv ("wg gen ...") or argv+2. */
    if (argc > 0 && argv[0][0] != '-') {
        start = 1;
    }
    if (start < argc && strcmp(argv[start], "gen") == 0) {
        start += 1;
    }

    for (i = start; i < argc; ++i) {
        const char *pc_tok = argv[i];
        char name[64];
        const char *pc_eq;
        const char *pc_val = NULL;
        size_t n;
        size_t k;
        int found = 0;
        int id = 0;
        int has_val = 0;

        if (pc_tok[0] != '-') {
            snprintf(pc_err, err_len, "%s: expected option", pc_tok);
            return -1;
        }

        pc_eq = strchr(pc_tok, '=');
        n = (pc_eq != NULL) ? (size_t)(pc_eq - pc_tok) : strlen(pc_tok);
        if (n == 0u || n >= sizeof(name)) {
            snprintf(pc_err, err_len, "%s: unknown option", pc_tok);
            return -1;
        }
        memcpy(name, pc_tok, n);
        name[n] = '\0';

        for (k = 0u; k < sizeof(g_opt_tab) / sizeof(g_opt_tab[0]); ++k) {
            if (strcmp(name, g_opt_tab[k].name) == 0) {
                found = 1;
                id = g_opt_tab[k].id;
                has_val = g_opt_tab[k].has_val;
                break;
            }
        }
        if (found == 0) {
            snprintf(pc_err, err_len, "%s: unknown option", name);
            return -1;
        }

        if (has_val != 0) {
            if (pc_eq != NULL) {
                pc_val = pc_eq + 1;
            } else {
                if (i + 1 >= argc) {
                    snprintf(pc_err, err_len, "%s: expected value", name);
                    return -1;
                }
                i += 1;
                pc_val = argv[i];
            }
        } else if (pc_eq != NULL) {
            snprintf(pc_err, err_len, "%s: expected no value", name);
            return -1;
        }

        if (opt_n >= (size_t)WG_MAX_OPTS) {
            snprintf(pc_err, err_len, "cli: too many options");
            return -1;
        }
        a_opt[opt_n].id = id;
        a_opt[opt_n].val = pc_val;
        opt_n += 1u;
    }

    /* --system first: it parameterises the scenario. */
    for (i = 0; i < (int)opt_n; ++i) {
        if (a_opt[i].id == OPT_SYSTEM) {
            long v = 0;

            if (parse_int_exact(a_opt[i].val, &v) != 0 ||
                (v != 50 && v != 60)) {
                snprintf(pc_err, err_len, "system_hz: expected 50 or 60");
                return -1;
            }
            s32_sys = (int32_t)v;
        }
    }

    for (i = 0; i < (int)opt_n; ++i) {
        if (a_opt[i].id == OPT_TEST) {
            pc_test = a_opt[i].val;
            has_test = 1;
        }
    }
    if (has_test != 0 &&
        wg_scenario_apply(pc_test, s32_sys, pst_cfg, pc_err, err_len) != 0) {
        return -1;
    }

    for (i = 0; i < (int)opt_n; ++i) {
        if (a_opt[i].id == OPT_CONFIG) {
            pc_cfg_path = a_opt[i].val;
            has_cfg = 1;
        }
    }
    if (has_cfg != 0 &&
        wg_config_load_json(pc_cfg_path, pst_cfg, pc_err, err_len) != 0) {
        return -1;
    }

    {
        int cleared_h = 0;
        int cleared_ih = 0;
        int cleared_t = 0;
        double d;

        for (i = 0; i < (int)opt_n; ++i) {
            switch (a_opt[i].id) {
            case OPT_DURATION:
                if (parse_duration(a_opt[i].val, &d) != 0 ||
                    d < WG_MIN_DURATION_S) {
                    snprintf(pc_err, err_len,
                             "duration_s: expected number >= 0.001");
                    return -1;
                }
                pst_cfg->duration_s = d;
                break;
            case OPT_ALLOW_LONG:
                allow_long = 1;
                break;
            case OPT_FREQ:
                if (apply_freq(pst_cfg, a_opt[i].val, pc_err, err_len) != 0) {
                    return -1;
                }
                break;
            case OPT_AMP:
                if (apply_amp(pst_cfg, a_opt[i].val, pc_err, err_len) != 0) {
                    return -1;
                }
                break;
            case OPT_PHASE:
                if (apply_phase(pst_cfg, a_opt[i].val, pc_err, err_len) != 0) {
                    return -1;
                }
                break;
            case OPT_HARM:
                if (apply_harmonics(pst_cfg, a_opt[i].val, pc_err, err_len,
                                    &cleared_h) != 0) {
                    return -1;
                }
                break;
            case OPT_IHARM:
                if (apply_interharmonics(pst_cfg, a_opt[i].val, pc_err,
                                         err_len, &cleared_ih) != 0) {
                    return -1;
                }
                break;
            case OPT_TONE:
                if (apply_tones(pst_cfg, a_opt[i].val, pc_err, err_len,
                                &cleared_t) != 0) {
                    return -1;
                }
                break;
            case OPT_MOD:
                if (mod_n >= (size_t)WG_MAX_MODULATORS) {
                    snprintf(pc_err, err_len,
                             "mod: expected at most %u entries",
                             (unsigned)WG_MAX_MODULATORS);
                    return -1;
                }
                if (parse_mod_option(a_opt[i].val, &a_mod[mod_n], pc_err,
                                     err_len) != 0) {
                    return -1;
                }
                mod_n += 1u;
                break;
            case OPT_PHASE_STEP:
                if (evt_n >= (size_t)WG_MAX_EVENTS) {
                    snprintf(pc_err, err_len,
                             "phase_step: expected at most %u entries",
                             (unsigned)WG_MAX_EVENTS);
                    return -1;
                }
                if (parse_phase_step(a_opt[i].val, &a_evt[evt_n], pc_err,
                                     err_len) != 0) {
                    return -1;
                }
                evt_n += 1u;
                break;
            case OPT_RATE: {
                long v = 0;

                if (parse_int_exact(a_opt[i].val, &v) != 0 || v < 1000 ||
                    v > 1000000) {
                    snprintf(pc_err, err_len,
                             "sample_rate_hz: expected integer in [1000, 1000000]");
                    return -1;
                }
                pst_cfg->sample_rate_hz = (uint32_t)v;
                break;
            }
            case OPT_FORMAT:
                if (strcmp(a_opt[i].val, "f64") == 0) {
                    pst_cfg->format = (uint8_t)WG_FMT_F64;
                } else if (strcmp(a_opt[i].val, "f32") == 0) {
                    pst_cfg->format = (uint8_t)WG_FMT_F32;
                } else if (strcmp(a_opt[i].val, "i24") == 0) {
                    pst_cfg->format = (uint8_t)WG_FMT_I24;
                } else {
                    snprintf(pc_err, err_len,
                             "format: expected one of f64|f32|i24");
                    return -1;
                }
                break;
            case OPT_FRAME: {
                long v = 0;

                if (parse_int_exact(a_opt[i].val, &v) != 0 || v < 1 ||
                    v > 65535) {
                    snprintf(pc_err, err_len,
                             "frame_samples: expected integer in [1, 65535]");
                    return -1;
                }
                pst_cfg->frame_samples = (uint32_t)v;
                break;
            }
            case OPT_START:
                if (parse_start_utc(a_opt[i].val,
                                    &pst_cfg->start_utc_ns) != 0) {
                    snprintf(pc_err, err_len,
                             "start_utc_ns: expected RFC3339 or epoch seconds");
                    return -1;
                }
                break;
            case OPT_OUT:
                if (strlen(a_opt[i].val) + 1u > out_len) {
                    snprintf(pc_err, err_len, "out: expected shorter path");
                    return -1;
                }
                memcpy(pc_out_path, a_opt[i].val, strlen(a_opt[i].val) + 1u);
                break;
            case OPT_TEST:
            case OPT_SYSTEM:
            case OPT_CONFIG:
            case OPT_LIST:
                break;
            default:
                break;
            }
        }
    }

    if (mod_n > 0u || evt_n > 0u) {
        wg_component_t *pst_fund = NULL;
        uint8_t u8_mask;
        size_t k;

        if (pst_cfg->segment_count > 0u) {
            snprintf(pc_err, err_len, "mod: not supported with segments");
            return -1;
        }
        if (pst_cfg->program.component_count == 0u &&
            cli_materialize_program(pst_cfg, pc_err, err_len) != 0) {
            return -1;
        }
        for (k = 0u; k < (size_t)pst_cfg->program.component_count; ++k) {
            if (pst_cfg->program.components[k].kind
                == (uint8_t)WG_COMP_FUNDAMENTAL) {
                pst_fund = &pst_cfg->program.components[k];
                break;
            }
        }
        if (pst_fund == NULL) {
            snprintf(pc_err, err_len,
                     "program.components: expected a fundamental component");
            return -1;
        }

        for (k = 0u; k < mod_n; ++k) {
            if (a_mod[k].channel_mask != 0u
                && (a_mod[k].channel_mask
                    & (uint8_t)~pst_fund->channel_mask) != 0u) {
                snprintf(pc_err, err_len,
                         "mod: channels: channel not enabled by configuration");
                return -1;
            }
            if (cli_add_mod(pst_fund, &a_mod[k], pc_err, err_len) != 0) {
                return -1;
            }
        }

        u8_mask = cli_channel_mask(pst_cfg);
        for (k = 0u; k < evt_n; ++k) {
            uint16_t u16_n = pst_cfg->program.event_count;

            if (u16_n >= (uint16_t)WG_MAX_EVENTS) {
                snprintf(pc_err, err_len,
                         "phase_step: expected at most %u entries",
                         (unsigned)WG_MAX_EVENTS);
                return -1;
            }
            if (a_evt[k].time_s > pst_cfg->duration_s) {
                snprintf(pc_err, err_len,
                         "phase_step: expected TIME_S in [0, duration_s]");
                return -1;
            }
            a_evt[k].channel_mask = u8_mask;
            pst_cfg->program.events[u16_n] = a_evt[k];
            pst_cfg->program.event_count = (uint16_t)(u16_n + 1u);
        }
    }

    if (pst_cfg->segment_count > 0u) {
        double total = 0.0;
        uint16_t k;

        for (k = 0u; k < pst_cfg->segment_count; ++k) {
            total += pst_cfg->segments[k].duration_s;
        }
        if (total < pst_cfg->duration_s) {
            snprintf(pc_err, err_len,
                     "segments: expected total duration >= duration_s");
            return -1;
        }
    }

    if (pst_cfg->duration_s > WG_MAX_DURATION_S) {
        double f64_gib = cli_estimate_bytes(pst_cfg) /
                         (1024.0 * 1024.0 * 1024.0);

        if (allow_long == 0) {
            snprintf(pc_err, err_len,
                     "duration_s: %.0f exceeds %.0f; pass --allow-long to "
                     "confirm (estimated output %.1f GiB)",
                     pst_cfg->duration_s, WG_MAX_DURATION_S, f64_gib);
            return -1;
        }
        fprintf(stderr,
                "note: --allow-long enabled; estimated output %.1f GiB\n",
                f64_gib);
    }

    return 0;
}
