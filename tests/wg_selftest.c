/*
 * wg_selftest.c - standalone self-test for libwg.a.
 *
 * Covers configuration validation, streaming counters/timestamps,
 * file-vs-stream byte identity, determinism, phase continuity,
 * I24 encoding and a long streaming run with an RSS check.
 * Only libwg.a and the C/POSIX runtime are used.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wg.h"
#include "wg_config.h"

#define TEST_PI       3.14159265358979323846
#define TEST_TWO_PI   (2.0 * TEST_PI)
#define TEST_SQRT2    1.41421356237309504880
#define TEST_I24_MIN  (-8388608)
#define TEST_I24_MAX  8388607
#define TEST_FNV_OFF  1469598103934665603ull
#define TEST_FNV_PRIME 1099511628211ull

static int g_failures;

static void report(int ok, const char *pc_name, const char *pc_detail)
{
    printf("%s %-18s %s\n", ok ? "PASS" : "FAIL", pc_name, pc_detail);
    if (ok == 0) {
        g_failures++;
    }
}

/* 文档 09 §6 的整数纳秒公式，独立于库实现。 */
static uint64_t ns_at(uint64_t u64_sample, uint32_t u32_fs)
{
    uint64_t u64_q = u64_sample / (uint64_t)u32_fs;
    uint64_t u64_r = u64_sample % (uint64_t)u32_fs;

    return u64_q * 1000000000ull
         + (u64_r * 1000000000ull + (uint64_t)(u32_fs / 2u)) / (uint64_t)u32_fs;
}

static void set_amp(wg_config_t *pst_cfg, double f64_pct)
{
    uint32_t u32_idx;

    for (u32_idx = 0u; u32_idx < (uint32_t)pst_cfg->channel_count; u32_idx++) {
        uint8_t u8_ch = pst_cfg->channels[u32_idx];

        pst_cfg->signal.amp_pct[u8_ch].profile = (uint8_t)WG_PROFILE_CONST;
        pst_cfg->signal.amp_pct[u8_ch].from = f64_pct;
        pst_cfg->signal.amp_pct[u8_ch].to = f64_pct;
        pst_cfg->signal.amp_pct[u8_ch].period_s = 0.0;
    }
}

static void temp_path(char *pc_buf, size_t z_len, const char *pc_tag)
{
    (void)snprintf(pc_buf, z_len, "/tmp/wg_selftest_%ld_%s.bin",
                   (long)getpid(), pc_tag);
}

static unsigned char *read_whole_file(const char *pc_path, size_t *pz_len)
{
    FILE *p_file;
    long s32_size;
    unsigned char *p_buf;

    p_file = fopen(pc_path, "rb");
    if (p_file == NULL) {
        return NULL;
    }
    if (fseek(p_file, 0L, SEEK_END) != 0) {
        (void)fclose(p_file);
        return NULL;
    }
    s32_size = ftell(p_file);
    if (s32_size < 0L || fseek(p_file, 0L, SEEK_SET) != 0) {
        (void)fclose(p_file);
        return NULL;
    }
    p_buf = (unsigned char *)malloc((size_t)s32_size + 1u);
    if (p_buf == NULL) {
        (void)fclose(p_file);
        return NULL;
    }
    if (s32_size > 0L
        && fread(p_buf, 1u, (size_t)s32_size, p_file) != (size_t)s32_size) {
        free(p_buf);
        (void)fclose(p_file);
        return NULL;
    }
    (void)fclose(p_file);
    *pz_len = (size_t)s32_size;
    return p_buf;
}

static int sha256_of(const char *pc_path, char ac_hex[65])
{
    char a_cmd[600];
    char a_hash[65];
    FILE *p_pipe;
    int s32_rc;

    (void)snprintf(a_cmd, sizeof(a_cmd), "sha256sum %s", pc_path);
    p_pipe = popen(a_cmd, "r");
    if (p_pipe == NULL) {
        return -1;
    }
    if (fscanf(p_pipe, "%64s", a_hash) != 1) {
        (void)pclose(p_pipe);
        return -1;
    }
    s32_rc = pclose(p_pipe);
    if (s32_rc != 0) {
        return -1;
    }
    (void)memcpy(ac_hex, a_hash, 65u);
    return 0;
}

static long read_rss_pages(void)
{
    FILE *p_file;
    unsigned long u64_size = 0ul;
    unsigned long u64_res = 0ul;

    p_file = fopen("/proc/self/statm", "r");
    if (p_file == NULL) {
        return -1;
    }
    if (fscanf(p_file, "%lu %lu", &u64_size, &u64_res) != 2) {
        (void)fclose(p_file);
        return -1;
    }
    (void)fclose(p_file);
    return (long)u64_res;
}

static double now_sec(void)
{
    struct timespec st_ts;

    if (clock_gettime(CLOCK_MONOTONIC, &st_ts) != 0) {
        return 0.0;
    }
    return (double)st_ts.tv_sec + (double)st_ts.tv_nsec / 1.0e9;
}

/* ------------------------------------------------------------------ */
/* 1. configuration                                                   */
/* ------------------------------------------------------------------ */

static void test_config(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    char a_err[256];
    char a_detail[512];
    int s32_default_ok;
    int s32_fs_rej;
    int s32_ch_rej;
    int s32_frame_rej;

    wg_config_default(&st_cfg);
    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    s32_default_ok = (pst_wg != NULL) ? 1 : 0;
    if (pst_wg != NULL) {
        wg_destroy(pst_wg);
    }

    wg_config_default(&st_cfg);
    st_cfg.sample_rate_hz = 0u;
    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    s32_fs_rej = (pst_wg == NULL) ? 1 : 0;
    if (pst_wg != NULL) {
        wg_destroy(pst_wg);
    }

    wg_config_default(&st_cfg);
    st_cfg.channel_count = 0u;
    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    s32_ch_rej = (pst_wg == NULL) ? 1 : 0;
    if (pst_wg != NULL) {
        wg_destroy(pst_wg);
    }

    wg_config_default(&st_cfg);
    st_cfg.frame_samples = 0u;
    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    s32_frame_rej = (pst_wg == NULL) ? 1 : 0;
    if (pst_wg != NULL) {
        wg_destroy(pst_wg);
    }

    (void)snprintf(a_detail, sizeof(a_detail),
                   "default=%s fs0=%s ch0=%s frame0=%s",
                   s32_default_ok ? "created" : "REJECTED",
                   s32_fs_rej ? "rejected" : "ACCEPTED",
                   s32_ch_rej ? "rejected" : "ACCEPTED",
                   s32_frame_rej ? "rejected" : "ACCEPTED");
    report(s32_default_ok && s32_fs_rej && s32_ch_rej && s32_frame_rej,
           "config-validation", a_detail);
}

