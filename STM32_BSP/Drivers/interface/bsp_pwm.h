/**
 * @file    bsp_pwm.h
 * @brief   PWM 输出契约。占空比用 Q16 定点（0~65535 表示 0~100%），
 *          避免不同应用需要不同分辨率时反复改接口。
 * @layer   Drivers / Interface
 *
 * 为什么不用"毫秒/微秒脉宽"？因为舵机要 us、电机要千分比、LED 要百分比，
 * Q16 是三者共同的超集，且换算只需一次移位。
 */
#ifndef BSP_PWM_H
#define BSP_PWM_H

#include "bsp_types.h"

typedef struct bsp_pwm bsp_pwm_t;

typedef struct {
    /** duty_q16: 0=0%, 65535=100% */
    bsp_status_t (*set_duty_q16)(bsp_pwm_t *self, uint16_t duty_q16);
    /** 使能/失能输出（部分平台可空实现） */
    void         (*enable)(bsp_pwm_t *self, uint8_t on);
    /** 当前通道周期（微秒），舵机换算用 */
    bsp_time_us_t (*period_us)(bsp_pwm_t *self);
} bsp_pwm_ops_t;

struct bsp_pwm {
    const bsp_pwm_ops_t *ops;
    void                *hw;
};

/**
 * @brief  设置占空比（Q16 定点：0 = 0%，65535 ≈ 100%）
 * @param  self      PWM 通道句柄
 * @param  duty_q16  0~65535
 * @retval BSP_OK
 * @note   为什么不用百分比？因为舵机要"微秒级脉宽"、电机要"千分比"、
 *         LED 要"百分比"，三种精度要求不一样。Q16 是它们的共同超集，
 *         换算只在调用处做一次移位，精度不丢。
 */
BSP_INLINE bsp_status_t bsp_pwm_set_duty_q16(bsp_pwm_t *self, uint16_t duty_q16)
{
    return self->ops->set_duty_q16(self, duty_q16);
}

/** @brief 千分比（0~1000）便捷接口 */

/**
 * @brief  设置占空比（千分比 0~1000），给电机用最顺手
 * @param  self      PWM 通道句柄
 * @param  permille  0~1000（超过 1000 会被截到 1000）
 * @retval BSP_OK
 * @note   内部会做 permille -> Q16 的换算，不需要调用方自己算
 */
BSP_INLINE bsp_status_t bsp_pwm_set_duty_permille(bsp_pwm_t *self, uint16_t permille)
{
    uint32_t q16 = ((uint32_t)permille * 65535u) / 1000u;
    return self->ops->set_duty_q16(self, (uint16_t)BSP_MIN(q16, 65535u));
}

/**
 * @brief  开/关这个 PWM 通道的输出
 * @param  self PWM 通道句柄
 * @param  on   1=开，0=关
 * @note   有的平台没有"单独关一个通道"的能力，此时这个函数是空实现，不影响调用
 */
BSP_INLINE void bsp_pwm_enable(bsp_pwm_t *self, uint8_t on)
{
    if (self->ops->enable != NULL) {
        self->ops->enable(self, on);
    }
}

/**
 * @brief  查询这个通道的载波周期（微秒）
 * @param  self PWM 通道句柄
 * @retval 周期，例如 50Hz 舵机返回 20000；平台不支持时返回 0
 * @note   舵机组件靠它把"脉宽 us"换算成"占空比 Q16"，
 *         所以舵机用的 PWM 通道必须能报告周期。
 */
BSP_INLINE bsp_time_us_t bsp_pwm_period_us(bsp_pwm_t *self)
{
    return (self->ops->period_us != NULL) ? self->ops->period_us(self) : 0u;
}

#endif /* BSP_PWM_H */
