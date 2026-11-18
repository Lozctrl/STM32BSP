/**
 * @file    bsp_rtc.h
 * @brief   实时时钟契约。时间用结构化字段表达，避免"秒计数器起点"这种平台私有约定。
 * @layer   Drivers / Interface
 */
#ifndef BSP_RTC_H
#define BSP_RTC_H

#include "bsp_types.h"

typedef struct bsp_rtc bsp_rtc_t;

typedef struct {
    uint16_t year;      /**< 完整年份，例如 2026 */
    uint8_t  month;     /**< 1~12 */
    uint8_t  day;       /**< 1~31 */
    uint8_t  hour;      /**< 0~23 */
    uint8_t  minute;    /**< 0~59 */
    uint8_t  second;    /**< 0~59 */
    uint8_t  weekday;   /**< 0=周日 ... 6=周六 */
} bsp_rtc_time_t;

typedef struct {
    bsp_status_t (*get)(bsp_rtc_t *self, bsp_rtc_time_t *out);
    bsp_status_t (*set)(bsp_rtc_t *self, const bsp_rtc_time_t *in);
    /** 是否已掉电保持过低速域（返回 1 表示需要重新校时） */
    uint8_t      (*is_reset)(bsp_rtc_t *self);
} bsp_rtc_ops_t;

struct bsp_rtc {
    const bsp_rtc_ops_t *ops;
    void                *hw;
};

/**
 * @brief  读当前日期时间
 * @param  self RTC 句柄
 * @param  out  输出：年月日时分秒 + 星期
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   时间用"结构体字段"表达，不是秒计数器。
 *         这样换平台时不用关心"这个秒数是从哪一年开始算的"。
 */
BSP_INLINE bsp_status_t bsp_rtc_get(bsp_rtc_t *self, bsp_rtc_time_t *out)
{
    return self->ops->get(self, out);
}

/**
 * @brief  设置日期时间（校时）
 * @param  self RTC 句柄
 * @param  in   要写入的时间
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   年份写完整年份（2026），不是"从 2000 年起的偏移"。
 */
BSP_INLINE bsp_status_t bsp_rtc_set(bsp_rtc_t *self, const bsp_rtc_time_t *in)
{
    return self->ops->set(self, in);
}

#endif /* BSP_RTC_H */
