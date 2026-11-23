/*
 * wg_config.c - default configuration and single-level JSON loader.
 *
 * Independent IEC 62586 waveform generator. Standard C99 only.
 */
#define _POSIX_C_SOURCE 200809L

#include "wg_config.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* defaults                                                           */
/* ------------------------------------------------------------------ */

static void wg_signal_init(wg_signal_t *pst_sig, double f_hz)
{
    int i;

    memset(pst_sig, 0, sizeof(*pst_sig));

    pst_sig->frequency.profile = WG_PROFILE_CONST;
    pst_sig->frequency.from = f_hz;
    pst_sig->frequency.to = f_hz;
    pst_sig->frequency.period_s = 0.0;

    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        pst_sig->amp_pct[i].profile = WG_PROFILE_CONST;
        pst_sig->amp_pct[i].from = 100.0;
        pst_sig->amp_pct[i].to = 100.0;
        pst_sig->amp_pct[i].period_s = 0.0;
        if (i % 3 == 0) {
            pst_sig->phase_deg[i] = 0.0;
        } else if (i % 3 == 1) {
            pst_sig->phase_deg[i] = -120.0;
        } else {
            pst_sig->phase_deg[i] = 120.0;
        }
    }

    pst_sig->modulation.type = WG_MOD_NONE;
    pst_sig->modulation.frequency_hz = 0.0;
    pst_sig->modulation.depth_pct = 0.0;
    pst_sig->modulation.duty_pct = 50.0;
    pst_sig->modulation.phase_deg = 0.0;
}

static uint64_t wg_now_whole_second_ns(void)
{
#if defined(_WIN32)
    time_t t = time(NULL);
    if (t < (time_t)0) {
        return 0u;
    }
    return (uint64_t)t * 1000000000ull;
#else
    struct timespec st_ts;

    if (clock_gettime(CLOCK_REALTIME, &st_ts) == 0) {
        if (st_ts.tv_sec < 0) {
            return 0u;
        }
        /* floor to whole second */
        return (uint64_t)st_ts.tv_sec * 1000000000ull;
    }
    return (uint64_t)time(NULL) * 1000000000ull;
#endif
}

void wg_config_default(wg_config_t *pst_cfg)
{
    int i;

    if (pst_cfg == NULL) {
        return;
    }

    memset(pst_cfg, 0, sizeof(*pst_cfg));

    pst_cfg->sample_rate_hz = 51200u;
    pst_cfg->start_utc_ns = wg_now_whole_second_ns();
    pst_cfg->duration_s = 60.0;

    pst_cfg->channel_count = 3u;
    pst_cfg->channels[0] = (uint8_t)WG_CH_UA;
    pst_cfg->channels[1] = (uint8_t)WG_CH_UB;
    pst_cfg->channels[2] = (uint8_t)WG_CH_UC;

    for (i = 0; i < WG_MAX_CHANNELS; ++i) {
        pst_cfg->nominal[i] = (i < 3) ? 220.0 : 5.0;
    }

    pst_cfg->format = (uint8_t)WG_FMT_F64;
    pst_cfg->frame_samples = 256u;

    wg_signal_init(&pst_cfg->signal, 50.0);
    pst_cfg->segment_count = 0u;
}

/* ------------------------------------------------------------------ */
/* minimal JSON DOM                                                   */
/* ------------------------------------------------------------------ */

enum {
    J_NULL = 0,
    J_BOOL,
    J_NUM,
    J_STR,
    J_ARR,
    J_OBJ
};

typedef struct jval jval;

struct jval {
    int type;
    int bval;
    double num;
    char *str;
    jval **items;
    char **keys;
    size_t count;
    size_t cap;
};

typedef struct {
    const char *p;
    int depth;
    int oom;
} jparser;

static jval *jnew(int type)
{
    jval *v = (jval *)calloc(1u, sizeof(*v));

    if (v != NULL) {
        v->type = type;
    }
    return v;
}

static void jfree(jval *v)
{
    size_t i;

    if (v == NULL) {
        return;
    }
    free(v->str);
    for (i = 0; i < v->count; ++i) {
        if (v->keys != NULL) {
            free(v->keys[i]);
        }
        jfree(v->items[i]);
    }
    free(v->keys);
    free(v->items);
    free(v);
}

static int jpush(jval *v, char *key, jval *item)
{
    if (v->count == v->cap) {
        size_t nc = (v->cap == 0u) ? 4u : v->cap * 2u;
        jval **ni = (jval **)realloc(v->items, nc * sizeof(*ni));

        if (ni == NULL) {
            return -1;
        }
        v->items = ni;

        if (v->type == J_OBJ) {
            char **nk = (char **)realloc(v->keys, nc * sizeof(*nk));

            if (nk == NULL) {
                return -1;
            }
            v->keys = nk;
        }
        v->cap = nc;
    }

    v->items[v->count] = item;
    if (v->type == J_OBJ) {
        v->keys[v->count] = key;
    }
    v->count += 1u;
    return 0;
}

static void jskip_ws(jparser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' ||
           *ps->p == '\n') {
        ps->p += 1;
    }
}

static int jis_digit(char c)
{
    return (c >= '0' && c <= '9') ? 1 : 0;
}

