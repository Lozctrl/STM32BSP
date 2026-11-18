/**
 * @file    drv_time.h
 * @brief   时间基准：SysTick 1ms 计数 + DWT 微秒延时。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_TIME_H
#define DRV_TIME_H

#include "bsp.h"

void        drv_time_init(void);
bsp_time_t *drv_time_get(void);
/** @brief 由 SysTick_Handler 调用，1ms 一次 */
void        drv_time_tick_isr(void);

#endif /* DRV_TIME_H */