/* ------------------------------------------------------------------ */
/* 2. stream counters, flags and timestamps                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t u32_fs;
    uint64_t u64_base;
    uint64_t u64_total;
    uint64_t u64_seen;
    uint64_t u64_frames;
    uint64_t u64_samples;
    int s32_first_ok;
    int s32_last_ok;
    int s32_seq_ok;
    int s32_ts_ok;
    int s32_flag_ok;
} t2_ctx_t;

static int32_t t2_sink(const wg_frame_t *pst_frame, void *p_ctx)
{
    t2_ctx_t *pst = (t2_ctx_t *)p_ctx;
    uint64_t u64_end = pst_frame->start_sample + (uint64_t)pst_frame->frames;

    if (pst_frame->frames == 0u) {
        pst->s32_seq_ok = 0;
    }
    if (pst_frame->start_sample != pst->u64_seen) {
        pst->s32_seq_ok = 0;
    }
    if (pst_frame->frame_index != pst->u64_frames) {
        pst->s32_seq_ok = 0;
    }
    if (pst_frame->start_utc_ns
        != pst->u64_base + ns_at(pst_frame->start_sample, pst->u32_fs)) {
        pst->s32_ts_ok = 0;
    }
    if (pst_frame->end_utc_ns
        != pst->u64_base + ns_at(u64_end, pst->u32_fs)) {
        pst->s32_ts_ok = 0;
    }
    if (((pst_frame->flags & WG_FRAME_FLAG_FIRST) != 0u)
        != (pst_frame->frame_index == 0u)) {
        pst->s32_flag_ok = 0;
    }
    if (((pst_frame->flags & WG_FRAME_FLAG_LAST) != 0u)
        != (u64_end == pst->u64_total)) {
        pst->s32_flag_ok = 0;
    }
    if (pst_frame->frame_index == 0u) {
        pst->s32_first_ok = ((pst_frame->flags & WG_FRAME_FLAG_FIRST) != 0u);
    }
    if (u64_end == pst->u64_total) {
        pst->s32_last_ok = ((pst_frame->flags & WG_FRAME_FLAG_LAST) != 0u);
    }

    pst->u64_frames++;
    pst->u64_samples += (uint64_t)pst_frame->frames;
    pst->u64_seen += (uint64_t)pst_frame->frames;
    return 0;
}

static void test_stream(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    t2_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    int32_t s32_rc;
    int s32_ok;

    wg_config_default(&st_cfg);
    st_cfg.sample_rate_hz = 51200u;
    st_cfg.duration_s = 1.0;
    st_cfg.frame_samples = 256u;
    st_cfg.start_utc_ns = 1794700800000000000ull;
    set_amp(&st_cfg, 80.0);

    (void)memset(&st_ctx, 0, sizeof(st_ctx));
    st_ctx.u32_fs = st_cfg.sample_rate_hz;
    st_ctx.u64_base = st_cfg.start_utc_ns;
    st_ctx.u64_total = 51200u;
    st_ctx.s32_seq_ok = 1;
    st_ctx.s32_ts_ok = 1;
    st_ctx.s32_flag_ok = 1;

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "stream-1s", a_detail);
        return;
    }
    s32_rc = wg_run_stream(pst_wg, t2_sink, &st_ctx);
    wg_destroy(pst_wg);

    s32_ok = (s32_rc == 0)
          && (st_ctx.u64_samples == 51200ull)
          && (st_ctx.u64_frames == 200ull)
          && st_ctx.s32_first_ok && st_ctx.s32_last_ok
          && st_ctx.s32_seq_ok && st_ctx.s32_ts_ok && st_ctx.s32_flag_ok;

    (void)snprintf(a_detail, sizeof(a_detail),
                   "samples=%llu frames=%llu first=%d last=%d seq=%d ts=%d flags=%d rc=%ld",
                   (unsigned long long)st_ctx.u64_samples,
                   (unsigned long long)st_ctx.u64_frames,
                   st_ctx.s32_first_ok, st_ctx.s32_last_ok, st_ctx.s32_seq_ok,
                   st_ctx.s32_ts_ok, st_ctx.s32_flag_ok, (long)s32_rc);
    report(s32_ok, "stream-1s", a_detail);
}

/* ------------------------------------------------------------------ */
/* 3. file vs stream byte identity                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *p_buf;
    size_t z_cap;
    size_t z_off;
    uint32_t u32_bps;
    int s32_overflow;
} t3_ctx_t;

static int32_t t3_sink(const wg_frame_t *pst_frame, void *p_ctx)
{
    t3_ctx_t *pst = (t3_ctx_t *)p_ctx;
    size_t z_bytes = (size_t)pst_frame->frames
                   * (size_t)pst_frame->channel_count
                   * (size_t)pst->u32_bps;

    if (z_bytes > pst->z_cap - pst->z_off) {
        pst->s32_overflow = 1;
        return -1;
    }
    if (z_bytes > 0u) {
        (void)memcpy(pst->p_buf + pst->z_off, pst_frame->samples, z_bytes);
    }
    pst->z_off += z_bytes;
    return 0;
}

static uint32_t ld_u32(const unsigned char *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* 跳过 64 B 文件头、56 B 帧头和 32 B 结束记录，只取样本区。 */
static int extract_payload(const unsigned char *p_file, size_t z_len,
                           unsigned char *p_dst, size_t z_cap,
                           size_t *pz_out)
{
    size_t z_off = 64u;
    size_t z_out = 0u;

    if (z_len < 64u || memcmp(p_file, "WGIEC625", 8u) != 0) {
        return -1;
    }
    while (z_off + 56u <= z_len && memcmp(p_file + z_off, "WGF1", 4u) == 0) {
        uint32_t u32_payload = ld_u32(p_file + z_off + 52u);

        z_off += 56u;
        if ((size_t)u32_payload > z_len - z_off
            || (size_t)u32_payload > z_cap - z_out) {
            return -1;
        }
        if (u32_payload > 0u) {
            (void)memcpy(p_dst + z_out, p_file + z_off, (size_t)u32_payload);
        }
        z_out += (size_t)u32_payload;
        z_off += (size_t)u32_payload;
    }
    *pz_out = z_out;
    return 0;
}

static void test_file_stream(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    t3_ctx_t st_ctx;
    unsigned char *p_stream = NULL;
    unsigned char *p_file = NULL;
    unsigned char *p_payload = NULL;
    size_t z_file = 0u;
    size_t z_payload = 0u;
    size_t z_total;
    uint64_t u64_total;
    char a_path[256];
    char a_err[256];
    char a_detail[512];
    int s32_ok = 0;
    int32_t s32_rc;

    wg_config_default(&st_cfg);
    st_cfg.duration_s = 0.2;
    st_cfg.frame_samples = 300u;
    st_cfg.start_utc_ns = 1794700800000000000ull;
    set_amp(&st_cfg, 80.0);

    u64_total = (uint64_t)llround(st_cfg.duration_s
                                  * (double)st_cfg.sample_rate_hz);
    z_total = (size_t)u64_total * (size_t)st_cfg.channel_count * 8u;

    p_stream = (unsigned char *)malloc(z_total);
    p_payload = (unsigned char *)malloc(z_total);
    temp_path(a_path, sizeof(a_path), "fs");
    if (p_stream == NULL || p_payload == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "out of memory");
        report(0, "file-vs-stream", a_detail);
        free(p_stream);
        free(p_payload);
        return;
    }
    (void)memset(p_stream, 0xA5, z_total);

    (void)memset(&st_ctx, 0, sizeof(st_ctx));
    st_ctx.p_buf = p_stream;
    st_ctx.z_cap = z_total;
    st_ctx.u32_bps = 8u;

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "file-vs-stream", a_detail);
        free(p_stream);
        free(p_payload);
        return;
    }
    s32_rc = wg_run_stream(pst_wg, t3_sink, &st_ctx);
    if (s32_rc == 0) {
        s32_rc = wg_run_file(pst_wg, a_path);
    }
    wg_destroy(pst_wg);

    if (s32_rc != 0 || st_ctx.s32_overflow != 0 || st_ctx.z_off != z_total) {
        (void)snprintf(a_detail, sizeof(a_detail),
                       "run failed rc=%ld overflow=%d stream_bytes=%zu/%zu",
                       (long)s32_rc, st_ctx.s32_overflow, st_ctx.z_off, z_total);
    } else {
        p_file = read_whole_file(a_path, &z_file);
        if (p_file == NULL) {
            (void)snprintf(a_detail, sizeof(a_detail), "cannot read %s", a_path);
        } else if (extract_payload(p_file, z_file, p_payload, z_total,
                                   &z_payload) != 0) {
            (void)snprintf(a_detail, sizeof(a_detail),
                           "cannot parse file layout (%zu bytes)", z_file);
        } else if (z_payload != z_total) {
            (void)snprintf(a_detail, sizeof(a_detail),
                           "payload_bytes=%zu expected=%zu", z_payload, z_total);
        } else if (memcmp(p_payload, p_stream, z_total) != 0) {
            (void)snprintf(a_detail, sizeof(a_detail),
                           "byte mismatch over %zu bytes", z_total);
        } else {
            s32_ok = 1;
            (void)snprintf(a_detail, sizeof(a_detail),
                           "identical payload %zu bytes, file=%zu bytes",
                           z_total, z_file);
        }
    }
    report(s32_ok, "file-vs-stream", a_detail);

    free(p_stream);
    free(p_payload);
    free(p_file);
    (void)remove(a_path);
    {
        char a_man[288];

        (void)snprintf(a_man, sizeof(a_man), "%s.manifest.json", a_path);
        (void)remove(a_man);
    }
}