static int jhex(char c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

/* Parse a quoted string; ps->p must point at the opening quote. */
static char *jstring_token(jparser *ps)
{
    const char *s;
    const char *q;
    char *out;
    size_t cap;
    size_t n = 0u;

    if (*ps->p != '"') {
        return NULL;
    }

    s = ps->p + 1;
    q = s;
    while (*q != '\0' && *q != '"') {
        if (*q == '\\' && q[1] != '\0') {
            q += 2;
        } else {
            q += 1;
        }
    }
    if (*q != '"') {
        return NULL;
    }

    cap = (size_t)(q - s);
    out = (char *)malloc(cap + 1u);
    if (out == NULL) {
        ps->oom = 1;
        return NULL;
    }

    ps->p = s;
    while (*ps->p != '\0' && *ps->p != '"') {
        unsigned char c = (unsigned char)*ps->p;

        ps->p += 1;
        if (c == (unsigned char)'\\') {
            char e = *ps->p;

            ps->p += 1;
            switch (e) {
            case '"':
                out[n++] = '"';
                break;
            case '\\':
                out[n++] = '\\';
                break;
            case '/':
                out[n++] = '/';
                break;
            case 'b':
                out[n++] = '\b';
                break;
            case 'f':
                out[n++] = '\f';
                break;
            case 'n':
                out[n++] = '\n';
                break;
            case 'r':
                out[n++] = '\r';
                break;
            case 't':
                out[n++] = '\t';
                break;
            case 'u': {
                unsigned cp = 0u;
                int k;

                for (k = 0; k < 4; ++k) {
                    int d = jhex(ps->p[k]);

                    if (d < 0) {
                        free(out);
                        return NULL;
                    }
                    cp = cp * 16u + (unsigned)d;
                }
                ps->p += 4;
                if (cp >= 0xD800u && cp <= 0xDFFFu) {
                    free(out);
                    return NULL;
                }
                if (cp < 0x80u) {
                    out[n++] = (char)cp;
                } else if (cp < 0x800u) {
                    out[n++] = (char)(0xC0u | (cp >> 6));
                    out[n++] = (char)(0x80u | (cp & 0x3Fu));
                } else {
                    out[n++] = (char)(0xE0u | (cp >> 12));
                    out[n++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                    out[n++] = (char)(0x80u | (cp & 0x3Fu));
                }
                break;
            }
            default:
                free(out);
                return NULL;
            }
        } else {
            out[n++] = (char)c;
        }
    }

    if (*ps->p != '"') {
        free(out);
        return NULL;
    }
    ps->p += 1;
    out[n] = '\0';
    return out;
}

static int jnumber_token(jparser *ps, double *p_d)
{
    const char *s = ps->p;
    const char *q = s;
    char *end = NULL;
    double d;

    if (*q == '-') {
        q += 1;
    }
    if (*q == '0') {
        q += 1;
    } else if (*q >= '1' && *q <= '9') {
        while (jis_digit(*q)) {
            q += 1;
        }
    } else {
        return -1;
    }
    if (*q == '.') {
        q += 1;
        if (!jis_digit(*q)) {
            return -1;
        }
        while (jis_digit(*q)) {
            q += 1;
        }
    }
    if (*q == 'e' || *q == 'E') {
        q += 1;
        if (*q == '+' || *q == '-') {
            q += 1;
        }
        if (!jis_digit(*q)) {
            return -1;
        }
        while (jis_digit(*q)) {
            q += 1;
        }
    }

    errno = 0;
    d = strtod(s, &end);
    if (errno == ERANGE || end != q) {
        return -1;
    }

    ps->p = q;
    *p_d = d;
    return 0;
}

static jval *jparse_value(jparser *ps);

static jval *jarray_token(jparser *ps)
{
    jval *v = jnew(J_ARR);

    if (v == NULL) {
        ps->oom = 1;
        return NULL;
    }

    ps->p += 1; /* '[' */
    jskip_ws(ps);
    if (*ps->p == ']') {
        ps->p += 1;
        return v;
    }

    for (;;) {
        jval *item = jparse_value(ps);

        if (item == NULL) {
            jfree(v);
            return NULL;
        }
        if (jpush(v, NULL, item) != 0) {
            jfree(item);
            jfree(v);
            ps->oom = 1;
            return NULL;
        }
        jskip_ws(ps);
        if (*ps->p == ',') {
            ps->p += 1;
            jskip_ws(ps);
            continue;
        }
        if (*ps->p == ']') {
            ps->p += 1;
            return v;
        }
        jfree(v);
        return NULL;
    }
}

static jval *jobject_token(jparser *ps)
{
    jval *v = jnew(J_OBJ);

    if (v == NULL) {
        ps->oom = 1;
        return NULL;
    }

    ps->p += 1; /* '{' */
    jskip_ws(ps);
    if (*ps->p == '}') {
        ps->p += 1;
        return v;
    }

    for (;;) {
        char *key;
        jval *item;

        jskip_ws(ps);
        key = jstring_token(ps);
        if (key == NULL) {
            jfree(v);
            return NULL;
        }
        jskip_ws(ps);
        if (*ps->p != ':') {
            free(key);
            jfree(v);
            return NULL;
        }
        ps->p += 1;
        item = jparse_value(ps);
        if (item == NULL) {
            free(key);
            jfree(v);
            return NULL;
        }
        if (jpush(v, key, item) != 0) {
            free(key);
            jfree(item);
            jfree(v);
            ps->oom = 1;
            return NULL;
        }
        jskip_ws(ps);
        if (*ps->p == ',') {
            ps->p += 1;
            continue;
        }
        if (*ps->p == '}') {
            ps->p += 1;
            return v;
        }
        jfree(v);
        return NULL;
    }
}

static jval *jparse_value(jparser *ps)
{
    jval *v;

    jskip_ws(ps);
    if (ps->depth > 16) {
        return NULL;
    }

    switch (*ps->p) {
    case '{':
        ps->depth += 1;
        v = jobject_token(ps);
        ps->depth -= 1;
        return v;
    case '[':
        ps->depth += 1;
        v = jarray_token(ps);
        ps->depth -= 1;
        return v;
    case '"': {
        char *s = jstring_token(ps);

        if (s == NULL) {
            return NULL;
        }
        v = jnew(J_STR);
        if (v == NULL) {
            free(s);
            ps->oom = 1;
            return NULL;
        }
        v->str = s;
        return v;
    }
    case 't':
        if (strncmp(ps->p, "true", 4u) == 0) {
            ps->p += 4;
            v = jnew(J_BOOL);
            if (v != NULL) {
                v->bval = 1;
            }
            return v;
        }
        return NULL;
    case 'f':
        if (strncmp(ps->p, "false", 5u) == 0) {
            ps->p += 5;
            v = jnew(J_BOOL);
            if (v != NULL) {
                v->bval = 0;
            }
            return v;
        }
        return NULL;
    case 'n':
        if (strncmp(ps->p, "null", 4u) == 0) {
            ps->p += 4;
            return jnew(J_NULL);
        }
        return NULL;
    default: {
        double d;

        if (jnumber_token(ps, &d) != 0) {
            return NULL;
        }
        v = jnew(J_NUM);
        if (v == NULL) {
            ps->oom = 1;
            return NULL;
        }
        v->num = d;
        return v;
    }
    }
}

static jval *jparse_document(const char *pc_text)
{
    jparser ps;
    jval *v;

    ps.p = pc_text;
    ps.depth = 0;
    ps.oom = 0;

    v = jparse_value(&ps);
    if (v == NULL) {
        return NULL;
    }
    jskip_ws(&ps);
    if (*ps.p != '\0') {
        jfree(v);
        return NULL;
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* field helpers                                                      */
/* ------------------------------------------------------------------ */

static const char *const g_chan_names[WG_MAX_CHANNELS] = {
    "Ua", "Ub", "Uc", "Ia", "Ib", "Ic", "In"
};

typedef struct {
    wg_config_t *cfg;
    char *err;
    size_t elen;
} cfg_ctx;

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

static const jval *jo_get(const jval *pst_obj, const char *pc_key)
{
    size_t i;

    for (i = 0; i < pst_obj->count; ++i) {
        if (strcmp(pst_obj->keys[i], pc_key) == 0) {
            return pst_obj->items[i];
        }
    }
    return NULL;
}

static int jo_unknown(const jval *pst_obj, const char *const *ppc_allowed,
                      size_t allowed_n, const char *pc_prefix,
                      char *pc_err, size_t err_len)
{
    size_t i;

    for (i = 0; i < pst_obj->count; ++i) {
        size_t k;
        int ok = 0;

        for (k = 0; k < allowed_n; ++k) {
            if (strcmp(pst_obj->keys[i], ppc_allowed[k]) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            if (pc_prefix != NULL && pc_prefix[0] != '\0') {
                snprintf(pc_err, err_len, "%s.%s: unknown field", pc_prefix,
                         pst_obj->keys[i]);
            } else {
                snprintf(pc_err, err_len, "%s: unknown field", pst_obj->keys[i]);
            }
            return -1;
        }
    }
    return 0;
}

static int want_number(cfg_ctx *pst_x, const jval *pst_v, const char *pc_path,
                       double *p_d)
{
    if (pst_v == NULL || pst_v->type != J_NUM) {
        snprintf(pst_x->err, pst_x->elen, "%s: expected number", pc_path);
        return -1;
    }
    *p_d = pst_v->num;
    return 0;
}

static int want_range(cfg_ctx *pst_x, const jval *pst_v, const char *pc_path,
                      double lo, double hi, double *p_d)
{
    if (pst_v == NULL || pst_v->type != J_NUM || !isfinite(pst_v->num) ||
        pst_v->num < lo || pst_v->num > hi) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s: expected number in [%g, %g]", pc_path, lo, hi);
        return -1;
    }
    *p_d = pst_v->num;
    return 0;
}

static int want_min(cfg_ctx *pst_x, const jval *pst_v, const char *pc_path,
                    double lo, double *p_d)
{
    if (pst_v == NULL || pst_v->type != J_NUM || !isfinite(pst_v->num) ||
        pst_v->num < lo) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s: expected number >= %g", pc_path, lo);
        return -1;
    }
    *p_d = pst_v->num;
    return 0;
}

static int want_int(cfg_ctx *pst_x, const jval *pst_v, const char *pc_path,
                    double lo, double hi, double *p_d)
{
    if (pst_v == NULL || pst_v->type != J_NUM ||
        pst_v->num < lo || pst_v->num > hi ||
        (double)(long long)pst_v->num != pst_v->num) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s: expected integer in [%.0f, %.0f]", pc_path, lo, hi);
        return -1;
    }
    *p_d = pst_v->num;
    return 0;
}

static int want_u64(cfg_ctx *pst_x, const jval *pst_v, const char *pc_path,
                    uint64_t *p_u64)
{
    if (pst_v == NULL || pst_v->type != J_NUM ||
        pst_v->num < 0.0 || pst_v->num > 9.0e18 ||
        (double)(unsigned long long)pst_v->num != pst_v->num) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s: expected integer >= 0", pc_path);
        return -1;
    }
    *p_u64 = (uint64_t)(unsigned long long)pst_v->num;
    return 0;
}

static int set_amp_const(cfg_ctx *pst_x, wg_signal_t *pst_sig, int ch,
                         double pct, const char *pc_path)
{
    if (pct < 0.0 || pct > 1000.0) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s: expected number in [0, 1000]", pc_path);
        return -1;
    }
    pst_sig->amp_pct[ch].profile = WG_PROFILE_CONST;
    pst_sig->amp_pct[ch].from = pct;
    pst_sig->amp_pct[ch].to = pct;
    pst_sig->amp_pct[ch].period_s = 0.0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* signal object                                                      */
/* ------------------------------------------------------------------ */

static int load_amp_pct(cfg_ctx *pst_x, const jval *pst_v, wg_signal_t *pst_sig,
                        const char *pc_prefix)
{
    char path[160];

    if (pst_v->type == J_NUM) {
        uint8_t i;

        for (i = 0; i < pst_x->cfg->channel_count; ++i) {
            int ch = (int)pst_x->cfg->channels[i];

            snprintf(path, sizeof(path), "%s.amp_pct.%s", pc_prefix,
                     g_chan_names[ch]);
            if (set_amp_const(pst_x, pst_sig, ch, pst_v->num, path) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.amp_pct: expected number in [0, 1000] or object",
                 pc_prefix);
        return -1;
    }

    {
        size_t k;

        for (k = 0; k < pst_v->count; ++k) {
            int ch = chan_index(pst_v->keys[k]);

            snprintf(path, sizeof(path), "%s.amp_pct.%s", pc_prefix,
                     pst_v->keys[k]);
            if (ch < 0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            if (pst_v->items[k]->type != J_NUM) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected number in [0, 1000]", path);
                return -1;
            }
            if (set_amp_const(pst_x, pst_sig, ch, pst_v->items[k]->num,
                              path) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int load_phase_deg(cfg_ctx *pst_x, const jval *pst_v,
                          wg_signal_t *pst_sig, const char *pc_prefix)
{
    char path[160];

    if (pst_v->type == J_OBJ) {
        size_t k;

        for (k = 0; k < pst_v->count; ++k) {
            int ch = chan_index(pst_v->keys[k]);

            snprintf(path, sizeof(path), "%s.phase_deg.%s", pc_prefix,
                     pst_v->keys[k]);
            if (ch < 0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            if (pst_v->items[k]->type != J_NUM) {
                snprintf(pst_x->err, pst_x->elen, "%s: expected number", path);
                return -1;
            }
            pst_sig->phase_deg[ch] = pst_v->items[k]->num;
        }
        return 0;
    }

    if (pst_v->type == J_ARR) {
        size_t k;

        if (pst_v->count > (size_t)pst_x->cfg->channel_count) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.phase_deg: expected at most %u entries", pc_prefix,
                     (unsigned)pst_x->cfg->channel_count);
            return -1;
        }
        for (k = 0; k < pst_v->count; ++k) {
            int ch = (int)pst_x->cfg->channels[k];

            snprintf(path, sizeof(path), "%s.phase_deg[%zu]", pc_prefix, k);
            if (pst_v->items[k]->type != J_NUM) {
                snprintf(pst_x->err, pst_x->elen, "%s: expected number", path);
                return -1;
            }
            pst_sig->phase_deg[ch] = pst_v->items[k]->num;
        }
        return 0;
    }

    snprintf(pst_x->err, pst_x->elen,
             "%s.phase_deg: expected object or array of numbers", pc_prefix);
    return -1;
}

static int load_harmonics(cfg_ctx *pst_x, const jval *pst_v,
                          wg_signal_t *pst_sig, const char *pc_prefix)
{
    static const char *const keys[4] = {
        "order", "amp_pct", "amplitude_pct", "phase_deg"
    };
    char path[160];
    size_t k;

    if (pst_v->type != J_ARR) {
        snprintf(pst_x->err, pst_x->elen, "%s.harmonics: expected array",
                 pc_prefix);
        return -1;
    }
    if (pst_v->count > (size_t)WG_MAX_HARMONICS) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.harmonics: expected at most %u entries", pc_prefix,
                 (unsigned)WG_MAX_HARMONICS);
        return -1;
    }

    pst_sig->harmonic_count = 0u;
    for (k = 0; k < pst_v->count; ++k) {
        const jval *e = pst_v->items[k];
        const jval *jv;
        double order;
        double amp;
        double ph = 0.0;
        wg_harmonic_t *h;

        snprintf(path, sizeof(path), "%s.harmonics[%zu]", pc_prefix, k);
        if (e->type != J_OBJ) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected object", path);
            return -1;
        }
        if (jo_unknown(e, keys, 4u, path, pst_x->err, pst_x->elen) != 0) {
            return -1;
        }

        snprintf(path, sizeof(path), "%s.harmonics[%zu].order", pc_prefix, k);
        if (want_int(pst_x, jo_get(e, "order"), path, 2.0, 64.0,
                     &order) != 0) {
            return -1;
        }

        jv = jo_get(e, "amp_pct");
        if (jv == NULL) {
            jv = jo_get(e, "amplitude_pct");
        }
        snprintf(path, sizeof(path), "%s.harmonics[%zu].amp_pct", pc_prefix, k);
        if (want_range(pst_x, jv, path, 0.0, 1000.0, &amp) != 0) {
            return -1;
        }

        jv = jo_get(e, "phase_deg");
        if (jv != NULL) {
            snprintf(path, sizeof(path), "%s.harmonics[%zu].phase_deg",
                     pc_prefix, k);
            if (want_number(pst_x, jv, path, &ph) != 0) {
                return -1;
            }
        }

        h = &pst_sig->harmonics[pst_sig->harmonic_count];
        h->order = (uint32_t)order;
        h->amplitude_pct = amp;
        h->phase_deg = ph;
        pst_sig->harmonic_count += 1u;
    }
    return 0;
}

static int load_interharmonics(cfg_ctx *pst_x, const jval *pst_v,
                               wg_signal_t *pst_sig, const char *pc_prefix)
{
    static const char *const keys[4] = {
        "order", "amp_pct", "amplitude_pct", "phase_deg"
    };
    char path[160];
    size_t k;

    if (pst_v->type != J_ARR) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.interharmonics: expected array", pc_prefix);
        return -1;
    }
    if (pst_v->count > (size_t)WG_MAX_INTERHARMONICS) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.interharmonics: expected at most %u entries", pc_prefix,
                 (unsigned)WG_MAX_INTERHARMONICS);
        return -1;
    }

    pst_sig->interharmonic_count = 0u;
    for (k = 0; k < pst_v->count; ++k) {
        const jval *e = pst_v->items[k];
        const jval *jv;
        double order;
        double amp;
        double ph = 0.0;
        wg_interharmonic_t *ih;

        snprintf(path, sizeof(path), "%s.interharmonics[%zu]", pc_prefix, k);
        if (e->type != J_OBJ) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected object", path);
            return -1;
        }
        if (jo_unknown(e, keys, 4u, path, pst_x->err, pst_x->elen) != 0) {
            return -1;
        }

        snprintf(path, sizeof(path), "%s.interharmonics[%zu].order", pc_prefix,
                 k);
        jv = jo_get(e, "order");
        if (jv == NULL || jv->type != J_NUM || jv->num <= 0.0) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected number > 0", path);
            return -1;
        }
        order = jv->num;

        jv = jo_get(e, "amp_pct");
        if (jv == NULL) {
            jv = jo_get(e, "amplitude_pct");
        }
        snprintf(path, sizeof(path), "%s.interharmonics[%zu].amp_pct",
                 pc_prefix, k);
        if (want_range(pst_x, jv, path, 0.0, 1000.0, &amp) != 0) {
            return -1;
        }

        jv = jo_get(e, "phase_deg");
        if (jv != NULL) {
            snprintf(path, sizeof(path), "%s.interharmonics[%zu].phase_deg",
                     pc_prefix, k);
            if (want_number(pst_x, jv, path, &ph) != 0) {
                return -1;
            }
        }

        ih = &pst_sig->interharmonics[pst_sig->interharmonic_count];
        ih->order = order;
        ih->amplitude_pct = amp;
        ih->phase_deg = ph;
        pst_sig->interharmonic_count += 1u;
    }
    return 0;
}

