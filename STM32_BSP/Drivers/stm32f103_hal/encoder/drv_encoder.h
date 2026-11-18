/**
 * @file    drv_encoder.h
 * @brief   硬件正交编码器接口（定时器编码器模式）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_ENCODER_H
#define DRV_ENCODER_H

#include "bsp.h"
#include "drv_pwm.h"    /* 复用 drv_tim_t */

/** @brief 仅 TIM3/TIM4 支持编码器模式（CH1=A，CH2=B） */
bsp_encoder_t *drv_encoder_create(drv_tim_t tim, bsp_gpio_t *cha, bsp_gpio_t *chb);

#endif /* DRV_ENCODER_H */
