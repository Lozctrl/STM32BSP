/**
 * @file    drv_i2c.h
 * @brief   I2C 总线工厂：同一块板上可以同时存在软件 I2C 与硬件 I2C，
 *          上层拿到的都是 bsp_i2c_t*，行为一致。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_I2C_H
#define DRV_I2C_H

#include "bsp.h"

/** @brief GPIO 模拟 I2C（开漏）。speed_hz 常见 100000 / 400000 */
bsp_i2c_t *drv_i2c_soft_create(bsp_gpio_t *scl, bsp_gpio_t *sda, uint32_t speed_hz);

/** @brief 硬件 I2C 外设，instance = 1 或 2 */
bsp_i2c_t *drv_i2c_hw_create(uint8_t instance);

#endif /* DRV_I2C_H */