static int load_tones(cfg_ctx *pst_x, const jval *pst_v, wg_signal_t *pst_sig,
                      const char *pc_prefix)
{
    static const char *const keys[4] = {
        "frequency_hz", "amp_pct", "amplitude_pct", "phase_deg"
    };
    char path[160];
    size_t k;

    if (pst_v->type != J_ARR) {
        snprintf(pst_x->err, pst_x->elen, "%s.tones: expected array",
                 pc_prefix);
        return -1;
    }
    if (pst_v->count > (size_t)WG_MAX_TONES) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.tones: expected at most %u entries", pc_prefix,
                 (unsigned)WG_MAX_TONES);
        return -1;
    }

    pst_sig->tone_count = 0u;
    for (k = 0; k < pst_v->count; ++k) {
        const jval *e = pst_v->items[k];
        const jval *jv;
        double freq;
        double amp;
        double ph = 0.0;
        wg_tone_t *t;

        snprintf(path, sizeof(path), "%s.tones[%zu]", pc_prefix, k);
        if (e->type != J_OBJ) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected object", path);
            return -1;
        }
        if (jo_unknown(e, keys, 4u, path, pst_x->err, pst_x->elen) != 0) {
            return -1;
        }

        snprintf(path, sizeof(path), "%s.tones[%zu].frequency_hz", pc_prefix, k);
        jv = jo_get(e, "frequency_hz");
        if (jv == NULL || jv->type != J_NUM || jv->num <= 0.0) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected number > 0", path);
            return -1;
        }
        freq = jv->num;

        jv = jo_get(e, "amp_pct");
        if (jv == NULL) {
            jv = jo_get(e, "amplitude_pct");
        }
        snprintf(path, sizeof(path), "%s.tones[%zu].amp_pct", pc_prefix, k);
        if (want_range(pst_x, jv, path, 0.0, 1000.0, &amp) != 0) {
            return -1;
        }

        jv = jo_get(e, "phase_deg");
        if (jv != NULL) {
            snprintf(path, sizeof(path), "%s.tones[%zu].phase_deg", pc_prefix,
                     k);
            if (want_number(pst_x, jv, path, &ph) != 0) {
                return -1;
            }
        }

        t = &pst_sig->tones[pst_sig->tone_count];
        t->frequency_hz = freq;
        t->amplitude_pct = amp;
        t->phase_deg = ph;
        pst_sig->tone_count += 1u;
    }
    return 0;
}

