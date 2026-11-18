/**
 * @file    drv_config.h
 * @brief   STM32F103 + HAL 后端的配置。
 * @layer   Drivers / stm32f103_hal
 *
 * 和 SPL 版（stm32f103_spl/board/drv_config.h）的区别：
 *   多了 3 个"谁来初始化"的开关，因为 HAL 工程通常由 CubeMX 生成 main()。
 */
#ifndef DRV_CONFIG_H
#define DRV_CONFIG_H

/*
 * 下面四个频率常量只作"默认 72MHz"的文档说明用：
 * HAL 后端在运行期用 HAL_RCC_GetHCLKFreq() / GetPCLK1Freq() / GetPCLK2Freq()
 * 现算延时和 PWM 分频，所以 HSE 起不来退到 HSI 64MHz 时也不会算错。
 */
#define DRV_MCU_HCLK_HZ             72000000u
#define DRV_MCU_PCLK1_HZ            36000000u   /* APB1，定时器倍频后 72MHz */
#define DRV_MCU_TIM_CLK_HZ          72000000u
#define DRV_MCU_PCLK2_HZ            72000000u

/* --------------------------------------------------------------------------
 * 与 CubeMX 工程共存时的三个开关
 * -------------------------------------------------------------------------- */

/** 1 = 由 drv_board_init() 调用 HAL_Init()（独立使用本库时）
 *  0 = 你的 main() 已经调过 HAL_Init()，别再调一次（否则 tick 会清零） */
#define DRV_CFG_INIT_HAL            1u

/** 1 = 由 drv_board_init() 配 72MHz（HSE 失败自动退到 HSI 64MHz）
 *  0 = CubeMX 的 SystemClock_Config() 已经配过 */
#define DRV_CFG_CONFIG_CLOCK        1u

/** 1 = 由 drv_exti.c 提供 EXTI0~15 的中断入口
 *  0 = CubeMX 在 stm32f1xx_it.c 里已经生成了，避免重复定义 */
#define DRV_CFG_EXTI_OWNS_IRQ       1u

/* 静态对象池：全部用数组，禁止 malloc */
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
#define DRV_FLASH_STORE_BASE        0x0800FC00u

#define DRV_RTC_YEAR_BASE           2000u

#endif /* DRV_CONFIG_H */
