/**
 * @file    sched.h
 * @brief   协作式软定时调度器：所有周期任务共用 SysTick 毫秒节拍，零动态内存。
 * @layer   Middlewares
 *
 * 语义：任务已到期时"补执行一次"，不会因为一次卡顿而连续爆发多个周期（防雪崩），
 *       next_ms 从当前时间重算，因此天然不会积累漂移误差。
 */
#ifndef SCHED_H
#define SCHED_H

#include "bsp_types.h"

#define SCHED_MAX_TASKS     8u

typedef void (*sched_fn_t)(void *ctx);

typedef struct {
    sched_fn_t    fn;
    void         *ctx;
    bsp_time_ms_t period_ms;
    bsp_time_ms_t next_ms;
    uint8_t       enabled;
} sched_task_t;

typedef struct {
    sched_task_t tasks[SCHED_MAX_TASKS];
    uint8_t      count;
    uint32_t     overrun_cnt;   /**< 到期未及时执行的次数（性能诊断） */
    uint8_t      started;       /**< 首次 run 后置 1，用于把"启动延时"换算为绝对时刻 */
    bsp_time_ms_t t0_ms;
} sched_t;

void         sched_init(sched_t *s);
int16_t      sched_add(sched_t *s, sched_fn_t fn, void *ctx,
                       bsp_time_ms_t period_ms, bsp_time_ms_t start_delay_ms);
void         sched_enable(sched_t *s, int16_t id, uint8_t enabled);
/** @brief 执行所有到期任务；now 由调用方从 bsp_time 取得 */
void         sched_run(sched_t *s, bsp_time_ms_t now);

#endif /* SCHED_H */