static int load_modulation(cfg_ctx *pst_x, const jval *pst_v,
                           wg_signal_t *pst_sig, const char *pc_prefix)
{
    static const char *const keys[5] = {
        "type", "frequency_hz", "depth_pct", "duty_pct", "phase_deg"
    };
    char path[160];
    const jval *jv;
    double depth = 0.0;
    double duty = 50.0;
    double phase = 0.0;
    double freq = 0.0;
    int type = (int)WG_MOD_NONE;

    if (pst_v->type == J_NULL) {
        pst_sig->modulation.type = (uint8_t)WG_MOD_NONE;
        pst_sig->modulation.frequency_hz = 0.0;
        pst_sig->modulation.depth_pct = 0.0;
        pst_sig->modulation.duty_pct = 50.0;
        pst_sig->modulation.phase_deg = 0.0;
        return 0;
    }
    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.modulation: expected object or null", pc_prefix);
        return -1;
    }
    if (jo_unknown(pst_v, keys, 5u, pc_prefix, pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "type");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.modulation.type", pc_prefix);
        if (jv->type != J_STR) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected one of none|sine|rect", path);
            return -1;
        }
        if (strcmp(jv->str, "none") == 0) {
            type = (int)WG_MOD_NONE;
        } else if (strcmp(jv->str, "sine") == 0) {
            type = (int)WG_MOD_SINE;
        } else if (strcmp(jv->str, "rect") == 0) {
            type = (int)WG_MOD_RECT;
        } else {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected one of none|sine|rect", path);
            return -1;
        }
    }

    jv = jo_get(pst_v, "frequency_hz");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.modulation.frequency_hz", pc_prefix);
        if (jv->type != J_NUM || jv->num <= 0.0) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected number > 0", path);
            return -1;
        }
        freq = jv->num;
    } else if (type != (int)WG_MOD_NONE) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.modulation.frequency_hz: expected number > 0", pc_prefix);
        return -1;
    }

    jv = jo_get(pst_v, "depth_pct");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.modulation.depth_pct", pc_prefix);
        if (want_range(pst_x, jv, path, 0.0, 100.0, &depth) != 0) {
            return -1;
        }
    }

    jv = jo_get(pst_v, "duty_pct");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.modulation.duty_pct", pc_prefix);
        if (want_range(pst_x, jv, path, 0.0, 100.0, &duty) != 0) {
            return -1;
        }
    }

    jv = jo_get(pst_v, "phase_deg");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.modulation.phase_deg", pc_prefix);
        if (want_number(pst_x, jv, path, &phase) != 0) {
            return -1;
        }
    }

    pst_sig->modulation.type = (uint8_t)type;
    pst_sig->modulation.frequency_hz = freq;
    pst_sig->modulation.depth_pct = depth;
    pst_sig->modulation.duty_pct = duty;
    pst_sig->modulation.phase_deg = phase;
    return 0;
}