/* ------------------------------------------------------------------ */
/* 4. determinism (SHA-256)                                           */
/* ------------------------------------------------------------------ */

static void test_determinism(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    char a_path1[256];
    char a_path2[256];
    char a_err[256];
    char a_detail[512];
    char a_sha1[65];
    char a_sha2[65];
    int s32_ok = 0;
    int32_t s32_rc;

    wg_config_default(&st_cfg);
    st_cfg.duration_s = 0.3;
    st_cfg.frame_samples = 256u;
    st_cfg.start_utc_ns = 1794700800000000000ull;
    set_amp(&st_cfg, 80.0);

    temp_path(a_path1, sizeof(a_path1), "det1");
    temp_path(a_path2, sizeof(a_path2), "det2");
    a_sha1[0] = '\0';
    a_sha2[0] = '\0';

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "determinism", a_detail);
        return;
    }
    s32_rc = wg_run_file(pst_wg, a_path1);
    if (s32_rc == 0) {
        s32_rc = wg_run_file(pst_wg, a_path2);
    }
    wg_destroy(pst_wg);

    if (s32_rc != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "run failed rc=%ld", (long)s32_rc);
    } else if (sha256_of(a_path1, a_sha1) != 0 || sha256_of(a_path2, a_sha2) != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "sha256sum invocation failed");
    } else if (strcmp(a_sha1, a_sha2) != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "sha1=%s sha2=%s", a_sha1, a_sha2);
    } else {
        s32_ok = 1;
        (void)snprintf(a_detail, sizeof(a_detail), "sha256=%s", a_sha1);
    }
    report(s32_ok, "determinism", a_detail);

    {
        char a_man1[288];
        char a_man2[288];

        (void)snprintf(a_man1, sizeof(a_man1), "%s.manifest.json", a_path1);
        (void)snprintf(a_man2, sizeof(a_man2), "%s.manifest.json", a_path2);
        (void)remove(a_man1);
        (void)remove(a_man2);
    }
    (void)remove(a_path1);
    (void)remove(a_path2);
}

/* ------------------------------------------------------------------ */
/* 5. phase continuity on A1.1.1                                      */
/* ------------------------------------------------------------------ */

/* A1.1.1: 42.5-57.5 Hz triangle, 10 s period (5 s one way). */
static double a111_freq(double f64_t)
{
    const double f64_period = 10.0;
    const double f64_half = 5.0;
    double f64_pos = fmod(f64_t, f64_period);
    double f64_ratio;

    if (f64_pos < 0.0) {
        f64_pos += f64_period;
    }
    if (f64_pos <= f64_half) {
        f64_ratio = f64_pos / f64_half;
    } else {
        f64_ratio = (f64_period - f64_pos) / f64_half;
    }
    return 42.5 + (57.5 - 42.5) * f64_ratio;
}

typedef struct {
    uint32_t u32_fs;
    double f64_peak;
    uint64_t u64_seen;
    double f64_phi;
    double f64_max_err;
    int s32_ok;
} t5_ctx_t;

static double wrap_pi(double f64_ang)
{
    double f64_v = fmod(f64_ang + TEST_PI, TEST_TWO_PI);

    if (f64_v < 0.0) {
        f64_v += TEST_TWO_PI;
    }
    return f64_v - TEST_PI;
}

static int32_t t5_sink(const wg_frame_t *pst_frame, void *p_ctx)
{
    t5_ctx_t *pst = (t5_ctx_t *)p_ctx;
    const double *p_samples = (const double *)pst_frame->samples;
    double f64_freq;
    double f64_delta;
    double f64_cos;
    double f64_sin;
    double f64_meas;
    double f64_err;
    uint32_t u32_i;

    if (pst_frame->frames < 2u) {
        pst->s32_ok = 0;
        return 0;
    }
    f64_freq = a111_freq((double)pst->u64_seen / (double)pst->u32_fs);
    f64_delta = TEST_TWO_PI * f64_freq / (double)pst->u32_fs;

    f64_cos = p_samples[0] / pst->f64_peak;
    f64_sin = (p_samples[0] * cos(f64_delta) - p_samples[1])
            / (pst->f64_peak * sin(f64_delta));
    f64_meas = atan2(f64_sin, f64_cos);
    f64_err = wrap_pi(f64_meas - pst->f64_phi);
    if (fabs(f64_err) > pst->f64_max_err) {
        pst->f64_max_err = fabs(f64_err);
    }

    for (u32_i = 0u; u32_i < pst_frame->frames; u32_i++) {
        double f64_f = a111_freq((double)(pst->u64_seen + (uint64_t)u32_i)
                                 / (double)pst->u32_fs);

        pst->f64_phi += TEST_TWO_PI * f64_f / (double)pst->u32_fs;
    }
    pst->u64_seen += (uint64_t)pst_frame->frames;
    return 0;
}

static void test_phase(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    t5_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    int32_t s32_rc;
    int s32_ok;

    wg_config_default(&st_cfg);
    if (wg_scenario_apply("A1.1.1", 50, &st_cfg, a_err, sizeof(a_err)) != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "scenario failed: %s", a_err);
        report(0, "phase-A1.1.1", a_detail);
        return;
    }
    st_cfg.duration_s = 10.0;
    st_cfg.start_utc_ns = 1794700800000000000ull;

    (void)memset(&st_ctx, 0, sizeof(st_ctx));
    st_ctx.u32_fs = st_cfg.sample_rate_hz;
    st_ctx.f64_peak = st_cfg.nominal[WG_CH_UA] * TEST_SQRT2;
    st_ctx.s32_ok = 1;

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "phase-A1.1.1", a_detail);
        return;
    }
    s32_rc = wg_run_stream(pst_wg, t5_sink, &st_ctx);
    wg_destroy(pst_wg);

    s32_ok = (s32_rc == 0) && st_ctx.s32_ok
          && (st_ctx.u64_seen == 512000ull)
          && (st_ctx.f64_max_err < 1.0e-9);
    (void)snprintf(a_detail, sizeof(a_detail),
                   "frames=%llu max_phase_err=%.3e rad limit=1e-9",
                   (unsigned long long)(st_ctx.u64_seen / 256ull),
                   st_ctx.f64_max_err);
    report(s32_ok, "phase-A1.1.1", a_detail);
}

/* ------------------------------------------------------------------ */
/* 6. I24 encoding                                                    */
/* ------------------------------------------------------------------ */

