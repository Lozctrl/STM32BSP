/**
 * @file    bsp_wdt.h
 * @brief   看门狗契约。
 * @layer   Drivers / Interface
 */
#ifndef BSP_WDT_H
#define BSP_WDT_H

#include "bsp_types.h"

typedef struct bsp_wdt bsp_wdt_t;

typedef struct {
    bsp_status_t (*start)(bsp_wdt_t *self, bsp_time_ms_t timeout_ms);
    void         (*feed)(bsp_wdt_t *self);
} bsp_wdt_ops_t;

struct bsp_wdt {
    const bsp_wdt_ops_t *ops;
    void                *hw;
};

/**
 * @brief  启动看门狗
 * @param  self       看门狗句柄
 * @param  timeout_ms 多久不喂就复位
 * @retval BSP_OK
 * @note   一旦启动就关不掉（硬件特性）。调试时要么先别启动，
 *         要么在调试器里打开"停 CPU 时冻结外设"。
 */
BSP_INLINE bsp_status_t bsp_wdt_start(bsp_wdt_t *self, bsp_time_ms_t timeout_ms)
{
    return self->ops->start(self, timeout_ms);
}

/**
 * @brief  喂狗（告诉硬件"我还活着"）
 * @param  self 看门狗句柄
 * @note   不要在主循环末尾无脑喂。正确做法是"所有任务都报到了才喂"，
 *         这样某个任务卡死能被复位救回来（见 app_main.c 的心跳机制）。
 */
BSP_INLINE void bsp_wdt_feed(bsp_wdt_t *self)
{
    self->ops->feed(self);
}

#endif /* BSP_WDT_H */
