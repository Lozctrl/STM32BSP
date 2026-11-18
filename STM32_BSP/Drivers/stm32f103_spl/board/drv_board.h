/**
 * @file    drv_board.h
 * @brief   板级初始化入口。上电后第一件事就是调用它。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_BOARD_H
#define DRV_BOARD_H

#include "bsp.h"

void drv_board_init(void);

#endif /* DRV_BOARD_H */
