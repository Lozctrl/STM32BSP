/**
 * @file    drv_wdt.c
 * @brief   独立看门狗 IWDG 实现（LSI 约 40kHz）。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_wdt.h"
#include "drv_config.h"

#define DRV_IWDG_LSI_HZ     40000u
#define DRV_IWDG_PRESCALER  64u
#define DRV_IWDG_RELOAD_MAX 4095u

/**
 * @brief  启动独立看门狗
 * @param  self       句柄
 * @param  timeout_ms 超时时间
 * @retval BSP_OK
 * @note   IWDG 用的是内部 40kHz LSI，精度一般（±50%），所以超时时间别卡太死。
 *         重装值上限 4095，对应最长约 6.5 秒（64 分频）。
 */
static bsp_status_t drv_wdt_start(bsp_wdt_t *self, bsp_time_ms_t timeout_ms)
{
    uint32_t reload;

    BSP_UNUSED(self);

    if (timeout_ms < 10u) {
        timeout_ms = 10u;
    }
    /* 计数频率 = LSI/64 = 625Hz => 1ms 需要 0.625 个计数 */
    reload = (timeout_ms * (DRV_IWDG_LSI_HZ / DRV_IWDG_PRESCALER)) / 1000u;
    if (reload > DRV_IWDG_RELOAD_MAX) {
        reload = DRV_IWDG_RELOAD_MAX;
    }
    if (reload == 0u) {
        reload = 1u;
    }

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload((uint16_t)reload);
    IWDG_ReloadCounter();
    IWDG_Enable();
    return BSP_OK;
}

/* 喂狗：把计数器重装。喂得太频繁也没意义，关键是"该喂的时候才喂" */
static void drv_wdt_feed(bsp_wdt_t *self)
{
    BSP_UNUSED(self);
    IWDG_ReloadCounter();
}

static const bsp_wdt_ops_t drv_wdt_ops = { drv_wdt_start, drv_wdt_feed };
static bsp_wdt_t s_wdt = { &drv_wdt_ops, NULL };

/**
 * @brief  拿到看门狗句柄
 * @retval 句柄
 */
bsp_wdt_t *drv_wdt_get(void)
{
    return &s_wdt;
}
