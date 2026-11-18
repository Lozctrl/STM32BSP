/**
 * @file    drv_time.c
 * @brief   SysTick 毫秒节拍 + DWT 周期计数器微秒延时。
 * @layer   Drivers / stm32f103
 *
 * 与教程 Delay_us() 的区别：不再改写 SysTick 的 LOAD/VAL/CTRL，
 * 因此不会和毫秒节拍（乃至将来接入的 RTOS）互相踩踏。
 *
 * 注意：本工程自带的是 CMSIS 1.30，其中并未定义 DWT/CoreDebug 结构体，
 *       因此这里直接按内核手册的固定地址访问 DWT，避免升级 CMSIS 的耦合。
 */
#include "stm32f10x.h"
#include "drv_time.h"
#include "drv_config.h"

/* Cortex-M3 调试组件地址（ARMv7-M 架构固定） */
#define DRV_DEMCR               (*(volatile uint32_t *)0xE000EDFCu)
#define DRV_DWT_CTRL            (*(volatile uint32_t *)0xE0001000u)
#define DRV_DWT_CYCCNT          (*(volatile uint32_t *)0xE0001004u)

#define DRV_DEMCR_TRCENA        (1u << 24)
#define DRV_DWT_CTRL_CYCCNTENA  (1u << 0)

static volatile uint32_t s_ms = 0u;
static uint32_t          s_cycles_per_us = DRV_MCU_HCLK_HZ / 1000000u;
static uint8_t           s_dwt_ok = 0u;

/* 取毫秒计数。32 位对齐读，单核上天然原子，不用关中断 */
static bsp_time_ms_t drv_time_get_ms(bsp_time_t *self)
{
    BSP_UNUSED(self);
    return s_ms;
}

/** @brief 无 DWT 时的兜底：按"每次循环约 3 周期"估算，仅用于初始化时序 */

/* 兜底方案：DWT 不可用时（极少数内核）用空循环估算。
   精度差，只够跑 I2C 这种"慢一点没关系、快一点不行"的时序 */
static void drv_time_delay_us_slow(uint32_t us)
{
    volatile uint32_t i;
    uint32_t loops = us * (DRV_MCU_HCLK_HZ / 3000000u);
    for (i = 0u; i < loops; i++) {
    }
}

/**
 * @brief  微秒级延时（DWT 硬件周期计数器）
 * @param  self 句柄
 * @param  us   微秒数
 * @note   为什么不改 SysTick 的 LOAD/VAL？因为 SysTick 现在是 1ms 系统节拍，
 *         借它做微秒延时会把节拍打乱（原来的 Delay_us 就有这个问题）。
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
 * @brief  初始化时间基准：开 DWT、配 SysTick 1ms
 * @note   顺序很重要：先让 DWT 自检（读两次看会不会涨），再配 SysTick。
 *         SysTick 优先级设成最低，保证它不会打断控制类中断。
 */
void drv_time_init(void)
{
    uint32_t probe;

    /* DWT 周期计数器：用于微秒级总线时序 */
    DRV_DEMCR |= DRV_DEMCR_TRCENA;
    DRV_DWT_CYCCNT = 0u;
    DRV_DWT_CTRL |= DRV_DWT_CTRL_CYCCNTENA;

    probe = DRV_DWT_CYCCNT;
    drv_time_delay_us_slow(2u);
    s_dwt_ok = (uint8_t)(DRV_DWT_CYCCNT != probe);

    /* SysTick 1ms：72MHz / 1000 = 72000 */
    (void)SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 0x0Fu);  /* 最低优先级，不打断控制中断 */
    s_ms = 0u;
}

/**
 * @brief  1ms 节拍（由 SysTick_Handler 调用）
 * @note   只做一次自增，不调用任何其他函数。
 */
void drv_time_tick_isr(void)
{
    s_ms++;
}

/**
 * @brief  拿到时间句柄（全局唯一，但通过接口暴露，上层不依赖它的实现）
 * @retval 时间句柄
 */
bsp_time_t *drv_time_get(void)
{
    return &s_time;
}

/* SysTick 入口放在驱动自己身上，而不是 stm32f10x_it.c */

/* SysTick 中断入口放在驱动自己身上，而不是 stm32f10x_it.c。
   好处：谁产生中断谁负责处理，中断文件里不会藏着一堆业务 */
void SysTick_Handler(void)
{
    drv_time_tick_isr();
}