static int load_signal(cfg_ctx *pst_x, const jval *pst_v, wg_signal_t *pst_sig,
                       const char *pc_prefix)
{
    static const char *const keys[7] = {
        "freq_hz", "amp_pct", "phase_deg", "harmonics",
        "interharmonics", "tones", "modulation"
    };
    char path[160];
    const jval *jv;

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "%s: expected object", pc_prefix);
        return -1;
    }
    if (jo_unknown(pst_v, keys, 7u, pc_prefix, pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "freq_hz");
    if (jv != NULL) {
        double f;

        snprintf(path, sizeof(path), "%s.freq_hz", pc_prefix);
        if (jv->type != J_NUM || jv->num <= 0.0) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected number > 0", path);
            return -1;
        }
        f = jv->num;
        pst_sig->frequency.profile = WG_PROFILE_CONST;
        pst_sig->frequency.from = f;
        pst_sig->frequency.to = f;
        pst_sig->frequency.period_s = 0.0;
    }

    jv = jo_get(pst_v, "amp_pct");
    if (jv != NULL && load_amp_pct(pst_x, jv, pst_sig, pc_prefix) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "phase_deg");
    if (jv != NULL && load_phase_deg(pst_x, jv, pst_sig, pc_prefix) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "harmonics");
    if (jv != NULL && load_harmonics(pst_x, jv, pst_sig, pc_prefix) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "interharmonics");
    if (jv != NULL &&
        load_interharmonics(pst_x, jv, pst_sig, pc_prefix) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "tones");
    if (jv != NULL && load_tones(pst_x, jv, pst_sig, pc_prefix) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "modulation");
    if (jv != NULL && load_modulation(pst_x, jv, pst_sig, pc_prefix) != 0) {
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* program: components, modulators and events                         */
/* ------------------------------------------------------------------ */

static const char *const g_comp_kind_names[4] = {
    "fundamental", "harmonic", "interharmonic", "tone"
};

static const char *const g_mod2_kind_names[10] = {
    "none", "amp_const", "amp_linear", "amp_triangle", "amp_step",
    "amp_sine", "amp_rect", "freq_linear", "freq_triangle", "freq_step"
};

static const char *const g_event_kind_names[2] = {
    "amp_step", "phase_step"
};

static int comp_kind_index(const char *pc_name)
{
    int i;

    for (i = 0; i < 4; ++i) {
        if (strcmp(pc_name, g_comp_kind_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int mod2_kind_index(const char *pc_name)
{
    int i;

    for (i = 1; i < 10; ++i) { /* "none" is not accepted from JSON */
        if (strcmp(pc_name, g_mod2_kind_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int event_kind_index(const char *pc_name)
{
    int i;

    for (i = 0; i < 2; ++i) {
        if (strcmp(pc_name, g_event_kind_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static uint8_t enabled_channel_mask(const cfg_ctx *pst_x)
{
    uint8_t u8_mask = 0u;
    uint8_t i;

    for (i = 0u; i < pst_x->cfg->channel_count; ++i) {
        u8_mask = (uint8_t)(u8_mask | (1u << pst_x->cfg->channels[i]));
    }
    return u8_mask;
}

/* Channel list: array of names or a comma-separated string. */
static int load_channel_mask(cfg_ctx *pst_x, const jval *pst_v,
                             const char *pc_prefix, const char *pc_field,
                             uint8_t *p_u8_mask)
{
    char path[200];
    uint8_t u8_mask = 0u;
    size_t k;

    if (pst_v->type == J_STR) {
        const char *p = pst_v->str;

        if (pst_v->str[0] == '\0') {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.%s: expected at least one channel", pc_prefix,
                     pc_field);
            return -1;
        }
        while (*p != '\0') {
            char name[8];
            size_t n = 0u;
            int ch;

            while (*p == ' ' || *p == '\t') {
                p += 1;
            }
            while (*p != '\0' && *p != ',') {
                if (n + 1u < sizeof(name)) {
                    name[n] = *p;
                    n += 1u;
                }
                p += 1;
            }
            while (n > 0u && (name[n - 1u] == ' ' || name[n - 1u] == '\t')) {
                n -= 1u;
            }
            name[n] = '\0';
            ch = chan_index(name);
            snprintf(path, sizeof(path), "%s.%s", pc_prefix, pc_field);
            if (ch < 0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            u8_mask = (uint8_t)(u8_mask | (1u << ch));
            if (*p == ',') {
                p += 1;
            }
        }
        *p_u8_mask = u8_mask;
        return 0;
    }

    if (pst_v->type != J_ARR || pst_v->count == 0u) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.%s: expected non-empty array of channel names",
                 pc_prefix, pc_field);
        return -1;
    }

    for (k = 0u; k < pst_v->count; ++k) {
        int ch;

        snprintf(path, sizeof(path), "%s.%s[%zu]", pc_prefix, pc_field, k);
        if (pst_v->items[k]->type != J_STR) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
            return -1;
        }
        ch = chan_index(pst_v->items[k]->str);
        if (ch < 0) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
            return -1;
        }
        u8_mask = (uint8_t)(u8_mask | (1u << ch));
    }
    *p_u8_mask = u8_mask;
    return 0;
}

static int load_comp_amp(cfg_ctx *pst_x, const jval *pst_v,
                         wg_component_t *pst_comp, const char *pc_prefix)
{
    char path[200];

    if (pst_v->type == J_NUM) {
        uint8_t i;

        for (i = 0u; i < pst_x->cfg->channel_count; ++i) {
            int ch = (int)pst_x->cfg->channels[i];

            if (pst_v->num < 0.0 || pst_v->num > 1000.0) {
                snprintf(path, sizeof(path), "%s.amp_pct.%s", pc_prefix,
                         g_chan_names[ch]);
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected number in [0, 1000]", path);
                return -1;
            }
            pst_comp->amp_pct[ch] = pst_v->num;
        }
        return 0;
    }

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.amp_pct: expected number or object", pc_prefix);
        return -1;
    }

    {
        size_t k;

        for (k = 0u; k < pst_v->count; ++k) {
            int ch = chan_index(pst_v->keys[k]);

            snprintf(path, sizeof(path), "%s.amp_pct.%s", pc_prefix,
                     pst_v->keys[k]);
            if (ch < 0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            if (pst_v->items[k]->type != J_NUM ||
                pst_v->items[k]->num < 0.0 || pst_v->items[k]->num > 1000.0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected number in [0, 1000]", path);
                return -1;
            }
            pst_comp->amp_pct[ch] = pst_v->items[k]->num;
        }
    }
    return 0;
}

static int load_comp_phase(cfg_ctx *pst_x, const jval *pst_v,
                           wg_component_t *pst_comp, const char *pc_prefix)
{
    char path[200];

    if (pst_v->type == J_NUM) {
        uint8_t i;

        for (i = 0u; i < pst_x->cfg->channel_count; ++i) {
            pst_comp->phase_deg[pst_x->cfg->channels[i]] = pst_v->num;
        }
        return 0;
    }

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.phase_deg: expected number or object", pc_prefix);
        return -1;
    }

    {
        size_t k;

        for (k = 0u; k < pst_v->count; ++k) {
            int ch = chan_index(pst_v->keys[k]);

            snprintf(path, sizeof(path), "%s.phase_deg.%s", pc_prefix,
                     pst_v->keys[k]);
            if (ch < 0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            if (pst_v->items[k]->type != J_NUM) {
                snprintf(pst_x->err, pst_x->elen, "%s: expected number", path);
                return -1;
            }
            pst_comp->phase_deg[ch] = pst_v->items[k]->num;
        }
    }
    return 0;
}

static int load_amplitude_ref(cfg_ctx *pst_x, const jval *pst_v,
                              const char *pc_path, uint8_t *p_u8_ref)
{
    if (pst_v->type == J_STR) {
        if (strcmp(pst_v->str, "nominal") == 0) {
            *p_u8_ref = 0u;
            return 0;
        }
        if (strcmp(pst_v->str, "fundamental") == 0) {
            *p_u8_ref = 1u;
            return 0;
        }
    } else if (pst_v->type == J_NUM &&
               (pst_v->num == 0.0 || pst_v->num == 1.0)) {
        *p_u8_ref = (uint8_t)pst_v->num;
        return 0;
    }
    snprintf(pst_x->err, pst_x->elen,
             "%s: expected one of nominal|fundamental", pc_path);
    return -1;
}

static int load_program_modulator(cfg_ctx *pst_x, const jval *pst_v,
                                  size_t ci, size_t mi,
                                  wg_component_t *pst_comp)
{
    static const char *const keys[12] = {
        "kind", "from", "to", "period_s", "frequency_hz", "depth_pct",
        "duty_pct", "phase_deg", "start_s", "duration_s", "hold", "channels"
    };
    char prefix[120];
    const jval *jv;
    wg_modulator_t st_mod;
    uint8_t u8_mod_mask = 0u;
    int kind;

    snprintf(prefix, sizeof(prefix), "program.components[%zu].modulators[%zu]",
             ci, mi);
    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "%s: expected object", prefix);
        return -1;
    }
    if (jo_unknown(pst_v, keys, 12u, prefix, pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "kind");
    if (jv == NULL || jv->type != J_STR) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: expected one of amp_const|amp_linear|amp_triangle|"
                 "amp_step|amp_sine|amp_rect|freq_linear|freq_triangle|"
                 "freq_step", prefix);
        return -1;
    }
    kind = mod2_kind_index(jv->str);
    if (kind < 0) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: expected one of amp_const|amp_linear|amp_triangle|"
                 "amp_step|amp_sine|amp_rect|freq_linear|freq_triangle|"
                 "freq_step", prefix);
        return -1;
    }
    if ((kind == (int)WG_MOD2_FREQ_LINEAR
         || kind == (int)WG_MOD2_FREQ_TRIANGLE
         || kind == (int)WG_MOD2_FREQ_STEP)
        && pst_comp->kind != (uint8_t)WG_COMP_FUNDAMENTAL) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: FREQ_* only allowed on the fundamental component",
                 prefix);
        return -1;
    }

    memset(&st_mod, 0, sizeof(st_mod));
    st_mod.kind = (uint8_t)kind;

#define LOAD_MOD_NUM(member, required, cond, expect)                          \
    do {                                                                      \
        const jval *jv2 = jo_get(pst_v, #member);                             \
        if (jv2 == NULL) {                                                     \
            if ((required) != 0) {                                             \
                snprintf(pst_x->err, pst_x->elen, "%s." #member ": " expect,   \
                         prefix);                                              \
                return -1;                                                     \
            }                                                                  \
            st_mod.member = 0.0;                                               \
        } else {                                                               \
            double v = jv2->num;                                               \
            (void)v;                                                           \
            if (jv2->type != J_NUM || !(cond)) {                               \
                snprintf(pst_x->err, pst_x->elen, "%s." #member ": " expect,   \
                         prefix);                                              \
                return -1;                                                     \
            }                                                                  \
            st_mod.member = v;                                                 \
        }                                                                      \
    } while (0)

    switch (kind) {
    case WG_MOD2_AMP_CONST:
        LOAD_MOD_NUM(from, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        break;
    case WG_MOD2_AMP_LINEAR:
        LOAD_MOD_NUM(from, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        LOAD_MOD_NUM(to, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        LOAD_MOD_NUM(duration_s, 1, v > 0.0, "expected number > 0");
        break;
    case WG_MOD2_AMP_TRIANGLE:
        LOAD_MOD_NUM(from, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        LOAD_MOD_NUM(to, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        LOAD_MOD_NUM(period_s, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(start_s, 0, v >= 0.0, "expected number >= 0");
        LOAD_MOD_NUM(hold, 0, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        break;
    case WG_MOD2_AMP_STEP:
        LOAD_MOD_NUM(from, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        LOAD_MOD_NUM(to, 1, v >= 0.0 && v <= 1000.0,
                     "expected number in [0, 1000]");
        LOAD_MOD_NUM(start_s, 1, v >= 0.0, "expected number >= 0");
        break;
    case WG_MOD2_AMP_SINE:
        LOAD_MOD_NUM(frequency_hz, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(depth_pct, 1, v >= 0.0, "expected number >= 0");
        LOAD_MOD_NUM(phase_deg, 0, 1, "expected number");
        LOAD_MOD_NUM(start_s, 0, v >= 0.0, "expected number >= 0");
        break;
    case WG_MOD2_AMP_RECT:
        LOAD_MOD_NUM(frequency_hz, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(depth_pct, 1, v >= 0.0, "expected number >= 0");
        LOAD_MOD_NUM(duty_pct, 0, v >= 0.0 && v <= 100.0,
                     "expected number in [0, 100]");
        LOAD_MOD_NUM(phase_deg, 0, 1, "expected number");
        LOAD_MOD_NUM(start_s, 0, v >= 0.0, "expected number >= 0");
        break;
    case WG_MOD2_FREQ_LINEAR:
        LOAD_MOD_NUM(from, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(to, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(duration_s, 1, v > 0.0, "expected number > 0");
        break;
    case WG_MOD2_FREQ_TRIANGLE:
        LOAD_MOD_NUM(from, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(to, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(period_s, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(start_s, 0, v >= 0.0, "expected number >= 0");
        break;
    case WG_MOD2_FREQ_STEP:
        LOAD_MOD_NUM(from, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(to, 1, v > 0.0, "expected number > 0");
        LOAD_MOD_NUM(start_s, 1, v >= 0.0, "expected number >= 0");
        break;
    default:
        break;
    }
#undef LOAD_MOD_NUM

    /*
     * 调制器级通道选择：省略表示本分量全部通道。FREQ_* 是全局频率轮廓，
     * 不接受通道掩码；AMP_* 掩码必须是所属分量掩码的子集。
     */
    jv = jo_get(pst_v, "channels");
    if (jv != NULL) {
        if (load_channel_mask(pst_x, jv, prefix, "channels",
                              &u8_mod_mask) != 0) {
            return -1;
        }
        if ((kind == (int)WG_MOD2_FREQ_LINEAR
             || kind == (int)WG_MOD2_FREQ_TRIANGLE
             || kind == (int)WG_MOD2_FREQ_STEP) && u8_mod_mask != 0u) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.channels: FREQ_* is global; channel selection not "
                     "allowed", prefix);
            return -1;
        }
        if (u8_mod_mask != 0u && pst_comp->channel_mask != 0u
            && (u8_mod_mask & (uint8_t)~pst_comp->channel_mask) != 0u) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.channels: channel not enabled by component", prefix);
            return -1;
        }
        st_mod.channel_mask = u8_mod_mask;
    }

    pst_comp->modulators[pst_comp->modulator_count] = st_mod;
    pst_comp->modulator_count = (uint8_t)(pst_comp->modulator_count + 1u);
    return 0;
}

static int load_program_component(cfg_ctx *pst_x, const jval *pst_v,
                                  size_t u_idx, wg_program_t *pst_prog)
{
    static const char *const keys[9] = {
        "kind", "order", "frequency_hz", "amp_pct", "amplitude_pct",
        "phase_deg", "channels", "amplitude_ref", "modulators"
    };
    char prefix[120];
    char path[200];
    const jval *jv;
    wg_component_t *pst_comp;
    uint8_t u8_kind;
    int kind;
    size_t k;

    snprintf(prefix, sizeof(prefix), "program.components[%zu]", u_idx);
    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "%s: expected object", prefix);
        return -1;
    }
    if (jo_unknown(pst_v, keys, 9u, prefix, pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "kind");
    if (jv == NULL || jv->type != J_STR) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: expected one of fundamental|harmonic|interharmonic|"
                 "tone", prefix);
        return -1;
    }
    kind = comp_kind_index(jv->str);
    if (kind < 0) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: expected one of fundamental|harmonic|interharmonic|"
                 "tone", prefix);
        return -1;
    }
    u8_kind = (uint8_t)kind;

    pst_comp = &pst_prog->components[u_idx];
    memset(pst_comp, 0, sizeof(*pst_comp));
    pst_comp->kind = u8_kind;
    pst_comp->channel_mask = enabled_channel_mask(pst_x);

    jv = jo_get(pst_v, "channels");
    if (jv != NULL &&
        load_channel_mask(pst_x, jv, prefix, "channels",
                          &pst_comp->channel_mask) != 0) {
        return -1;
    }

    switch (kind) {
    case WG_COMP_FUNDAMENTAL:
        pst_comp->order = 1.0;
        jv = jo_get(pst_v, "order");
        if (jv != NULL &&
            (jv->type != J_NUM || jv->num != 1.0)) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.order: expected 1", prefix);
            return -1;
        }
        break;
    case WG_COMP_HARMONIC:
        jv = jo_get(pst_v, "order");
        if (jv == NULL || jv->type != J_NUM ||
            jv->num < 2.0 || jv->num > 64.0 ||
            (double)(long long)jv->num != jv->num) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.order: expected integer 2..64", prefix);
            return -1;
        }
        pst_comp->order = jv->num;
        break;
    case WG_COMP_INTERHARMONIC:
        jv = jo_get(pst_v, "order");
        if (jv == NULL || jv->type != J_NUM || !(jv->num > 0.0)) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.order: expected number > 0", prefix);
            return -1;
        }
        pst_comp->order = jv->num;
        break;
    default: /* WG_COMP_TONE */
        jv = jo_get(pst_v, "frequency_hz");
        if (jv == NULL || jv->type != J_NUM || !(jv->num > 0.0)) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.frequency_hz: expected number > 0", prefix);
            return -1;
        }
        pst_comp->frequency_hz = jv->num;
        break;
    }

    if (kind != WG_COMP_TONE) {
        jv = jo_get(pst_v, "frequency_hz");
        if (jv != NULL &&
            (jv->type != J_NUM || jv->num < 0.0)) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.frequency_hz: expected number >= 0", prefix);
            return -1;
        }
        if (jv != NULL) {
            pst_comp->frequency_hz = jv->num;
        }
    }

    jv = jo_get(pst_v, "amp_pct");
    if (jv == NULL) {
        jv = jo_get(pst_v, "amplitude_pct");
    }
    if (jv != NULL) {
        if (load_comp_amp(pst_x, jv, pst_comp, prefix) != 0) {
            return -1;
        }
    } else if (kind == WG_COMP_FUNDAMENTAL) {
        uint8_t i;

        for (i = 0u; i < pst_x->cfg->channel_count; ++i) {
            uint8_t u8_ch = pst_x->cfg->channels[i];

            pst_comp->amp_pct[u8_ch] = pst_x->cfg->signal.amp_pct[u8_ch].from;
        }
    }

    jv = jo_get(pst_v, "phase_deg");
    if (jv != NULL) {
        if (load_comp_phase(pst_x, jv, pst_comp, prefix) != 0) {
            return -1;
        }
    } else if (kind == WG_COMP_FUNDAMENTAL) {
        uint8_t i;

        for (i = 0u; i < pst_x->cfg->channel_count; ++i) {
            uint8_t u8_ch = pst_x->cfg->channels[i];

            pst_comp->phase_deg[u8_ch] = pst_x->cfg->signal.phase_deg[u8_ch];
        }
    }

    pst_comp->amplitude_ref = (kind == WG_COMP_HARMONIC) ? 1u : 0u;
    jv = jo_get(pst_v, "amplitude_ref");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.amplitude_ref", prefix);
        if (load_amplitude_ref(pst_x, jv, path,
                               &pst_comp->amplitude_ref) != 0) {
            return -1;
        }
    }

    jv = jo_get(pst_v, "modulators");
    if (jv != NULL) {
        if (jv->type != J_ARR) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.modulators: expected array", prefix);
            return -1;
        }
        if (jv->count > (size_t)WG_MAX_MODULATORS) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s.modulators: expected at most %u entries", prefix,
                     (unsigned)WG_MAX_MODULATORS);
            return -1;
        }
        for (k = 0u; k < jv->count; ++k) {
            if (load_program_modulator(pst_x, jv->items[k], u_idx, k,
                                       pst_comp) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int load_program_event(cfg_ctx *pst_x, const jval *pst_v,
                              size_t u_idx, wg_program_t *pst_prog)
{
    static const char *const keys[5] = {
        "kind", "time_s", "channel_mask", "value", "duration_s"
    };
    char prefix[120];
    char path[200];
    const jval *jv;
    wg_event_t *pst_evt;
    int kind;

    snprintf(prefix, sizeof(prefix), "program.events[%zu]", u_idx);
    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "%s: expected object", prefix);
        return -1;
    }
    if (jo_unknown(pst_v, keys, 5u, prefix, pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "kind");
    if (jv == NULL || jv->type != J_STR) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: expected one of amp_step|phase_step", prefix);
        return -1;
    }
    kind = event_kind_index(jv->str);
    if (kind < 0) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s.kind: expected one of amp_step|phase_step", prefix);
        return -1;
    }

    pst_evt = &pst_prog->events[u_idx];
    memset(pst_evt, 0, sizeof(*pst_evt));
    pst_evt->kind = (uint8_t)kind;
    pst_evt->channel_mask = 0u;

    jv = jo_get(pst_v, "time_s");
    snprintf(path, sizeof(path), "%s.time_s", prefix);
    if (jv == NULL || jv->type != J_NUM || jv->num < 0.0 ||
        jv->num > pst_x->cfg->duration_s) {
        snprintf(pst_x->err, pst_x->elen,
                 "%s: expected number in [0, duration_s]", path);
        return -1;
    }
    pst_evt->time_s = jv->num;

    jv = jo_get(pst_v, "channel_mask");
    if (jv != NULL) {
        if (jv->type == J_NUM) {
            if (jv->num < 0.0 || jv->num > 127.0 ||
                (double)(long long)jv->num != jv->num) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s.channel_mask: expected integer in [0, 127]",
                         prefix);
                return -1;
            }
            pst_evt->channel_mask = (uint8_t)jv->num;
        } else if (load_channel_mask(pst_x, jv, prefix, "channel_mask",
                                     &pst_evt->channel_mask) != 0) {
            return -1;
        }
    }

    jv = jo_get(pst_v, "value");
    snprintf(path, sizeof(path), "%s.value", prefix);
    if (jv == NULL || jv->type != J_NUM) {
        snprintf(pst_x->err, pst_x->elen, "%s: expected number", path);
        return -1;
    }
    pst_evt->value = jv->num;

    jv = jo_get(pst_v, "duration_s");
    if (jv != NULL) {
        snprintf(path, sizeof(path), "%s.duration_s", prefix);
        if (jv->type != J_NUM || jv->num < 0.0) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected number >= 0", path);
            return -1;
        }
        pst_evt->duration_s = jv->num;
    }

    return 0;
}

