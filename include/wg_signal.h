#ifndef WG_SIGNAL_H
#define WG_SIGNAL_H

#include <stdint.h>

/*
 * 分层信号契约（Signal Core）。
 *
 * 本头文件只定义数据结构，不含任何采样实现：分量（components）与事件
 * （events）组成一个 program，采样器按 program 求值。旧 wg_signal_t 保持
 * 不变，program 非空（component_count 或 event_count 非 0）时 program 优先。
 *
 * 容量是编译期常量，program 以值形式内嵌进 wg_config_t，因此不要随意放大；
 * wg_signal_t 会被 wg_config_t.segments[] 复制，分量数组只允许放在 program，
 * 不得放进 wg_signal_t。
 */

/* 通道数上限；与 wg_types.h 中的同名宏同值。 */
#if !defined(WG_MAX_CHANNELS)
#define WG_MAX_CHANNELS        7
#endif

#define WG_MAX_COMPONENTS      96
#define WG_MAX_MODULATORS      8
#define WG_MAX_EVENTS          64

/* 分量类型。 */
typedef enum {
    WG_COMP_FUNDAMENTAL   = 0,  /* 基波 */
    WG_COMP_HARMONIC      = 1,  /* 谐波：order 为整数次数，相对基波 */
    WG_COMP_INTERHARMONIC = 2,  /* 间谐波：order 可为非整数，相对基波 */
    WG_COMP_TONE          = 3   /* 独立频点（载波）：用 frequency_hz */
} wg_component_kind_t;

/* 调制类型：AMP_* 乘在分量幅值上；FREQ_* 是全局频率轮廓。 */
typedef enum {
    WG_MOD2_NONE          = 0,
    WG_MOD2_AMP_CONST     = 1,
    WG_MOD2_AMP_LINEAR    = 2,
    WG_MOD2_AMP_TRIANGLE  = 3,
    WG_MOD2_AMP_STEP      = 4,
    WG_MOD2_AMP_SINE      = 5,
    WG_MOD2_AMP_RECT      = 6,
    WG_MOD2_FREQ_LINEAR   = 7,
    WG_MOD2_FREQ_TRIANGLE = 8,
    WG_MOD2_FREQ_STEP     = 9
} wg_modulator_kind_t;

/* 事件类型：作用于 channel_mask 指定通道。 */
typedef enum {
    WG_EVENT_AMP_STEP   = 0,    /* value 为幅值百分比 */
    WG_EVENT_PHASE_STEP = 1     /* value 为角度（°） */
} wg_event_kind_t;

/*
 * 调制项。字段按 kind 取用，未用字段置 0：
 *   AMP_CONST      from 为常数因子（%）。
 *   AMP_LINEAR     duration_s 内从 from 线性到 to。
 *   AMP_TRIANGLE   period_s 为完整周期，在 from 与 to 之间往返；
 *                  start_s 前保持 hold（%），从 start_s 起开始三角。
 *   AMP_STEP       start_s 时刻从 from 跳到 to。
 *   AMP_SINE       frequency_hz / depth_pct / phase_deg；start_s 前不调制。
 *   AMP_RECT       frequency_hz / depth_pct / duty_pct / phase_deg；start_s 前不调制。
 *   FREQ_*         仅允许挂在基波分量上，作为全局频率轮廓（from/to 为 Hz）；
 *                  FREQ_LINEAR 用 duration_s，FREQ_TRIANGLE 在 start_s 前保持 from，
 *                  FREQ_STEP 用 start_s。
 */
typedef struct {
    uint8_t kind;
    double  from;
    double  to;
    double  period_s;
    double  frequency_hz;
    double  depth_pct;
    double  duty_pct;
    double  phase_deg;
    double  start_s;
    double  duration_s;
    double  hold;
} wg_modulator_t;

/*
 * 信号分量。amp_pct/phase_deg 按通道索引，未启用通道的项被忽略；
 * channel_mask 的 bit n 对应通道 n（WG_CH_UA=0 … WG_CH_IN=6）。
 * amplitude_ref: 0=相对通道 nominal，1=相对基波（谐波用 1，间谐波/载波用 0）。
 * modulators[] 只作用于本分量，多条叠加。
 */
typedef struct {
    uint8_t        kind;
    /* 相对基波的次数：谐波必须为正整数，间谐波可为 m+0.5（如 1.5/7.5/49.5）。 */
    double         order;
    double         frequency_hz;
    double         amp_pct[WG_MAX_CHANNELS];
    double         phase_deg[WG_MAX_CHANNELS];
    uint8_t        channel_mask;
    uint8_t        amplitude_ref;
    wg_modulator_t modulators[WG_MAX_MODULATORS];
    uint8_t        modulator_count;
} wg_component_t;

/*
 * 时间事件。time_s 为相对全局起点的秒数；duration_s 为过渡时长，
 * 0 表示立即生效。AMP_STEP 的 value 为幅值百分比，PHASE_STEP 的 value 为角度。
 */
typedef struct {
    uint8_t kind;
    uint8_t channel_mask;
    double  time_s;
    double  value;
    double  duration_s;
} wg_event_t;

/* 全局分量/事件集合。空（两个 count 均为 0）时表示未使用 program。 */
typedef struct {
    wg_component_t components[WG_MAX_COMPONENTS];
    uint16_t       component_count;
    wg_event_t     events[WG_MAX_EVENTS];
    uint16_t       event_count;
} wg_program_t;

/*
 * program 追加原语：预设层与 builder 共用，返回 0 成功、负值错误。
 * 追加前按分量/调制/事件规则做容量与参数校验，失败时不修改 program。
 */
int32_t wg_program_add_component(wg_program_t *pst_prog,
                                 const wg_component_t *pst_comp);
int32_t wg_program_add_modulator(wg_program_t *pst_prog, uint32_t u32_index,
                                 const wg_modulator_t *pst_mod);
int32_t wg_program_add_event(wg_program_t *pst_prog,
                             const wg_event_t *pst_event);

#endif /* WG_SIGNAL_H */
