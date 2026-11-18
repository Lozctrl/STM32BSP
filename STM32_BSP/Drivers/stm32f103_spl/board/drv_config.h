/**
 * @file    drv_config.h
 * @brief   STM32F103 驱动层配置。本文件不含厂商头文件，宿主机仿真可直接复用。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_CONFIG_H
#define DRV_CONFIG_H

#define DRV_MCU_HCLK_HZ             72000000u
#define DRV_MCU_PCLK1_HZ            36000000u   /* APB1，定时器倍频后 72MHz */
#define DRV_MCU_TIM_CLK_HZ          72000000u
#define DRV_MCU_PCLK2_HZ            72000000u

/* 静态对象池：全部用数组，禁止 malloc，便于静态分析确定 RAM 峰值 */
#define DRV_GPIO_POOL_SIZE          24u
#define DRV_I2C_POOL_SIZE           3u
#define DRV_SPI_POOL_SIZE           3u
#define DRV_PWM_POOL_SIZE           6u
#define DRV_UART_POOL_SIZE          3u
#define DRV_ADC_POOL_SIZE           2u
#define DRV_ENCODER_POOL_SIZE       2u

#define DRV_UART_RX_BUF_SIZE        128u    /* 必须是 2 的幂 */
#define DRV_UART_TX_TIMEOUT_MS      20u

#define DRV_FLASH_PAGE_SIZE         1024u   /* F103C8：1KB/页 */
#define DRV_FLASH_STORE_BASE        0x0800FC00u /* 最后一页专门存参数 */

#define DRV_RTC_YEAR_BASE           2000u

#endif /* DRV_CONFIG_H */