static void test_i24(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    unsigned char *p_file = NULL;
    size_t z_file = 0u;
    size_t z_off;
    double f64_u_scale = 0.0;
    double f64_i_scale = 0.0;
    int32_t s32_min = TEST_I24_MAX;
    int32_t s32_max = TEST_I24_MIN;
    uint64_t u64_codes = 0u;
    char a_path[256];
    char a_err[256];
    char a_detail[512];
    int s32_ok = 0;
    int32_t s32_rc;

    wg_config_default(&st_cfg);
    st_cfg.duration_s = 1.0;
    st_cfg.frame_samples = 256u;
    st_cfg.format = (uint8_t)WG_FMT_I24;
    st_cfg.channel_count = 4u;
    st_cfg.channels[3] = (uint8_t)WG_CH_IA;
    st_cfg.start_utc_ns = 1794700800000000000ull;

    temp_path(a_path, sizeof(a_path), "i24");
    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "i24-encoding", a_detail);
        return;
    }
    s32_rc = wg_run_file(pst_wg, a_path);
    wg_destroy(pst_wg);

    if (s32_rc != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "run failed rc=%ld", (long)s32_rc);
        report(0, "i24-encoding", a_detail);
        (void)remove(a_path);
        return;
    }

    p_file = read_whole_file(a_path, &z_file);
    if (p_file == NULL || z_file < 64u) {
        (void)snprintf(a_detail, sizeof(a_detail), "cannot read output");
    } else {
        (void)memcpy(&f64_u_scale, p_file + 40u, sizeof(f64_u_scale));
        (void)memcpy(&f64_i_scale, p_file + 48u, sizeof(f64_i_scale));

        z_off = 64u;
        while (z_off + 56u <= z_file && memcmp(p_file + z_off, "WGF1", 4u) == 0) {
            uint32_t u32_frames = ld_u32(p_file + z_off + 40u);
            uint32_t u32_ch = (uint32_t)p_file[z_off + 48u]
                            | ((uint32_t)p_file[z_off + 49u] << 8);
            uint32_t u32_payload = ld_u32(p_file + z_off + 52u);
            uint32_t u32_i;
            uint32_t u32_bps;

            z_off += 56u;
            if (z_off + (size_t)u32_payload > z_file
                || u32_ch == 0u || u32_frames == 0u) {
                break;
            }
            u32_bps = u32_payload / (u32_frames * u32_ch);
            if (u32_bps == 3u) {
                for (u32_i = 0u;
                     u32_i < (uint32_t)u32_payload;
                     u32_i += 3u) {
                    int32_t s32_code = (int32_t)((uint32_t)p_file[z_off + u32_i]
                                       | ((uint32_t)p_file[z_off + u32_i + 1u] << 8)
                                       | ((uint32_t)p_file[z_off + u32_i + 2u] << 16));

                    if (s32_code >= 0x800000) {
                        s32_code -= 0x1000000;
                    }
                    if (s32_code < s32_min) {
                        s32_min = s32_code;
                    }
                    if (s32_code > s32_max) {
                        s32_max = s32_code;
                    }
                    u64_codes++;
                }
            }
            z_off += (size_t)u32_payload;
        }

        s32_ok = (f64_u_scale > 0.0) && (f64_i_scale > 0.0)
              && (s32_min >= TEST_I24_MIN) && (s32_max <= TEST_I24_MAX)
              && (s32_max >= TEST_I24_MAX - 8) && (u64_codes == 204800u);
        (void)snprintf(a_detail, sizeof(a_detail),
                       "u_scale=%.6e i_scale=%.6e code=[%ld,%ld] codes=%llu",
                       f64_u_scale, f64_i_scale, (long)s32_min, (long)s32_max,
                       (unsigned long long)u64_codes);
    }
    report(s32_ok, "i24-encoding", a_detail);

    free(p_file);
    {
        char a_man[288];

        (void)snprintf(a_man, sizeof(a_man), "%s.manifest.json", a_path);
        (void)remove(a_man);
    }
    (void)remove(a_path);
}

/* ------------------------------------------------------------------ */
/* 7. 660 s streaming long run with RSS check                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t u64_samples;
    uint64_t u64_frames;
    uint64_t u64_sum;
} t7_ctx_t;

static int32_t t7_sink(const wg_frame_t *pst_frame, void *p_ctx)
{
    t7_ctx_t *pst = (t7_ctx_t *)p_ctx;
    const unsigned char *p_bytes = (const unsigned char *)pst_frame->samples;
    size_t z_bytes = (size_t)pst_frame->frames
                   * (size_t)pst_frame->channel_count * 8u;
    size_t z_i;
    uint64_t u64_sum = pst->u64_sum;

    if (z_bytes > 16u) {
        z_bytes = 16u;
    }
    for (z_i = 0u; z_i < z_bytes; z_i++) {
        u64_sum = (u64_sum ^ (uint64_t)p_bytes[z_i]) * TEST_FNV_PRIME;
    }
    u64_sum = (u64_sum ^ (uint64_t)pst_frame->frames) * TEST_FNV_PRIME;
    pst->u64_sum = u64_sum;
    pst->u64_frames++;
    pst->u64_samples += (uint64_t)pst_frame->frames;
    return 0;
}

static void test_long_run(void)
{
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    t7_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    long s32_rss_before;
    long s32_rss_after;
    double f64_t0;
    double f64_elapsed;
    int s32_rss_ok;
    int s32_ok;
    int32_t s32_rc;

    wg_config_default(&st_cfg);
    if (wg_scenario_apply("A2.6.1", 50, &st_cfg, a_err, sizeof(a_err)) != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "scenario failed: %s", a_err);
        report(0, "longrun-660s", a_detail);
        return;
    }
    st_cfg.start_utc_ns = 1794700800000000000ull;

    (void)memset(&st_ctx, 0, sizeof(st_ctx));
    st_ctx.u64_sum = TEST_FNV_OFF;

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "longrun-660s", a_detail);
        return;
    }

    s32_rss_before = read_rss_pages();
    f64_t0 = now_sec();
    s32_rc = wg_run_stream(pst_wg, t7_sink, &st_ctx);
    f64_elapsed = now_sec() - f64_t0;
    s32_rss_after = read_rss_pages();
    wg_destroy(pst_wg);

    s32_rss_ok = (s32_rss_before > 0) && (s32_rss_after > 0)
              && (labs(s32_rss_after - s32_rss_before) * 10L < s32_rss_before);
    s32_ok = (s32_rc == 0)
          && (st_ctx.u64_samples == 33792000ull)
          && (st_ctx.u64_frames == 132000ull)
          && s32_rss_ok;
    (void)snprintf(a_detail, sizeof(a_detail),
                   "samples=%llu frames=%llu rss=%ld->%ld pages elapsed=%.2fs sum=%016llx",
                   (unsigned long long)st_ctx.u64_samples,
                   (unsigned long long)st_ctx.u64_frames,
                   s32_rss_before, s32_rss_after, f64_elapsed,
                   (unsigned long long)st_ctx.u64_sum);
    report(s32_ok, "longrun-660s", a_detail);
}

/* ------------------------------------------------------------------ */
/* 8. layered-signal tests (builder / modulators / presets)           */
/* ------------------------------------------------------------------ */

#define TEST_MAX_CAPTURE_CH 7

typedef struct {
    double *p_plane[TEST_MAX_CAPTURE_CH];
    size_t  z_len[TEST_MAX_CAPTURE_CH];
    size_t  z_cap[TEST_MAX_CAPTURE_CH];
    uint32_t u32_nch;
    int s32_bad;
} cap_ctx_t;

static void cap_free(cap_ctx_t *pst_ctx)
{
    uint32_t c;

    for (c = 0u; c < (uint32_t)TEST_MAX_CAPTURE_CH; c++) {
        free(pst_ctx->p_plane[c]);
        pst_ctx->p_plane[c] = NULL;
        pst_ctx->z_len[c] = 0u;
        pst_ctx->z_cap[c] = 0u;
    }
}

static int cap_init(cap_ctx_t *pst_ctx, uint32_t u32_nch, size_t z_samples)
{
    uint32_t c;

    (void)memset(pst_ctx, 0, sizeof(*pst_ctx));
    if (u32_nch == 0u || u32_nch > (uint32_t)TEST_MAX_CAPTURE_CH
        || z_samples == 0u) {
        return -1;
    }
    pst_ctx->u32_nch = u32_nch;
    for (c = 0u; c < u32_nch; c++) {
        pst_ctx->p_plane[c] = (double *)malloc(z_samples * sizeof(double));
        if (pst_ctx->p_plane[c] == NULL) {
            cap_free(pst_ctx);
            return -1;
        }
        pst_ctx->z_cap[c] = z_samples;
    }
    return 0;
}

static int32_t cap_sink(const wg_frame_t *pst_frame, void *p_ctx)
{
    cap_ctx_t *pst = (cap_ctx_t *)p_ctx;
    const double *p_samples = (const double *)pst_frame->samples;
    size_t z_frames = (size_t)pst_frame->frames;
    uint32_t c;

    if (pst == NULL || p_samples == NULL
        || pst_frame->channel_count != pst->u32_nch) {
        pst->s32_bad = 1;
        return -1;
    }
    for (c = 0u; c < pst->u32_nch; c++) {
        if (pst->z_len[c] + z_frames > pst->z_cap[c]) {
            pst->s32_bad = 1;
            return -1;
        }
        (void)memcpy(pst->p_plane[c] + pst->z_len[c],
                     p_samples + (size_t)c * z_frames,
                     z_frames * sizeof(double));
        pst->z_len[c] += z_frames;
    }
    return 0;
}

static int cap_run(wg_handle_t *pst_wg, cap_ctx_t *pst_ctx)
{
    int32_t s32_rc = wg_run_stream(pst_wg, cap_sink, pst_ctx);

    return (s32_rc == 0 && pst_ctx->s32_bad == 0) ? 0 : -1;
}

