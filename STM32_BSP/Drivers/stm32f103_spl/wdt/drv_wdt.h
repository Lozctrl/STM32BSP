/**
 * @file    drv_wdt.h
 * @brief   独立看门狗（IWDG）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_WDT_H
#define DRV_WDT_H

#include "bsp.h"

bsp_wdt_t *drv_wdt_get(void);

#endif /* DRV_WDT_H */
