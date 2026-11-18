/**
 * @file    drv_exti.h
 * @brief   外部中断管理（EXTI0~15，共享中断向量分发）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_EXTI_H
#define DRV_EXTI_H

#include "bsp.h"

bsp_exti_t *drv_exti_get(void);

#endif /* DRV_EXTI_H */
