/**
 * @file    drv_spi.h
 * @brief   SPI 主设备工厂（软件 / 硬件可选）。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_SPI_H
#define DRV_SPI_H

#include "bsp.h"

bsp_spi_t *drv_spi_soft_create(bsp_gpio_t *sck, bsp_gpio_t *mosi,
                               bsp_gpio_t *miso, uint32_t speed_hz);
/** @brief instance = 1 或 2 */
bsp_spi_t *drv_spi_hw_create(uint8_t instance);

#endif /* DRV_SPI_H */
