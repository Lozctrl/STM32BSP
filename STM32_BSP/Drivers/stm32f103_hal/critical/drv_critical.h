/**
 * @file    drv_critical.h
 * @brief   临界区实现（PRIMASK 保存/恢复）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_CRITICAL_H
#define DRV_CRITICAL_H

#include "bsp.h"

bsp_critical_t *drv_critical_get(void);

#endif /* DRV_CRITICAL_H */