static int load_program(cfg_ctx *pst_x, const jval *pst_v)
{
    static const char *const keys[2] = { "components", "events" };
    wg_program_t *pst_prog = &pst_x->cfg->program;
    const jval *jv;
    size_t k;

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "program: expected object");
        return -1;
    }
    if (jo_unknown(pst_v, keys, 2u, "program", pst_x->err,
                   pst_x->elen) != 0) {
        return -1;
    }

    memset(pst_prog, 0, sizeof(*pst_prog));

    jv = jo_get(pst_v, "components");
    if (jv != NULL) {
        if (jv->type != J_ARR) {
            snprintf(pst_x->err, pst_x->elen,
                     "program.components: expected array");
            return -1;
        }
        if (jv->count > (size_t)WG_MAX_COMPONENTS) {
            snprintf(pst_x->err, pst_x->elen,
                     "program.components: expected at most %u entries",
                     (unsigned)WG_MAX_COMPONENTS);
            return -1;
        }
        for (k = 0u; k < jv->count; ++k) {
            if (load_program_component(pst_x, jv->items[k], k, pst_prog) != 0) {
                return -1;
            }
            pst_prog->component_count = (uint16_t)(k + 1u);
        }
    }

    jv = jo_get(pst_v, "events");
    if (jv != NULL) {
        if (jv->type != J_ARR) {
            snprintf(pst_x->err, pst_x->elen,
                     "program.events: expected array");
            return -1;
        }
        if (jv->count > (size_t)WG_MAX_EVENTS) {
            snprintf(pst_x->err, pst_x->elen,
                     "program.events: expected at most %u entries",
                     (unsigned)WG_MAX_EVENTS);
            return -1;
        }
        for (k = 0u; k < jv->count; ++k) {
            if (load_program_event(pst_x, jv->items[k], k, pst_prog) != 0) {
                return -1;
            }
            pst_prog->event_count = (uint16_t)(k + 1u);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* top-level members                                                  */
/* ------------------------------------------------------------------ */

static int load_channels(cfg_ctx *pst_x, const jval *pst_v)
{
    char path[160];
    uint8_t n = 0u;
    uint8_t i;

    if (pst_v->type == J_STR) {
        const char *p = pst_v->str;

        while (*p != '\0') {
            char buf[8];
            size_t k = 0u;
            int ch;

            while (*p == ' ' || *p == '\t') {
                p += 1;
            }
            while (*p != '\0' && *p != ',') {
                if (k + 1u < sizeof(buf)) {
                    buf[k] = *p;
                    k += 1u;
                }
                p += 1;
            }
            while (k > 0u && (buf[k - 1u] == ' ' || buf[k - 1u] == '\t')) {
                k -= 1u;
            }
            buf[k] = '\0';

            snprintf(path, sizeof(path), "channels[%u]", (unsigned)n);
            if (k == 0u) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            ch = chan_index(buf);
            if (ch < 0) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                return -1;
            }
            if (n >= (uint8_t)WG_MAX_CHANNELS) {
                snprintf(pst_x->err, pst_x->elen,
                         "channels: expected at most %u entries",
                         (unsigned)WG_MAX_CHANNELS);
                return -1;
            }
            for (i = 0u; i < n; ++i) {
                if (pst_x->cfg->channels[i] == (uint8_t)ch) {
                    snprintf(pst_x->err, pst_x->elen,
                             "%s: expected unique channel name", path);
                    return -1;
                }
            }
            pst_x->cfg->channels[n] = (uint8_t)ch;
            n += 1u;

            if (*p == ',') {
                p += 1;
            }
        }

        if (n == 0u) {
            snprintf(pst_x->err, pst_x->elen,
                     "channels: expected at least one channel");
            return -1;
        }
        pst_x->cfg->channel_count = n;
        return 0;
    }

    if (pst_v->type != J_ARR) {
        snprintf(pst_x->err, pst_x->elen,
                 "channels: expected array of channel names or string list");
        return -1;
    }
    if (pst_v->count == 0u) {
        snprintf(pst_x->err, pst_x->elen,
                 "channels: expected at least one channel");
        return -1;
    }
    if (pst_v->count > (size_t)WG_MAX_CHANNELS) {
        snprintf(pst_x->err, pst_x->elen,
                 "channels: expected at most %u entries",
                 (unsigned)WG_MAX_CHANNELS);
        return -1;
    }

    for (i = 0u; i < (uint8_t)pst_v->count; ++i) {
        const jval *e = pst_v->items[i];
        int ch;

        snprintf(path, sizeof(path), "channels[%u]", (unsigned)i);
        if (e->type != J_STR) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
            return -1;
        }
        ch = chan_index(e->str);
        if (ch < 0) {
            snprintf(pst_x->err, pst_x->elen,
                     "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
            return -1;
        }
        {
            uint8_t j;

            for (j = 0u; j < i; ++j) {
                if (pst_x->cfg->channels[j] == (uint8_t)ch) {
                    snprintf(pst_x->err, pst_x->elen,
                             "%s: expected unique channel name", path);
                    return -1;
                }
            }
        }
        pst_x->cfg->channels[i] = (uint8_t)ch;
    }
    pst_x->cfg->channel_count = i;
    return 0;
}

static int load_time(cfg_ctx *pst_x, const jval *pst_v)
{
    static const char *const keys[3] = {
        "duration_s", "sample_rate_hz", "start_utc_ns"
    };
    const jval *jv;
    double d;

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "time: expected object");
        return -1;
    }
    if (jo_unknown(pst_v, keys, 3u, "time", pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "duration_s");
    if (jv != NULL) {
        if (want_min(pst_x, jv, "time.duration_s", WG_MIN_DURATION_S,
                     &d) != 0) {
            return -1;
        }
        pst_x->cfg->duration_s = d;
    }

    jv = jo_get(pst_v, "sample_rate_hz");
    if (jv != NULL) {
        if (want_int(pst_x, jv, "time.sample_rate_hz", 1000.0, 1000000.0,
                     &d) != 0) {
            return -1;
        }
        pst_x->cfg->sample_rate_hz = (uint32_t)d;
    }

    jv = jo_get(pst_v, "start_utc_ns");
    if (jv != NULL) {
        if (want_u64(pst_x, jv, "time.start_utc_ns",
                     &pst_x->cfg->start_utc_ns) != 0) {
            return -1;
        }
    }

    return 0;
}

