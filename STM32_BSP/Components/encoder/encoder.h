/**
 * @file    encoder.h
 * @brief   编码器组件：增量读数 → 转速/线速度，自带一阶低通。
 * @layer   Components
 */
#ifndef ENCODER_H
#define ENCODER_H

#include "bsp.h"
#include "filter.h"

typedef enum {
    ENCODER_UNIT_COUNTS_PER_S = 0,  /**< 计数/秒 */
    ENCODER_UNIT_RPM,               /**< 转/分（考虑减速比） */
    ENCODER_UNIT_LINEAR_MM_S        /**< 线速度，需设置轮径 */
} encoder_unit_t;

typedef struct {
    bsp_encoder_t *hw;              /**< 注入的编码器接口 */
    uint16_t       counts_per_rev;  /**< 电机轴一圈的计数（4 倍频后） */
    float          gear_ratio;      /**< 电机轴:输出轴，例如 30.0 */
    float          wheel_diameter_mm;
    encoder_unit_t unit;

    int32_t        total;           /**< 累计计数 */
    int16_t        delta;           /**< 本周期增量 */
    float          speed;           /**< 已滤波速度（unit 对应单位） */
    flt_lpf_t      lpf;
} encoder_t;

void  encoder_init(encoder_t *e, bsp_encoder_t *hw,
                   uint16_t counts_per_rev, float gear_ratio);
void  encoder_set_wheel(encoder_t *e, float diameter_mm);
void  encoder_set_unit(encoder_t *e, encoder_unit_t unit);
void  encoder_set_lpf(encoder_t *e, float alpha);
/** @brief 周期调用（dt_s 为距上次调用的秒数，必须 > 0） */
void  encoder_update(encoder_t *e, float dt_s);
/** @brief 清零累计值与滤波器（现场标定用） */
void  encoder_reset(encoder_t *e);
float encoder_speed(const encoder_t *e);
int32_t encoder_total(const encoder_t *e);

#endif /* ENCODER_H */