static double wrap_deg(double f64_deg)
{
    double f64_v = fmod(f64_deg + 180.0, 360.0);

    if (f64_v < 0.0) {
        f64_v += 360.0;
    }
    return f64_v - 180.0;
}

/* Goertzel RMS/phase over an integer number of cycles of f64_freq. */
static int dft_rms_phase(const double *p_x, size_t z_n, double f64_fs,
                         double f64_freq, double *pf64_rms,
                         double *pf64_phase_deg)
{
    size_t z_use;
    size_t i;
    double f64_cycles;
    double f64_w;
    double f64_coeff;
    double f64_s1 = 0.0;
    double f64_s2 = 0.0;
    double f64_re;
    double f64_im;
    double f64_phi;
    double f64_c;
    double f64_s;
    double f64_rr;
    double f64_ii;

    if (p_x == NULL || z_n < 2u || !(f64_freq > 0.0)
        || f64_freq >= f64_fs * 0.5) {
        return -1;
    }
    f64_cycles = (double)z_n * f64_freq / f64_fs;
    if (f64_cycles >= 1.0) {
        z_use = (size_t)llround(f64_cycles * f64_fs / f64_freq);
        if (z_use < 2u) {
            z_use = 2u;
        }
        if (z_use > z_n) {
            z_use = z_n;
        }
    } else {
        z_use = z_n;
    }

    f64_w = TEST_TWO_PI * f64_freq / f64_fs;
    f64_coeff = 2.0 * cos(f64_w);
    for (i = 0u; i < z_use; i++) {
        double f64_s0 = p_x[i] + f64_coeff * f64_s1 - f64_s2;

        f64_s2 = f64_s1;
        f64_s1 = f64_s0;
    }
    f64_re = f64_s1 - f64_s2 * cos(f64_w);
    f64_im = f64_s2 * sin(f64_w);
    f64_phi = (double)(z_use - 1u) * f64_w;
    f64_c = cos(f64_phi);
    f64_s = sin(f64_phi);
    f64_rr = f64_re * f64_c + f64_im * f64_s;
    f64_ii = f64_im * f64_c - f64_re * f64_s;

    *pf64_rms = TEST_SQRT2 * hypot(f64_rr, f64_ii) / (double)z_use;
    *pf64_phase_deg = atan2(f64_ii, f64_rr) * (180.0 / TEST_PI);
    *pf64_phase_deg = wrap_deg(*pf64_phase_deg);
    return 0;
}

static int dft_window(const double *p_x, size_t z_total, double f64_fs,
                      double f64_t0, double f64_t1, double f64_freq,
                      double *pf64_rms, double *pf64_phase_deg)
{
    size_t z_i0 = (size_t)llround(f64_t0 * f64_fs);
    size_t z_i1 = (size_t)llround(f64_t1 * f64_fs);

    if (z_i0 >= z_total) {
        return -1;
    }
    if (z_i1 > z_total) {
        z_i1 = z_total;
    }
    if (z_i1 <= z_i0 + 1u) {
        return -1;
    }
    return dft_rms_phase(p_x + z_i0, z_i1 - z_i0, f64_fs, f64_freq,
                         pf64_rms, pf64_phase_deg);
}

/* Frequency from rising zero crossings (linear interpolation), window mean. */
static double crossing_freq_window(const double *p_x, size_t z_total,
                                   double f64_fs, double f64_t0,
                                   double f64_t1)
{
    size_t z_i0 = (size_t)llround(f64_t0 * f64_fs);
    size_t z_i1 = (size_t)llround(f64_t1 * f64_fs);
    size_t i;
    size_t z_count = 0u;
    double f64_first = 0.0;
    double f64_last = 0.0;

    if (z_i0 + 1u >= z_total) {
        return 0.0;
    }
    if (z_i1 > z_total) {
        z_i1 = z_total;
    }
    for (i = z_i0 + 1u; i < z_i1; i++) {
        double f64_prev = p_x[i - 1u];
        double f64_cur = p_x[i];

        if (f64_prev < 0.0 && f64_cur >= 0.0) {
            double f64_den = f64_cur - f64_prev;
            double f64_frac = (f64_den != 0.0) ? (-f64_prev) / f64_den : 0.0;
            double f64_t = ((double)(i - 1u) + f64_frac) / f64_fs;

            if (z_count == 0u) {
                f64_first = f64_t;
            }
            f64_last = f64_t;
            z_count++;
        }
    }
    if (z_count < 2u || !(f64_last > f64_first)) {
        return 0.0;
    }
    return (double)(z_count - 1u) / (f64_last - f64_first);
}

static double triangle_value(double f64_t, double f64_lo, double f64_hi,
                             double f64_period)
{
    double f64_pos;
    double f64_half;
    double f64_ratio;

    if (!(f64_period > 0.0)) {
        return f64_lo;
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
    return f64_lo + (f64_hi - f64_lo) * f64_ratio;
}

static double triangle_window_mean(double a, double b, double f64_lo,
                                   double f64_hi, double f64_period)
{
    const int s32_steps = 400;
    double f64_h = (b - a) / (double)s32_steps;
    double f64_acc = 0.0;
    int i;

    for (i = 0; i < s32_steps; i++) {
        f64_acc += triangle_value(a + ((double)i + 0.5) * f64_h,
                                  f64_lo, f64_hi, f64_period);
    }
    return f64_acc / (double)s32_steps;
}

static void comp_init(wg_component_t *pst_c, uint8_t u8_kind, double f64_order,
                      double f64_freq, double f64_amp, double f64_phase,
                      uint8_t u8_ref)
{
    int i;

    (void)memset(pst_c, 0, sizeof(*pst_c));
    pst_c->kind = u8_kind;
    pst_c->order = f64_order;
    pst_c->frequency_hz = f64_freq;
    pst_c->channel_mask = 0x01u;
    pst_c->amplitude_ref = u8_ref;
    for (i = 0; i < WG_MAX_CHANNELS; i++) {
        pst_c->amp_pct[i] = f64_amp;
        pst_c->phase_deg[i] = f64_phase;
    }
}

static wg_builder_t *builder_single(uint32_t u32_fs, double f64_duration)
{
    const uint8_t au8_ch[1] = { (uint8_t)WG_CH_UA };
    const double af64_nominal[1] = { 220.0 };
    wg_builder_t *pst_b;

    pst_b = wg_builder_create(u32_fs, 0u);
    if (pst_b == NULL) {
        return NULL;
    }
    if (wg_builder_set_channels(pst_b, au8_ch, 1u, af64_nominal) != 0
        || wg_builder_set_duration(pst_b, f64_duration) != 0
        || wg_builder_set_output(pst_b, (uint8_t)WG_FMT_F64, 256u) != 0) {
        wg_builder_destroy(pst_b);
        return NULL;
    }
    return pst_b;
}

static void test_builder_arbitrary(void)
{
    const uint32_t u32_fs = 51200u;
    const size_t z_n = 51200u;
    wg_builder_t *pst_b;
    wg_handle_t *pst_wg;
    wg_component_t st_c;
    cap_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    double f64_rms_f;
    double f64_ph_f;
    double f64_rms_h3;
    double f64_ph_h3;
    double f64_rms_h25;
    double f64_ph_h25;
    double f64_rel3;
    double f64_rel25;
    int s32_ok = 0;

    pst_b = builder_single(u32_fs, 1.0);
    if (pst_b == NULL) {
        report(0, "builder-arbitrary", "builder create failed");
        return;
    }
    comp_init(&st_c, (uint8_t)WG_COMP_FUNDAMENTAL, 1.0, 50.0, 100.0,
              0.0, 0u);
    if (wg_builder_add_component(pst_b, &st_c) != 0) {
        wg_builder_destroy(pst_b);
        report(0, "builder-arbitrary", "add fundamental failed");
        return;
    }
    comp_init(&st_c, (uint8_t)WG_COMP_HARMONIC, 3.0, 0.0, 7.0, 45.0, 1u);
    (void)wg_builder_add_component(pst_b, &st_c);
    comp_init(&st_c, (uint8_t)WG_COMP_HARMONIC, 25.0, 0.0, 0.5, 90.0, 1u);
    (void)wg_builder_add_component(pst_b, &st_c);

    pst_wg = wg_create_from_builder(pst_b, a_err, sizeof(a_err));
    wg_builder_destroy(pst_b);
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "builder-arbitrary", a_detail);
        return;
    }
    if (cap_init(&st_ctx, 1u, z_n) != 0 || cap_run(pst_wg, &st_ctx) != 0) {
        wg_destroy(pst_wg);
        cap_free(&st_ctx);
        report(0, "builder-arbitrary", "capture failed");
        return;
    }
    wg_destroy(pst_wg);

    s32_ok = (dft_rms_phase(st_ctx.p_plane[0], z_n, (double)u32_fs, 50.0,
                            &f64_rms_f, &f64_ph_f) == 0)
          && (dft_rms_phase(st_ctx.p_plane[0], z_n, (double)u32_fs, 150.0,
                            &f64_rms_h3, &f64_ph_h3) == 0)
          && (dft_rms_phase(st_ctx.p_plane[0], z_n, (double)u32_fs, 1250.0,
                            &f64_rms_h25, &f64_ph_h25) == 0);
    if (s32_ok != 0) {
        f64_rel3 = fabs(f64_rms_h3 - 15.4) / 15.4;
        f64_rel25 = fabs(f64_rms_h25 - 1.1) / 1.1;
        s32_ok = (fabs(f64_rms_f - 220.0) / 220.0 <= 1.0e-3)
              && (f64_rel3 <= 1.0e-3) && (f64_rel25 <= 1.0e-3)
              && (fabs(wrap_deg(f64_ph_f)) <= 0.1)
              && (fabs(wrap_deg(f64_ph_h3 - 45.0)) <= 0.1)
              && (fabs(wrap_deg(f64_ph_h25 - 90.0)) <= 0.1);
    } else {
        f64_rel3 = -1.0;
        f64_rel25 = -1.0;
    }
    (void)snprintf(a_detail, sizeof(a_detail),
                   "fund=%.3fV/%.2fdeg H3=%.3fV/%.2fdeg H25=%.3fV/%.2fdeg "
                   "rel=%.2e/%.2e",
                   f64_rms_f, f64_ph_f, f64_rms_h3, f64_ph_h3,
                   f64_rms_h25, f64_ph_h25, f64_rel3, f64_rel25);
    report(s32_ok, "builder-arbitrary", a_detail);
    cap_free(&st_ctx);
}

