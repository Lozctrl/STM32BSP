/**
 * @file    drv_rtc.h
 * @brief   RTC（LSE 32.768kHz + 备份域）驱动。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_RTC_H
#define DRV_RTC_H

#include "bsp.h"

/** @brief 首次上电会自动写入默认时间 2026-01-01 00:00:00 */
bsp_rtc_t *drv_rtc_get(void);

#endif /* DRV_RTC_H */