static int load_output(cfg_ctx *pst_x, const jval *pst_v)
{
    static const char *const keys[2] = { "format", "frame_samples" };
    const jval *jv;
    double d;

    if (pst_v->type != J_OBJ) {
        snprintf(pst_x->err, pst_x->elen, "output: expected object");
        return -1;
    }
    if (jo_unknown(pst_v, keys, 2u, "output", pst_x->err, pst_x->elen) != 0) {
        return -1;
    }

    jv = jo_get(pst_v, "format");
    if (jv != NULL) {
        if (jv->type != J_STR) {
            snprintf(pst_x->err, pst_x->elen,
                     "output.format: expected one of f64|f32|i24");
            return -1;
        }
        if (strcmp(jv->str, "f64") == 0) {
            pst_x->cfg->format = (uint8_t)WG_FMT_F64;
        } else if (strcmp(jv->str, "f32") == 0) {
            pst_x->cfg->format = (uint8_t)WG_FMT_F32;
        } else if (strcmp(jv->str, "i24") == 0) {
            pst_x->cfg->format = (uint8_t)WG_FMT_I24;
        } else {
            snprintf(pst_x->err, pst_x->elen,
                     "output.format: expected one of f64|f32|i24");
            return -1;
        }
    }

    jv = jo_get(pst_v, "frame_samples");
    if (jv != NULL) {
        if (want_int(pst_x, jv, "output.frame_samples", 1.0, 65535.0,
                     &d) != 0) {
            snprintf(pst_x->err, pst_x->elen,
                     "frame_samples: expected 1..65535");
            return -1;
        }
        pst_x->cfg->frame_samples = (uint32_t)d;
    }

    return 0;
}