static void test_per_component_envelope(void)
{
    const uint32_t u32_fs = 51200u;
    const size_t z_n = 51200u;
    wg_builder_t *pst_b;
    wg_handle_t *pst_wg;
    wg_component_t st_c;
    wg_modulator_t st_mod;
    cap_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    double f64_fund;
    double f64_ph;
    double f64_rms_early;
    double f64_rms_late;
    double f64_ratio;
    int s32_ok;

    pst_b = builder_single(u32_fs, 1.0);
    if (pst_b == NULL) {
        report(0, "per-component-envelope", "builder create failed");
        return;
    }
    comp_init(&st_c, (uint8_t)WG_COMP_FUNDAMENTAL, 1.0, 50.0, 100.0,
              0.0, 0u);
    (void)wg_builder_add_component(pst_b, &st_c);
    comp_init(&st_c, (uint8_t)WG_COMP_HARMONIC, 3.0, 0.0, 7.0, 0.0, 1u);
    (void)wg_builder_add_component(pst_b, &st_c);
    (void)memset(&st_mod, 0, sizeof(st_mod));
    st_mod.kind = (uint8_t)WG_MOD2_AMP_LINEAR;
    st_mod.from = 10.0;
    st_mod.to = 0.0;
    st_mod.duration_s = 1.0;
    (void)wg_builder_add_modulator(pst_b, 1u, &st_mod);

    pst_wg = wg_create_from_builder(pst_b, a_err, sizeof(a_err));
    wg_builder_destroy(pst_b);
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "per-component-envelope", a_detail);
        return;
    }
    if (cap_init(&st_ctx, 1u, z_n) != 0 || cap_run(pst_wg, &st_ctx) != 0) {
        wg_destroy(pst_wg);
        cap_free(&st_ctx);
        report(0, "per-component-envelope", "capture failed");
        return;
    }
    wg_destroy(pst_wg);

    s32_ok = (dft_window(st_ctx.p_plane[0], z_n, (double)u32_fs, 0.0, 0.2,
                         150.0, &f64_rms_early, &f64_ph) == 0)
          && (dft_window(st_ctx.p_plane[0], z_n, (double)u32_fs, 0.8, 1.0,
                         150.0, &f64_rms_late, &f64_ph) == 0)
          && (dft_window(st_ctx.p_plane[0], z_n, (double)u32_fs, 0.0, 1.0,
                         50.0, &f64_fund, &f64_ph) == 0);
    if (s32_ok != 0) {
        f64_ratio = (f64_rms_late > 0.0) ? f64_rms_early / f64_rms_late : 0.0;
        s32_ok = (fabs(f64_fund - 220.0) / 220.0 <= 1.0e-3)
              && (fabs(f64_ratio - 9.0) / 9.0 <= 0.02);
    } else {
        f64_ratio = 0.0;
    }
    (void)snprintf(a_detail, sizeof(a_detail),
                   "fund=%.3fV H3 0-0.2s=%.4fV 0.8-1s=%.4fV ratio=%.3f (exp 9)",
                   f64_fund, f64_rms_early, f64_rms_late, f64_ratio);
    report(s32_ok, "per-component-envelope", a_detail);
    cap_free(&st_ctx);
}

static double stacked_freq(double f64_t)
{
    return triangle_value(f64_t, 40.0, 60.0, 6.0);
}

static double stacked_amp_factor(double f64_t)
{
    return 1.0 + 0.10 * cos(TEST_TWO_PI * 2.3 * f64_t);
}

/* Least-squares fit x = a*cos(phi) + b*sin(phi); the amplitude factor is
 * folded into the basis so the fit also reports the relative scale. */
static int stacked_offset(const double *p_x, size_t z_n, double f64_fs,
                          double f64_t0, double f64_t1, double *pf64_offset,
                          double *pf64_scale)
{
    size_t z_i0 = (size_t)llround(f64_t0 * f64_fs);
    size_t z_i1 = (size_t)llround(f64_t1 * f64_fs);
    size_t i;
    double f64_phi = 0.0;
    double f64_scc = 0.0;
    double f64_sss = 0.0;
    double f64_scs = 0.0;
    double f64_rc = 0.0;
    double f64_rs = 0.0;
    double f64_det;
    double f64_a;
    double f64_b;

    if (z_i0 >= z_n || z_i1 > z_n || z_i1 <= z_i0) {
        return -1;
    }
    for (i = 0u; i < z_i0; i++) {
        f64_phi += TEST_TWO_PI * stacked_freq((double)i / f64_fs) / f64_fs;
    }
    for (i = z_i0; i < z_i1; i++) {
        double f64_t = (double)i / f64_fs;
        double f64_peak = 220.0 * TEST_SQRT2 * stacked_amp_factor(f64_t);
        double f64_c = f64_peak * cos(f64_phi);
        double f64_s = f64_peak * sin(f64_phi);

        f64_scc += f64_c * f64_c;
        f64_sss += f64_s * f64_s;
        f64_scs += f64_c * f64_s;
        f64_rc += p_x[i] * f64_c;
        f64_rs += p_x[i] * f64_s;
        f64_phi += TEST_TWO_PI * stacked_freq(f64_t) / f64_fs;
    }
    f64_det = f64_scc * f64_sss - f64_scs * f64_scs;
    if (!(fabs(f64_det) > 0.0)) {
        return -1;
    }
    f64_a = (f64_rc * f64_sss - f64_rs * f64_scs) / f64_det;
    f64_b = (f64_rs * f64_scc - f64_rc * f64_scs) / f64_det;
    *pf64_offset = atan2(-f64_b, f64_a) * (180.0 / TEST_PI);
    *pf64_scale = hypot(f64_a, f64_b);
    return 0;
}

