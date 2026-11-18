/**
 * @file    drv_wdt.c
 * @brief   独立看门狗 IWDG（HAL 后端）。
 * @layer   Drivers / stm32f103_hal
 */
#include "stm32f1xx_hal.h"
#include "drv_wdt.h"
#include "drv_config.h"

#define DRV_IWDG_LSI_HZ     40000u
#define DRV_IWDG_PRESCALER  64u
#define DRV_IWDG_RELOAD_MAX 4095u

static IWDG_HandleTypeDef s_hiwdg;
static uint8_t            s_started = 0u;

/**
 * @brief  启动看门狗
 * @param  self       句柄
 * @param  timeout_ms 超时时间
 * @retval BSP_OK / BSP_ERR
 * @note   LSI 是内部 RC，精度约 ±50%，所以超时时间别卡太死。
 *         重装值上限 4095（64 分频下最长约 6.5 秒）。
 */
static bsp_status_t drv_wdt_start(bsp_wdt_t *self, bsp_time_ms_t timeout_ms)
{
    uint32_t reload;

    BSP_UNUSED(self);

    if (timeout_ms < 10u) {
        timeout_ms = 10u;
    }
    reload = (timeout_ms * (DRV_IWDG_LSI_HZ / DRV_IWDG_PRESCALER)) / 1000u;
    if (reload > DRV_IWDG_RELOAD_MAX) {
        reload = DRV_IWDG_RELOAD_MAX;
    }
    if (reload == 0u) {
        reload = 1u;
    }

    s_hiwdg.Instance          = IWDG;
    s_hiwdg.Init.Prescaler    = IWDG_PRESCALER_64;
    s_hiwdg.Init.Reload       = reload;

    if (HAL_IWDG_Init(&s_hiwdg) != HAL_OK) {
        return BSP_ERR;
    }
    s_started = 1u;
    return BSP_OK;
}

static void drv_wdt_feed(bsp_wdt_t *self)
{
    BSP_UNUSED(self);
    if (s_started != 0u) {
        (void)HAL_IWDG_Refresh(&s_hiwdg);
    }
}

static const bsp_wdt_ops_t drv_wdt_ops = { drv_wdt_start, drv_wdt_feed };
static bsp_wdt_t s_wdt = { &drv_wdt_ops, NULL };

bsp_wdt_t *drv_wdt_get(void)
{
    return &s_wdt;
}
