/**
 * @file    drv_time.c
 * @brief   时间基准（HAL 后端）：毫秒用 HAL_GetTick，微秒用 DWT。
 * @layer   Drivers / stm32f103_hal
 *
 * ⚠ 和 SPL 版最大的区别：**这里没有 SysTick_Handler**。
 *   HAL_Init() 已经把 SysTick 配成 1ms 并接到 HAL_IncTick()，
 *   再写一个 SysTick_Handler 会和 CubeMX 生成的 stm32f1xx_it.c 重复定义。
 *   所以毫秒计数直接取 HAL_GetTick()，我们只负责微秒延时。
 */
#include "stm32f1xx_hal.h"
#include "drv_time.h"
#include "drv_config.h"

/* Cortex-M3 调试组件地址（ARMv7-M 架构固定，不依赖 CMSIS 版本） */
#define DRV_DEMCR               (*(volatile uint32_t *)0xE000EDFCu)
#define DRV_DWT_CTRL            (*(volatile uint32_t *)0xE0001000u)
#define DRV_DWT_CYCCNT          (*(volatile uint32_t *)0xE0001004u)

#define DRV_DEMCR_TRCENA        (1u << 24)
#define DRV_DWT_CTRL_CYCCNTENA  (1u << 0)

static uint32_t s_cycles_per_us = 1u;   /* 由 drv_time_init 按实际 HCLK 填 */
static uint8_t  s_dwt_ok = 0u;

/**
 * @brief  取毫秒计数
 * @param  self 句柄
 * @retval HAL 的 1ms 节拍（HAL_Init 之后自动开始走）
 * @note   直接借 HAL 的节拍，不再自己数，避免两套时钟打架。
 */
static bsp_time_ms_t drv_time_get_ms(bsp_time_t *self)
{
    BSP_UNUSED(self);
    return HAL_GetTick();
}

/* DWT 不可用时的兜底：按"每次循环约 3 周期"估算，只够跑 I2C 时序 */
static void drv_time_delay_us_slow(uint32_t us)
{
    volatile uint32_t i;
    uint32_t per_us = (s_cycles_per_us > 3u) ? (s_cycles_per_us / 3u) : 1u;
    uint32_t loops = us * per_us;
    for (i = 0u; i < loops; i++) {
    }
}

/**
 * @brief  微秒级延时（DWT 硬件周期计数器）
 * @param  self 句柄
 * @param  us   微秒数
 * @note   用 DWT 而不是 SysTick：SysTick 现在是 HAL 的毫秒节拍，不能借来做微秒。
 */
static void drv_time_delay_us(bsp_time_t *self, bsp_time_us_t us)
{
    uint32_t start;
    uint32_t ticks;

    BSP_UNUSED(self);

    if (us == 0u) {
        return;
    }
    if (s_dwt_ok == 0u) {
        drv_time_delay_us_slow((uint32_t)us);
        return;
    }

    ticks = (uint32_t)us * s_cycles_per_us;
    start = DRV_DWT_CYCCNT;
    while ((DRV_DWT_CYCCNT - start) < ticks) {
        /* 纯硬件计数，不依赖中断 */
    }
}

static const bsp_time_ops_t drv_time_ops = {
    drv_time_get_ms,
    drv_time_delay_us
};

static bsp_time_t s_time = { &drv_time_ops, NULL };

/**
 * @brief  初始化 DWT 微秒延时
 * @note   只做 DWT，不碰 SysTick（那是 HAL_Init 的事）。
 *         顺序上必须在 drv_board_init() 配完时钟之后调用，
 *         这里会按 HAL_RCC_GetHCLKFreq() 的实际值换算每微秒周期数。
 */
void drv_time_init(void)
{
    uint32_t probe;

    /* 按实际 HCLK 算，不用编译期常量：HSE 起不来退到 HSI 64MHz 时也不会偏 */
    s_cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000u;
    if (s_cycles_per_us == 0u) {
        s_cycles_per_us = 1u;
    }

    DRV_DEMCR |= DRV_DEMCR_TRCENA;
    DRV_DWT_CYCCNT = 0u;
    DRV_DWT_CTRL |= DRV_DWT_CTRL_CYCCNTENA;

    probe = DRV_DWT_CYCCNT;
    drv_time_delay_us_slow(2u);
    s_dwt_ok = (uint8_t)(DRV_DWT_CYCCNT != probe);
}

void drv_time_tick_isr(void)
{
    /* HAL 后端里节拍由 HAL_IncTick 维护，本函数留空仅为保持接口一致 */
}

bsp_time_t *drv_time_get(void)
{
    return &s_time;
}
