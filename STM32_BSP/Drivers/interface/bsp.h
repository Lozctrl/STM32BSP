/**
 * @file    bsp.h
 * @brief   契约层总入口。Components 只需包含本文件即可见到全部硬件抽象接口。
 * @layer   Drivers / Interface
 *
 * ⚠ 铁律：Components / Middlewares / Applications 禁止包含以下任何内容 —
 *        stm32f10x.h、core_cm3.h、drv_xxx.h、HAL_xxx.h、GPIOA、TIM2、USART1
 *   由 Tools/check_layering.ps1 自动校验。
 */
#ifndef BSP_H
#define BSP_H

#include "bsp_types.h"
#include "bsp_gpio.h"
#include "bsp_time.h"
#include "bsp_critical.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_pwm.h"
#include "bsp_uart.h"
#include "bsp_adc.h"
#include "bsp_encoder.h"
#include "bsp_flash.h"
#include "bsp_rtc.h"
#include "bsp_wdt.h"
#include "bsp_irq.h"

#endif /* BSP_H */
