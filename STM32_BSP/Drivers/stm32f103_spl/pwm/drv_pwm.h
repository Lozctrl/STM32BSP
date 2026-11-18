/**
 * @file    drv_pwm.h
 * @brief   PWM 通道工厂（基于通用定时器）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_PWM_H
#define DRV_PWM_H

#include "bsp.h"

typedef enum {
    DRV_TIM_1 = 0,
    DRV_TIM_2,
    DRV_TIM_3,
    DRV_TIM_4
} drv_tim_t;

/**
 * @brief 创建一个 PWM 通道
 * @param tim      定时器编号（本板只用 TIM1~TIM4，均挂在 72MHz）
 * @param channel  1~4
 * @param pin      输出引脚（驱动内部会把它切到复用推挽）
 * @param freq_hz  载波频率（舵机 50，电机 20000，LED 1000）
 */
bsp_pwm_t *drv_pwm_create(drv_tim_t tim, uint8_t channel, bsp_gpio_t *pin, uint32_t freq_hz);

#endif /* DRV_PWM_H */
