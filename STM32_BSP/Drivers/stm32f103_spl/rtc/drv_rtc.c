/**
 * @file    drv_rtc.c
 * @brief   RTC（LSE 32.768kHz）实现，含历法换算。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_rtc.h"
#include "drv_config.h"

#define DRV_RTC_BKP_MAGIC   0xA5A5u
#define DRV_RTC_SEC_PER_DAY 86400u

/* 闰年判断：能被 4 整除但不能被 100 整除，或者能被 400 整除 */
static uint8_t drv_rtc_is_leap(uint16_t y)
{
    return (uint8_t)(((y % 4u) == 0u && ((y % 100u) != 0u)) || ((y % 400u) == 0u));
}

/* 某年某月有多少天（2 月要看闰年） */
static uint8_t drv_rtc_days_in_month(uint16_t y, uint8_t m)
{
    static const uint8_t tbl[12] = { 31u, 28u, 31u, 30u, 31u, 30u,
                                     31u, 31u, 30u, 31u, 30u, 31u };
    if ((m < 1u) || (m > 12u)) {
        return 30u;
    }
    if ((m == 2u) && (drv_rtc_is_leap(y) != 0u)) {
        return 29u;
    }
    return tbl[m - 1u];
}

/**
 * @brief  日期时间 -> 从 2000-01-01 起的秒数
 * @param  t 时间结构体
 * @retval 秒数
 * @note   RTC 硬件只会数秒，所以"年月日"这套东西必须由软件换算。
 */
static uint32_t drv_rtc_to_counter(const bsp_rtc_time_t *t)
{
    uint32_t days = 0u;
    uint16_t y;
    uint8_t  m;

    if (t->year < DRV_RTC_YEAR_BASE) {
        return 0u;
    }
    for (y = DRV_RTC_YEAR_BASE; y < t->year; y++) {
        days += (uint32_t)(365u + drv_rtc_is_leap(y));
    }
    for (m = 1u; m < t->month; m++) {
        days += drv_rtc_days_in_month(t->year, m);
    }
    days += (uint32_t)((t->day > 0u) ? (t->day - 1u) : 0u);

    return (days * DRV_RTC_SEC_PER_DAY)
         + ((uint32_t)t->hour * 3600u)
         + ((uint32_t)t->minute * 60u)
         + (uint32_t)t->second;
}

/**
 * @brief  秒数 -> 日期时间
 * @param  counter 秒数
 * @param  t       输出时间
 * @note   反向换算：先减掉整年，再减整月，剩下的就是日时分秒。
 *         星期几用"2000-01-01 是周六"这个已知点推算。
 */
static void drv_rtc_from_counter(uint32_t counter, bsp_rtc_time_t *t)
{
    uint32_t days = counter / DRV_RTC_SEC_PER_DAY;
    uint32_t rem  = counter % DRV_RTC_SEC_PER_DAY;
    uint16_t y = DRV_RTC_YEAR_BASE;
    uint8_t  m = 1u;

    while (days >= (uint32_t)(365u + drv_rtc_is_leap(y))) {
        days -= (uint32_t)(365u + drv_rtc_is_leap(y));
        y++;
    }
    while (days >= drv_rtc_days_in_month(y, m)) {
        days -= drv_rtc_days_in_month(y, m);
        m++;
        if (m > 12u) {
            m = 1u;
            y++;
        }
    }

    t->year    = y;
    t->month   = m;
    t->day     = (uint8_t)(days + 1u);
    t->hour    = (uint8_t)(rem / 3600u);
    t->minute  = (uint8_t)((rem % 3600u) / 60u);
    t->second  = (uint8_t)(rem % 60u);
    /* 2000-01-01 是周六 */
    t->weekday = (uint8_t)((6u + (counter / DRV_RTC_SEC_PER_DAY)) % 7u);
}

/**
 * @brief  RTC 硬件初始化：开备份域、启 LSE、配 1Hz 预分频
 * @note   用备份寄存器里的魔法字判断"是不是第一次上电"：
 *         有魔法字 = 之前配过，只需等同步；没有 = 全新，要完整配一遍。
 *         这样复位后时间不会丢。
 */
static void drv_rtc_hw_init(void)
{
    uint32_t guard;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    if (BKP_ReadBackupRegister(BKP_DR1) != DRV_RTC_BKP_MAGIC) {
        BKP_DeInit();
        RCC_LSEConfig(RCC_LSE_ON);
        guard = 100000u;
        while ((RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET) && (--guard != 0u)) {
        }
        if (guard != 0u) {
            RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
            RCC_RTCCLKCmd(ENABLE);
            RTC_WaitForSynchro();
            RTC_WaitForLastTask();
            RTC_SetPrescaler(32767u);       /* 32.768kHz / 32768 = 1Hz */
            RTC_WaitForLastTask();
            RTC_SetCounter(0u);
            RTC_WaitForLastTask();
            BKP_WriteBackupRegister(BKP_DR1, DRV_RTC_BKP_MAGIC);
        }
    } else {
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();
    }
}

/* 读秒计数器并换算成日期时间 */
static bsp_status_t drv_rtc_op_get(bsp_rtc_t *self, bsp_rtc_time_t *out)
{
    BSP_UNUSED(self);
    if (out == NULL) {
        return BSP_ERR_PARAM;
    }
    drv_rtc_from_counter(RTC_GetCounter(), out);
    return BSP_OK;
}

/* 把日期时间换算成秒写进 RTC。写之前要等上一次写完成 */
static bsp_status_t drv_rtc_op_set(bsp_rtc_t *self, const bsp_rtc_time_t *in)
{
    BSP_UNUSED(self);
    if (in == NULL) {
        return BSP_ERR_PARAM;
    }
    RTC_WaitForLastTask();
    RTC_SetCounter(drv_rtc_to_counter(in));
    RTC_WaitForLastTask();
    return BSP_OK;
}

/* 备份域没配过（比如电池掉了），说明时间不可信，需要重新校时 */
static uint8_t drv_rtc_op_is_reset(bsp_rtc_t *self)
{
    BSP_UNUSED(self);
    return (uint8_t)(BKP_ReadBackupRegister(BKP_DR1) != DRV_RTC_BKP_MAGIC);
}

static const bsp_rtc_ops_t drv_rtc_ops = { drv_rtc_op_get, drv_rtc_op_set, drv_rtc_op_is_reset };
static bsp_rtc_t s_rtc = { &drv_rtc_ops, NULL };
static uint8_t   s_ready = 0u;

/**
 * @brief  拿到 RTC 句柄（第一次调用会自动做硬件初始化）
 * @retval 句柄
 */
bsp_rtc_t *drv_rtc_get(void)
{
    if (s_ready == 0u) {
        drv_rtc_hw_init();
        s_ready = 1u;
    }
    return &s_rtc;
}