/* Exact analytic peak of |A(t) cos(phi(t) + step(t))| on the sample grid. */
static double stacked_peak_expected(double f64_fs, double f64_t0,
                                    double f64_t1)
{
    size_t z_i0 = (size_t)llround(f64_t0 * f64_fs);
    size_t z_i1 = (size_t)llround(f64_t1 * f64_fs);
    size_t i;
    double f64_phi = 0.0;
    double f64_peak = 0.0;

    for (i = 0u; i < z_i0; i++) {
        f64_phi += TEST_TWO_PI * stacked_freq((double)i / f64_fs) / f64_fs;
    }
    for (i = z_i0; i < z_i1; i++) {
        double f64_t = (double)i / f64_fs;
        double f64_step = (f64_t >= 0.5) ? 30.0 * (TEST_PI / 180.0) : 0.0;
        double f64_v = 220.0 * TEST_SQRT2 * stacked_amp_factor(f64_t)
                     * fabs(cos(f64_phi + f64_step));

        if (f64_v > f64_peak) {
            f64_peak = f64_v;
        }
        f64_phi += TEST_TWO_PI * stacked_freq(f64_t) / f64_fs;
    }
    return f64_peak;
}

static double window_peak(const double *p_x, size_t z_n, double f64_fs,
                          double f64_t0, double f64_t1)
{
    size_t z_i0 = (size_t)llround(f64_t0 * f64_fs);
    size_t z_i1 = (size_t)llround(f64_t1 * f64_fs);
    size_t i;
    double f64_peak = 0.0;

    if (z_i0 >= z_n) {
        return 0.0;
    }
    if (z_i1 > z_n) {
        z_i1 = z_n;
    }
    for (i = z_i0; i < z_i1; i++) {
        double f64_abs = fabs(p_x[i]);

        if (f64_abs > f64_peak) {
            f64_peak = f64_abs;
        }
    }
    return f64_peak;
}

static void test_stacked_modulation(void)
{
    const uint32_t u32_fs = 51200u;
    const size_t z_n = 102400u;
    const double af64_win[3][2] = { { 0.2, 0.4 }, { 0.8, 1.0 },
                                    { 1.4, 1.6 } };
    const double af64_env[3] = { 0.1, 1.0, 1.9 };
    wg_builder_t *pst_b;
    wg_handle_t *pst_wg;
    wg_component_t st_c;
    wg_modulator_t st_mod;
    wg_event_t st_evt;
    cap_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    double f64_before = 0.0;
    double f64_after = 0.0;
    double f64_scale_b = 0.0;
    double f64_scale_a = 0.0;
    double f64_jump;
    double f64_max_freq_err = 0.0;
    double f64_max_env_err = 0.0;
    int i;
    int s32_ok = 0;

    pst_b = builder_single(u32_fs, 2.0);
    if (pst_b == NULL) {
        report(0, "stacked-modulation", "builder create failed");
        return;
    }
    comp_init(&st_c, (uint8_t)WG_COMP_FUNDAMENTAL, 1.0, 50.0, 100.0,
              0.0, 0u);
    if (wg_builder_add_component(pst_b, &st_c) != 0) {
        wg_builder_destroy(pst_b);
        report(0, "stacked-modulation", "add fundamental failed");
        return;
    }
    (void)memset(&st_mod, 0, sizeof(st_mod));
    st_mod.kind = (uint8_t)WG_MOD2_FREQ_TRIANGLE;
    st_mod.from = 40.0;
    st_mod.to = 60.0;
    st_mod.period_s = 6.0;
    (void)wg_builder_add_modulator(pst_b, 0u, &st_mod);
    (void)memset(&st_mod, 0, sizeof(st_mod));
    st_mod.kind = (uint8_t)WG_MOD2_AMP_SINE;
    st_mod.frequency_hz = 2.3;
    st_mod.depth_pct = 10.0;
    (void)wg_builder_add_modulator(pst_b, 0u, &st_mod);
    (void)memset(&st_evt, 0, sizeof(st_evt));
    st_evt.kind = (uint8_t)WG_EVENT_PHASE_STEP;
    st_evt.channel_mask = 0x01u;
    st_evt.time_s = 0.5;
    st_evt.value = 30.0;
    (void)wg_builder_add_event(pst_b, &st_evt);

    pst_wg = wg_create_from_builder(pst_b, a_err, sizeof(a_err));
    wg_builder_destroy(pst_b);
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "stacked-modulation", a_detail);
        return;
    }
    if (cap_init(&st_ctx, 1u, z_n) != 0 || cap_run(pst_wg, &st_ctx) != 0) {
        wg_destroy(pst_wg);
        cap_free(&st_ctx);
        report(0, "stacked-modulation", "capture failed");
        return;
    }
    wg_destroy(pst_wg);

    s32_ok = (stacked_offset(st_ctx.p_plane[0], z_n, (double)u32_fs, 0.3,
                             0.5, &f64_before, &f64_scale_b) == 0)
          && (stacked_offset(st_ctx.p_plane[0], z_n, (double)u32_fs, 0.5,
                             0.7, &f64_after, &f64_scale_a) == 0);
    f64_jump = wrap_deg(f64_after - f64_before);
    if (s32_ok != 0) {
        s32_ok = (fabs(f64_jump - 30.0) <= 0.2)
              && (fabs(f64_scale_b - 1.0) <= 0.001)
              && (fabs(f64_scale_a - 1.0) <= 0.001);
    }
    for (i = 0; i < 3 && s32_ok != 0; i++) {
        double f64_exp_f = triangle_window_mean(af64_win[i][0],
                                                af64_win[i][1],
                                                40.0, 60.0, 6.0);
        double f64_meas_f = crossing_freq_window(st_ctx.p_plane[0], z_n,
                                                 (double)u32_fs,
                                                 af64_win[i][0],
                                                 af64_win[i][1]);
        double f64_mid = af64_env[i];
        double f64_exp_peak = stacked_peak_expected((double)u32_fs,
                                                    f64_mid - 0.05,
                                                    f64_mid + 0.05);
        double f64_meas_peak = window_peak(st_ctx.p_plane[0], z_n,
                                           (double)u32_fs, f64_mid - 0.05,
                                           f64_mid + 0.05);
        double f64_freq_err = fabs(f64_meas_f - f64_exp_f);
        double f64_env_err = fabs(f64_meas_peak - f64_exp_peak) / f64_exp_peak;

        if (f64_freq_err > f64_max_freq_err) {
            f64_max_freq_err = f64_freq_err;
        }
        if (f64_env_err > f64_max_env_err) {
            f64_max_env_err = f64_env_err;
        }
    }
    if (s32_ok != 0) {
        s32_ok = (f64_max_freq_err <= 0.1) && (f64_max_env_err <= 0.01);
    }
    (void)snprintf(a_detail, sizeof(a_detail),
                   "jump=%.3fdeg before=%.3f after=%.3f scale=%.4f/%.4f "
                   "max_freq_err=%.4fHz max_env_err=%.2f%%",
                   f64_jump, f64_before, f64_after, f64_scale_b, f64_scale_a,
                   f64_max_freq_err, f64_max_env_err * 100.0);
    report(s32_ok, "stacked-modulation", a_detail);
    cap_free(&st_ctx);
}

