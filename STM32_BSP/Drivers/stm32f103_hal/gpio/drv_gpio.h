/**
 * @file    drv_gpio.h
 * @brief   STM32F103 GPIO 工厂：从静态池中"认领"一个引脚并返回 bsp_gpio_t*。
 * @layer   Drivers / stm32f103
 *
 * 上层永远写 `drv_gpio_claim('A', 1)` 这种板级描述，而不会出现 GPIOA/GPIO_Pin_1。
 * 换到 GD32/STM32F4/ESP32 时，本文件的实现整体替换，调用处不变。
 */
#ifndef DRV_GPIO_H
#define DRV_GPIO_H

#include "bsp.h"

/* 供同包内 drv_exti.c 使用：把 bsp_gpio_t 还原成"端口+位"描述 */
typedef struct drv_gpio_hw {
    void    *port;          /* 实际为 GPIO_TypeDef*，此处用 void* 以免暴露厂商类型 */
    uint16_t pin_mask;
    uint8_t  port_index;    /* 0=A, 1=B, ... 6=G */
} drv_gpio_hw_t;

/** @param port 'A'..'G'；@param pin 0..15；失败返回 NULL（池满/参数错） */
bsp_gpio_t *drv_gpio_claim(char port, uint8_t pin);
uint8_t     drv_gpio_used(void);

#endif /* DRV_GPIO_H */
