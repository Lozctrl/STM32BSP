/**
 * @file    drv_flash.h
 * @brief   内部 Flash 存储（参数保存区）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_FLASH_H
#define DRV_FLASH_H

#include "bsp.h"

bsp_flash_t *drv_flash_get(void);

#endif /* DRV_FLASH_H */
