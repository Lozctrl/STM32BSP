/**
 * @file    drv_rtc.c
 * @brief   RTC（HAL 后端，LSE 32.768kHz + 备份域）。
 * @layer   Drivers / stm32f103_hal
 *
 * 和 SPL 版最大的区别：不再自己把"年月日"换算成秒。
 * F1 的 HAL_RTC 已经把 CNT 计数器封装成了 GetTime/GetDate，
 * 所以那两个历法换算函数（to_counter / from_counter）在这里是不需要的。
 */
#include "stm32f1xx_hal.h"
#include "drv_rtc.h"
#include "drv_config.h"
#include <string.h>

#define DRV_RTC_BKP_MAGIC   0xA5A5u

static RTC_HandleTypeDef s_hrtc;
static uint8_t           s_ready = 0u;

/* 我们的 weekday（0=周日…6=周六）-> HAL 的 RTC_WEEKDAY_xxx（1=周一…7=周日） */
static uint8_t drv_rtc_to_hal_weekday(uint8_t w)
{
    return (w == 0u) ? RTC_WEEKDAY_SUNDAY : w;
}

static uint8_t drv_rtc_from_hal_weekday(uint8_t w)
{
    return (w == RTC_WEEKDAY_SUNDAY) ? 0u : w;
}

/**
 * @brief  RTC 硬件初始化：开备份域 → 启 LSE → 选时钟源 → 配预分频
 * @note   用备份寄存器里的魔法字判断是否第一次上电：
 *         有魔法字说明之前配过，只要补一下初始化即可，时间不会丢。
 */
static void drv_rtc_hw_init(void)
{
    RCC_OscInitTypeDef       osc;
    RCC_PeriphCLKInitTypeDef pclk;
    uint8_t                  fresh;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    fresh = (uint8_t)(HAL_RTCEx_BKUPRead(&s_hrtc, RTC_BKP_DR1) != DRV_RTC_BKP_MAGIC);

    /* 结构体必须先清零：HAL 会读 PLL/DayLightSaving 这些本次用不到的字段，
       栈上的随机值可能被当成有效配置 */
    (void)memset(&osc, 0, sizeof(osc));
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    (void)HAL_RCC_OscConfig(&osc);

    (void)memset(&pclk, 0, sizeof(pclk));
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    (void)HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_RTC_ENABLE();

    s_hrtc.Instance          = RTC;
    s_hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;   /* 自动配成 32767，正好 1Hz */
    s_hrtc.Init.OutPut       = RTC_OUTPUTSOURCE_NONE;

    if (HAL_RTC_Init(&s_hrtc) != HAL_OK) {
        return;
    }

    if (fresh != 0u) {
        HAL_RTCEx_BKUPWrite(&s_hrtc, RTC_BKP_DR1, DRV_RTC_BKP_MAGIC);
    }
}

static bsp_status_t drv_rtc_op_get(bsp_rtc_t *self, bsp_rtc_time_t *out)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    BSP_UNUSED(self);

    if (out == NULL) {
        return BSP_ERR_PARAM;
    }
    (void)memset(&t, 0, sizeof(t));
    (void)memset(&d, 0, sizeof(d));

    /* 顺序不能反：F1 必须先读时间，再读日期，否则影子寄存器不会解锁 */
    if (HAL_RTC_GetTime(&s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        return BSP_ERR;
    }
    if (HAL_RTC_GetDate(&s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        return BSP_ERR;
    }

    out->year    = (uint16_t)(DRV_RTC_YEAR_BASE + d.Year);
    out->month   = d.Month;
    out->day     = d.Date;
    out->hour    = t.Hours;
    out->minute  = t.Minutes;
    out->second  = t.Seconds;
    out->weekday = drv_rtc_from_hal_weekday(d.WeekDay);

    return BSP_OK;
}

static bsp_status_t drv_rtc_op_set(bsp_rtc_t *self, const bsp_rtc_time_t *in)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    BSP_UNUSED(self);

    if (in == NULL) {
        return BSP_ERR_PARAM;
    }
    if (in->year < DRV_RTC_YEAR_BASE) {
        return BSP_ERR_PARAM;
    }
    (void)memset(&t, 0, sizeof(t));
    (void)memset(&d, 0, sizeof(d));

    /* F1 的 RTC 只是个秒计数器，时间结构体里没有 12/24 小时制字段 */
    t.Hours      = in->hour;
    t.Minutes    = in->minute;
    t.Seconds    = in->second;

    d.Year    = (uint8_t)(in->year - DRV_RTC_YEAR_BASE);
    d.Month   = in->month;
    d.Date    = in->day;
    d.WeekDay = drv_rtc_to_hal_weekday(in->weekday);

    if (HAL_RTC_SetTime(&s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        return BSP_ERR;
    }
    if (HAL_RTC_SetDate(&s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        return BSP_ERR;
    }
    return BSP_OK;
}

static uint8_t drv_rtc_op_is_reset(bsp_rtc_t *self)
{
    BSP_UNUSED(self);
    return (uint8_t)(HAL_RTCEx_BKUPRead(&s_hrtc, RTC_BKP_DR1) != DRV_RTC_BKP_MAGIC);
}

static const bsp_rtc_ops_t drv_rtc_ops = { drv_rtc_op_get, drv_rtc_op_set, drv_rtc_op_is_reset };
static bsp_rtc_t s_rtc = { &drv_rtc_ops, NULL };

bsp_rtc_t *drv_rtc_get(void)
{
    if (s_ready == 0u) {
        drv_rtc_hw_init();
        s_ready = 1u;
    }
    return &s_rtc;
}