static int load_segments(cfg_ctx *pst_x, const jval *pst_v)
{
    static const char *const keys[4] = {
        "duration_s", "transition", "channel_delay_s", "signal"
    };
    char path[160];
    size_t k;

    if (pst_v->type != J_ARR) {
        snprintf(pst_x->err, pst_x->elen, "segments: expected array");
        return -1;
    }
    if (pst_v->count > (size_t)WG_MAX_SEGMENTS) {
        snprintf(pst_x->err, pst_x->elen,
                 "segments: expected at most %u entries",
                 (unsigned)WG_MAX_SEGMENTS);
        return -1;
    }

    pst_x->cfg->segment_count = 0u;

    for (k = 0; k < pst_v->count; ++k) {
        const jval *e = pst_v->items[k];
        const jval *jv;
        wg_segment_t *pst_seg;
        double dur;
        int i;

        snprintf(path, sizeof(path), "segments[%zu]", k);
        if (e->type != J_OBJ) {
            snprintf(pst_x->err, pst_x->elen, "%s: expected object", path);
            return -1;
        }
        if (jo_unknown(e, keys, 4u, path, pst_x->err, pst_x->elen) != 0) {
            return -1;
        }

        snprintf(path, sizeof(path), "segments[%zu].duration_s", k);
        if (want_min(pst_x, jo_get(e, "duration_s"), path, WG_MIN_DURATION_S,
                     &dur) != 0) {
            return -1;
        }

        pst_seg = &pst_x->cfg->segments[k];
        memset(pst_seg, 0, sizeof(*pst_seg));
        pst_seg->duration_s = dur;
        pst_seg->transition = (uint8_t)WG_TRANS_IMMEDIATE;
        pst_seg->signal = pst_x->cfg->signal;
        for (i = 0; i < WG_MAX_CHANNELS; ++i) {
            pst_seg->channel_delay_s[i] = 0.0;
        }

        jv = jo_get(e, "transition");
        if (jv != NULL) {
            snprintf(path, sizeof(path), "segments[%zu].transition", k);
            if (jv->type != J_STR) {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of immediate|zero_crossing", path);
                return -1;
            }
            if (strcmp(jv->str, "immediate") == 0) {
                pst_seg->transition = (uint8_t)WG_TRANS_IMMEDIATE;
            } else if (strcmp(jv->str, "zero_crossing") == 0) {
                pst_seg->transition = (uint8_t)WG_TRANS_ZERO_CROSSING;
            } else {
                snprintf(pst_x->err, pst_x->elen,
                         "%s: expected one of immediate|zero_crossing", path);
                return -1;
            }
        }

        jv = jo_get(e, "channel_delay_s");
        if (jv != NULL && jv->type == J_ARR) {
            size_t m;

            if (jv->count > (size_t)pst_x->cfg->channel_count) {
                snprintf(pst_x->err, pst_x->elen,
                         "segments[%zu].channel_delay_s: expected at most %u "
                         "entries", k, (unsigned)pst_x->cfg->channel_count);
                return -1;
            }
            for (m = 0; m < jv->count; ++m) {
                int ch = (int)pst_x->cfg->channels[m];

                snprintf(path, sizeof(path),
                         "segments[%zu].channel_delay_s[%zu]", k, m);
                if (jv->items[m]->type != J_NUM ||
                    jv->items[m]->num < 0.0) {
                    snprintf(pst_x->err, pst_x->elen,
                             "%s: expected number >= 0", path);
                    return -1;
                }
                pst_seg->channel_delay_s[ch] = jv->items[m]->num;
            }
            jv = NULL;
        }
        if (jv != NULL) {
            size_t m;

            if (jv->type != J_OBJ) {
                snprintf(pst_x->err, pst_x->elen,
                         "segments[%zu].channel_delay_s: expected object or "
                         "array", k);
                return -1;
            }
            for (m = 0; m < jv->count; ++m) {
                int ch = chan_index(jv->keys[m]);

                snprintf(path, sizeof(path),
                         "segments[%zu].channel_delay_s.%s", k, jv->keys[m]);
                if (ch < 0) {
                    snprintf(pst_x->err, pst_x->elen,
                             "%s: expected one of Ua|Ub|Uc|Ia|Ib|Ic|In", path);
                    return -1;
                }
                if (jv->items[m]->type != J_NUM ||
                    jv->items[m]->num < 0.0) {
                    snprintf(pst_x->err, pst_x->elen,
                             "%s: expected number >= 0", path);
                    return -1;
                }
                pst_seg->channel_delay_s[ch] = jv->items[m]->num;
            }
        }

        jv = jo_get(e, "signal");
        if (jv != NULL) {
            snprintf(path, sizeof(path), "segments[%zu].signal", k);
            if (load_signal(pst_x, jv, &pst_seg->signal, path) != 0) {
                return -1;
            }
        }

        pst_x->cfg->segment_count = (uint16_t)(k + 1u);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* public loader                                                      */
/* ------------------------------------------------------------------ */

int32_t wg_config_load_json(const char *pc_path, wg_config_t *pst_cfg,
                            char *pc_err, size_t err_len)
{
    static const char *const keys[7] = {
        "test", "time", "channels", "signal", "segments", "output",
        "program"
    };
    FILE *p_file = NULL;
    long file_size;
    char *pc_buf = NULL;
    jval *pst_root = NULL;
    wg_config_t st_tmp;
    cfg_ctx st_x;
    const jval *jv;
    int32_t s32_rc = -1;

    if (pc_err != NULL && err_len > 0u) {
        pc_err[0] = '\0';
    }
    if (pc_path == NULL || pst_cfg == NULL || pc_err == NULL || err_len == 0u) {
        return -1;
    }

    p_file = fopen(pc_path, "rb");
    if (p_file == NULL) {
        snprintf(pc_err, err_len, "config: cannot open '%s'", pc_path);
        return -1;
    }
    if (fseek(p_file, 0L, SEEK_END) != 0) {
        fclose(p_file);
        snprintf(pc_err, err_len, "config: cannot read '%s'", pc_path);
        return -1;
    }
    file_size = ftell(p_file);
    if (file_size < 0L || (unsigned long)file_size > 4ul * 1024ul * 1024ul) {
        fclose(p_file);
        snprintf(pc_err, err_len, "config: expected file <= 4 MiB");
        return -1;
    }
    if (fseek(p_file, 0L, SEEK_SET) != 0) {
        fclose(p_file);
        snprintf(pc_err, err_len, "config: cannot read '%s'", pc_path);
        return -1;
    }

    pc_buf = (char *)malloc((size_t)file_size + 1u);
    if (pc_buf == NULL) {
        fclose(p_file);
        snprintf(pc_err, err_len, "config: out of memory");
        return -1;
    }
    if (file_size > 0L &&
        fread(pc_buf, 1u, (size_t)file_size, p_file) != (size_t)file_size) {
        fclose(p_file);
        free(pc_buf);
        snprintf(pc_err, err_len, "config: cannot read '%s'", pc_path);
        return -1;
    }
    fclose(p_file);
    p_file = NULL;
    pc_buf[file_size] = '\0';

    pst_root = jparse_document(pc_buf);
    if (pst_root == NULL) {
        snprintf(pc_err, err_len, "config: expected valid JSON document");
        goto done;
    }
    if (pst_root->type != J_OBJ) {
        snprintf(pc_err, err_len, "config: expected object");
        goto done;
    }

    st_tmp = *pst_cfg;
    st_x.cfg = &st_tmp;
    st_x.err = pc_err;
    st_x.elen = err_len;

    if (jo_unknown(pst_root, keys, 7u, "", pc_err, err_len) != 0) {
        goto done;
    }

    jv = jo_get(pst_root, "test");
    if (jv != NULL && jv->type != J_STR) {
        snprintf(pc_err, err_len, "test: expected string");
        goto done;
    }

    jv = jo_get(pst_root, "time");
    if (jv != NULL && load_time(&st_x, jv) != 0) {
        goto done;
    }

    jv = jo_get(pst_root, "channels");
    if (jv != NULL && load_channels(&st_x, jv) != 0) {
        goto done;
    }

    jv = jo_get(pst_root, "output");
    if (jv != NULL && load_output(&st_x, jv) != 0) {
        goto done;
    }

    jv = jo_get(pst_root, "signal");
    if (jv != NULL && load_signal(&st_x, jv, &st_tmp.signal, "signal") != 0) {
        goto done;
    }

    jv = jo_get(pst_root, "program");
    if (jv != NULL && load_program(&st_x, jv) != 0) {
        goto done;
    }

    jv = jo_get(pst_root, "segments");
    if (jv != NULL && load_segments(&st_x, jv) != 0) {
        goto done;
    }

    {
        double total = 0.0;
        uint16_t i;

        for (i = 0u; i < st_tmp.segment_count; ++i) {
            total += st_tmp.segments[i].duration_s;
        }
        if (st_tmp.segment_count > 0u && total < st_tmp.duration_s) {
            snprintf(pc_err, err_len,
                     "segments: expected total duration >= duration_s");
            goto done;
        }
    }

    *pst_cfg = st_tmp;
    s32_rc = 0;

done:
    jfree(pst_root);
    free(pc_buf);
    return s32_rc;
}
