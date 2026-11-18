#ifndef WG_TYPES_H
#define WG_TYPES_H

#include <stdint.h>

#include "wg_signal.h"

#if !defined(WG_MAX_CHANNELS)
#define WG_MAX_CHANNELS        7
#endif

#define WG_MAX_HARMONICS       64
#define WG_MAX_INTERHARMONICS  16
#define WG_MAX_TONES           8
#define WG_MAX_SEGMENTS        16

/* 通道编号，与输入契约中的 Ua/Ub/Uc/Ia/Ib/Ic/In 一一对应。 */
typedef enum {
    WG_CH_UA = 0,
    WG_CH_UB = 1,
    WG_CH_UC = 2,
    WG_CH_IA = 3,
    WG_CH_IB = 4,
    WG_CH_IC = 5,
    WG_CH_IN = 6
} wg_channel_t;

/* 样本格式，取值写入文件头 format 字段。 */
typedef enum {
    WG_FMT_F64 = 0,
    WG_FMT_F32 = 1,
    WG_FMT_I24 = 2
} wg_sample_format_t;

/* 轮廓类型：0=常数（取 from），1=线性，2=三角。 */
typedef enum {
    WG_PROFILE_CONST    = 0,
    WG_PROFILE_LINEAR   = 1,
    WG_PROFILE_TRIANGLE = 2
} wg_profile_kind_t;

/* 调制类型。 */
typedef enum {
    WG_MOD_NONE = 0,
    WG_MOD_SINE = 1,
    WG_MOD_RECT = 2
} wg_modulation_kind_t;

/* 段切换方式：0=立即，1=过零。 */
typedef enum {
    WG_TRANS_IMMEDIATE     = 0,
    WG_TRANS_ZERO_CROSSING = 1
} wg_transition_kind_t;

/* 帧标志位。 */
#define WG_FRAME_FLAG_BREAK  0x0001u
#define WG_FRAME_FLAG_FIRST  0x0002u
#define WG_FRAME_FLAG_LAST   0x0004u

/* 时间轮廓：三角的 period_s 为完整周期（上升+下降）；线性忽略 period_s。 */
typedef struct {
    uint8_t profile;
    double  from;
    double  to;
    double  period_s;
} wg_profile_f64_t;

typedef struct {
    uint32_t order;
    double   amplitude_pct;
    double   phase_deg;
} wg_harmonic_t;

/* order 允许非整数（如 1.5、7.5），相对基波倍频。 */
typedef struct {
    double order;
    double amplitude_pct;
    double phase_deg;
} wg_interharmonic_t;

/* 独立频点分量（载波），不随基波缩放。 */
typedef struct {
    double frequency_hz;
    double amplitude_pct;
    double phase_deg;
} wg_tone_t;

typedef struct {
    uint8_t type;
    double  frequency_hz;
    double  depth_pct;
    double  duty_pct;
    double  phase_deg;
} wg_modulation_t;

/*
 * 信号定义。amp_pct=100 表示该通道 nominal；phase_deg 为基波初相。
 * harmonics/interharmonics/tones 为所有已启用通道共用。
 */
typedef struct {
    wg_profile_f64_t frequency;

    wg_profile_f64_t amp_pct[WG_MAX_CHANNELS];
    double           phase_deg[WG_MAX_CHANNELS];

    wg_harmonic_t       harmonics[WG_MAX_HARMONICS];
    uint16_t            harmonic_count;
    wg_interharmonic_t  interharmonics[WG_MAX_INTERHARMONICS];
    uint16_t            interharmonic_count;
    wg_tone_t           tones[WG_MAX_TONES];
    uint16_t            tone_count;

    wg_modulation_t modulation;
} wg_signal_t;

/* 时间段。channel_delay_s 用于多相错时，正值表示相对段起点的延时。 */
typedef struct {
    double          duration_s;
    uint8_t         transition;
    double          channel_delay_s[WG_MAX_CHANNELS];
    wg_signal_t     signal;
} wg_segment_t;

/*
 * 完整配置。nominal 为通道 RMS 基准（电压 V、电流 A）；
 * duration_s 为总时长；segments 为空（segment_count=0）时使用 signal。
 */
typedef struct {
    uint32_t sample_rate_hz;
    uint64_t start_utc_ns;
    double   duration_s;

    uint8_t  channel_count;
    uint8_t  channels[WG_MAX_CHANNELS];
    double   nominal[WG_MAX_CHANNELS];

    uint8_t  format;
    uint32_t frame_samples;

    wg_signal_t    signal;
    uint16_t       segment_count;
    wg_segment_t   segments[WG_MAX_SEGMENTS];

    /* 分层信号集合（全局分量/事件）；非空时优先于 signal。 */
    wg_program_t   program;
} wg_config_t;

/* 流参数，字段顺序与文件头中的对应字段一致。 */
typedef struct {
    uint32_t sample_rate_hz;
    uint16_t channel_count;
    uint16_t frame_samples;
    uint64_t start_utc_ns;
    double   sample_period_ns;
    uint8_t  format;
    uint8_t  reserved[7];
} wg_stream_info_t;

/* 逐帧交付结构：帧头字段 + 平面样本指针（格式由 format 决定）。 */
typedef struct {
    uint64_t frame_index;
    uint64_t start_sample;
    uint64_t start_utc_ns;
    uint64_t end_utc_ns;
    uint32_t frames;
    uint32_t flags;
    uint16_t channel_count;
    uint16_t reserved;
    const void *samples;
} wg_frame_t;

#endif /* WG_TYPES_H */
