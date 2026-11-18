/**
 * @file    drv_uart.h
 * @brief   串口工厂：ISR 只负责把字节塞进环形缓冲，业务在 uart_link 组件里解析。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_UART_H
#define DRV_UART_H

#include "bsp.h"

/** @brief instance = 1..3（USART1/2/3） */
bsp_uart_t *drv_uart_create(uint8_t instance, uint32_t baud);
/** @brief 各实例的统计（溢出次数等），调参/排障用 */
uint32_t    drv_uart_overflow_count(uint8_t instance);

#endif /* DRV_UART_H */