static void test_custom_triangle(void)
{
    const uint32_t u32_fs = 51200u;
    const size_t z_n = 12u * 51200u;
    const double af64_win[4][2] = { { 0.0, 0.2 }, { 2.9, 3.1 },
                                    { 6.0, 6.2 }, { 8.9, 9.1 } };
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    cap_ctx_t st_ctx;
    char a_err[256];
    char a_detail[512];
    double af64_meas[4];
    double f64_max_err = 0.0;
    int i;
    int s32_ok = 1;

    wg_config_default(&st_cfg);
    if (wg_scenario_apply("A1.1.1", 50, &st_cfg, a_err, sizeof(a_err)) != 0) {
        (void)snprintf(a_detail, sizeof(a_detail), "scenario failed: %s", a_err);
        report(0, "custom-triangle", a_detail);
        return;
    }
    st_cfg.duration_s = 12.0;
    st_cfg.signal.frequency.profile = (uint8_t)WG_PROFILE_TRIANGLE;
    st_cfg.signal.frequency.from = 40.0;
    st_cfg.signal.frequency.to = 60.0;
    st_cfg.signal.frequency.period_s = 6.0;
    st_cfg.start_utc_ns = 0u;

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        (void)snprintf(a_detail, sizeof(a_detail), "create failed: %s", a_err);
        report(0, "custom-triangle", a_detail);
        return;
    }
    if (cap_init(&st_ctx, 3u, z_n) != 0 || cap_run(pst_wg, &st_ctx) != 0) {
        wg_destroy(pst_wg);
        cap_free(&st_ctx);
        report(0, "custom-triangle", "capture failed");
        return;
    }
    wg_destroy(pst_wg);

    for (i = 0; i < 4; i++) {
        double f64_exp = triangle_window_mean(af64_win[i][0],
                                              af64_win[i][1],
                                              40.0, 60.0, 6.0);
        double f64_err;

        af64_meas[i] = crossing_freq_window(st_ctx.p_plane[0], z_n,
                                            (double)u32_fs,
                                            af64_win[i][0], af64_win[i][1]);
        f64_err = fabs(af64_meas[i] - f64_exp);
        if (f64_err > f64_max_err) {
            f64_max_err = f64_err;
        }
        if (f64_err > 0.1) {
            s32_ok = 0;
        }
    }
    if (fabs(af64_meas[0] - af64_meas[2]) > 0.1
        || fabs(af64_meas[1] - af64_meas[3]) > 0.1) {
        s32_ok = 0;
    }
    (void)snprintf(a_detail, sizeof(a_detail),
                   "t0=%.3f t3=%.3f t6=%.3f t9=%.3f Hz max_err=%.4fHz "
                   "period=%.2fs",
                   af64_meas[0], af64_meas[1], af64_meas[2], af64_meas[3],
                   f64_max_err, 6.0);
    report(s32_ok, "custom-triangle", a_detail);
    cap_free(&st_ctx);
}

static void test_no_default(void)
{
    wg_config_t st_cfg;
    char a_err[256];
    char a_detail[512];
    int32_t s32_rc;

    wg_config_default(&st_cfg);
    s32_rc = wg_scenario_apply("A6.2.5", 50, &st_cfg, a_err, sizeof(a_err));
    (void)snprintf(a_detail, sizeof(a_detail),
                   "rc=%ld error=\"%s\"", (long)s32_rc, a_err);
    report((s32_rc != 0) && (strncmp(a_err, "NO_DEFAULT", 10u) == 0),
           "no-default", a_detail);
}

static void test_old_compat(void)
{
    const uint32_t u32_fs = 51200u;
    const size_t z_n = 25600u;
    const double af64_phase[3] = { 0.0, -120.0, 120.0 };
    wg_config_t st_legacy;
    wg_config_t st_program;
    wg_handle_t *pst_wg;
    cap_ctx_t st_leg;
    cap_ctx_t st_prog;
    char a_err[256];
    char a_detail[512];
    double f64_max_rel = 0.0;
    double f64_max_phase = 0.0;
    int s32_ok = 1;
    int c;
    int k;

    wg_config_default(&st_legacy);
    st_legacy.duration_s = 0.5;
    st_legacy.frame_samples = 256u;
    st_legacy.start_utc_ns = 0u;
    (void)memset(&st_legacy.program, 0, sizeof(st_legacy.program));
    set_amp(&st_legacy, 100.0);
    st_legacy.signal.harmonic_count = 1u;
    st_legacy.signal.harmonics[0].order = 3u;
    st_legacy.signal.harmonics[0].amplitude_pct = 7.0;
    st_legacy.signal.harmonics[0].phase_deg = 45.0;

    wg_config_default(&st_program);
    st_program.duration_s = 0.5;
    st_program.frame_samples = 256u;
    st_program.start_utc_ns = 0u;
    st_program.program.component_count = 2u;
    comp_init(&st_program.program.components[0],
              (uint8_t)WG_COMP_FUNDAMENTAL, 1.0, 0.0, 100.0, 0.0, 0u);
    comp_init(&st_program.program.components[1],
              (uint8_t)WG_COMP_HARMONIC, 3.0, 0.0, 7.0, 45.0, 1u);
    for (c = 0; c < 3; c++) {
        st_program.program.components[0].phase_deg[c] = af64_phase[c];
        st_program.program.components[1].phase_deg[c] = 45.0 + af64_phase[c];
        st_program.program.components[0].amp_pct[c] = 100.0;
        st_program.program.components[1].amp_pct[c] = 7.0;
    }
    st_program.program.components[0].channel_mask = 0x07u;
    st_program.program.components[1].channel_mask = 0x07u;

    if (cap_init(&st_leg, 3u, z_n) != 0 || cap_init(&st_prog, 3u, z_n) != 0) {
        cap_free(&st_leg);
        cap_free(&st_prog);
        report(0, "old-compat", "capture allocation failed");
        return;
    }

    pst_wg = wg_create(&st_legacy, a_err, sizeof(a_err));
    if (pst_wg == NULL || cap_run(pst_wg, &st_leg) != 0) {
        if (pst_wg != NULL) {
            wg_destroy(pst_wg);
        }
        cap_free(&st_leg);
        cap_free(&st_prog);
        (void)snprintf(a_detail, sizeof(a_detail), "legacy create/run: %s",
                       a_err);
        report(0, "old-compat", a_detail);
        return;
    }
    wg_destroy(pst_wg);

    pst_wg = wg_create(&st_program, a_err, sizeof(a_err));
    if (pst_wg == NULL || cap_run(pst_wg, &st_prog) != 0) {
        if (pst_wg != NULL) {
            wg_destroy(pst_wg);
        }
        cap_free(&st_leg);
        cap_free(&st_prog);
        (void)snprintf(a_detail, sizeof(a_detail), "program create/run: %s",
                       a_err);
        report(0, "old-compat", a_detail);
        return;
    }
    wg_destroy(pst_wg);

    for (c = 0; c < 3; c++) {
        for (k = 0; k < 2; k++) {
            double f64_freq = (k == 0) ? 50.0 : 150.0;
            double f64_rms_a;
            double f64_rms_b;
            double f64_ph_a;
            double f64_ph_b;
            double f64_den;
            double f64_rel;

            if (dft_rms_phase(st_leg.p_plane[c], z_n, (double)u32_fs,
                              f64_freq, &f64_rms_a, &f64_ph_a) != 0
                || dft_rms_phase(st_prog.p_plane[c], z_n, (double)u32_fs,
                                 f64_freq, &f64_rms_b, &f64_ph_b) != 0) {
                s32_ok = 0;
                continue;
            }
            f64_den = (fabs(f64_rms_a) > 1.0e-12) ? fabs(f64_rms_a) : 1.0;
            f64_rel = fabs(f64_rms_a - f64_rms_b) / f64_den;
            if (f64_rel > f64_max_rel) {
                f64_max_rel = f64_rel;
            }
            if (fabs(wrap_deg(f64_ph_a - f64_ph_b)) > f64_max_phase) {
                f64_max_phase = fabs(wrap_deg(f64_ph_a - f64_ph_b));
            }
            if (f64_rel > 1.0e-6
                || fabs(wrap_deg(f64_ph_a - f64_ph_b)) > 1.0e-6) {
                s32_ok = 0;
            }
        }
    }
    (void)snprintf(a_detail, sizeof(a_detail),
                   "max_rms_rel=%.3e max_phase_diff=%.3e deg (limit 1e-6)",
                   f64_max_rel, f64_max_phase);
    report(s32_ok, "old-compat", a_detail);
    cap_free(&st_leg);
    cap_free(&st_prog);
}

int main(void)
{
    test_config();
    test_stream();
    test_file_stream();
    test_determinism();
    test_phase();
    test_i24();
    test_long_run();
    test_builder_arbitrary();
    test_per_component_envelope();
    test_stacked_modulation();
    test_custom_triangle();
    test_no_default();
    test_old_compat();

    if (g_failures != 0) {
        printf("SUMMARY: %d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("SUMMARY: all tests PASSED\n");
    return 0;
}
