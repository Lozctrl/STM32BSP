/**
 * @file    encoder.c
 * @brief   编码器组件实现。
 * @layer   Components
 */
#include "encoder.h"

#define ENCODER_PI  3.14159265f

/**
 * @brief  初始化编码器测速
 * @param  e              对象
 * @param  hw             编码器接口（注入，硬件计数或软件解码都行）
 * @param  counts_per_rev 电机轴转一圈的计数（编码器线数 × 4，四倍频）
 * @param  gear_ratio     减速比（电机轴 : 输出轴，比如 30.0）
 * @note   这些参数只影响"换算成转速/线速度"，不影响原始计数。
 */
void encoder_init(encoder_t *e, bsp_encoder_t *hw,
                  uint16_t counts_per_rev, float gear_ratio)
{
    if ((e == NULL) || (hw == NULL) || (counts_per_rev == 0u) || (gear_ratio <= 0.0f)) {
        return;
    }

    e->hw = hw;
    e->counts_per_rev = counts_per_rev;
    e->gear_ratio = gear_ratio;
    e->wheel_diameter_mm = 65.0f;
    e->unit = ENCODER_UNIT_COUNTS_PER_S;
    e->total = 0;
    e->delta = 0;
    e->speed = 0.0f;
    flt_lpf_init_alpha(&e->lpf, 0.35f);
}

/**
 * @brief  设置轮子直径（换算线速度时用）
 * @param  e           对象
 * @param  diameter_mm 直径，单位毫米（常见 65）
 */
void encoder_set_wheel(encoder_t *e, float diameter_mm)
{
    if ((e != NULL) && (diameter_mm > 0.0f)) {
        e->wheel_diameter_mm = diameter_mm;
    }
}

/**
 * @brief  选择 speed 的单位：原始计数/秒、转速 RPM、还是线速度 mm/s
 * @param  e    对象
 * @param  unit 单位枚举
 */
void encoder_set_unit(encoder_t *e, encoder_unit_t unit)
{
    if (e != NULL) {
        e->unit = unit;
    }
}

/**
 * @brief  设置速度滤波系数
 * @param  e     对象
 * @param  alpha 0~1，越大越跟手但越抖；0.3~0.5 比较常用
 * @note   编码器测速天生有量化噪声（10ms 内可能只有十几个计数），
 *         不滤波的速度值直接喂 PID 会让电机"嗡嗡"抖。
 */
void encoder_set_lpf(encoder_t *e, float alpha)
{
    if (e != NULL) {
        e->lpf.alpha = BSP_CLAMP(alpha, 0.0f, 1.0f);
        e->lpf.primed = 0u;
    }
}

/**
 * @brief  读一次增量、更新速度，必须周期性调用
 * @param  e    对象
 * @param  dt_s 距上次调用的时间（秒），必须 > 0
 * @note   dt_s 一定要传真实值。传错（比如固定 0.01 但实际 0.03）会让
 *         算出来的速度整体偏差 3 倍。
 */
void encoder_update(encoder_t *e, float dt_s)
{
    float counts_per_s;
    float rev_per_s;
    float raw;

    if ((e == NULL) || (e->hw == NULL) || (dt_s <= 0.0f)) {
        return;
    }

    e->delta = bsp_encoder_read_delta(e->hw);
    e->total += (int32_t)e->delta;

    counts_per_s = (float)e->delta / dt_s;

    switch (e->unit) {
    case ENCODER_UNIT_RPM:
        rev_per_s = counts_per_s / ((float)e->counts_per_rev * e->gear_ratio);
        raw = rev_per_s * 60.0f;
        break;

    case ENCODER_UNIT_LINEAR_MM_S:
        rev_per_s = counts_per_s / ((float)e->counts_per_rev * e->gear_ratio);
        raw = rev_per_s * ENCODER_PI * e->wheel_diameter_mm;
        break;

    case ENCODER_UNIT_COUNTS_PER_S:
    default:
        raw = counts_per_s;
        break;
    }

    e->speed = flt_lpf_update(&e->lpf, raw);
}

/**
 * @brief  清零累计值、增量、滤波状态
 * @param  e 对象
 */
void encoder_reset(encoder_t *e)
{
    if ((e == NULL) || (e->hw == NULL)) {
        return;
    }
    bsp_encoder_reset(e->hw);
    e->total = 0;
    e->delta = 0;
    e->speed = 0.0f;
    e->lpf.primed = 0u;
    e->lpf.y = 0.0f;
}

/**
 * @brief  读最近一次算出来的速度（已滤波）
 * @param  e 对象
 * @retval 速度，单位见 encoder_set_unit
 */
float encoder_speed(const encoder_t *e)
{
    return (e != NULL) ? e->speed : 0.0f;
}

/**
 * @brief  读累计走过的计数（软件累加，不会像硬件计数器那样回绕）
 * @param  e 对象
 * @retval 累计计数
 */
int32_t encoder_total(const encoder_t *e)
{
    return (e != NULL) ? e->total : 0;
}
