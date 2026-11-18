/**
 * @file    sched.c
 * @brief   协作式调度器实现。
 * @layer   Middlewares
 */
#include "sched.h"

/**
 * @brief  初始化调度器（清空任务表）
 * @param  s 调度器对象
 */
void sched_init(sched_t *s)
{
    uint8_t i;
    if (s == NULL) {
        return;
    }
    s->count = 0u;
    s->overrun_cnt = 0u;
    s->started = 0u;
    s->t0_ms = 0u;
    for (i = 0u; i < SCHED_MAX_TASKS; i++) {
        s->tasks[i].fn = NULL;
        s->tasks[i].ctx = NULL;
        s->tasks[i].period_ms = 0u;
        s->tasks[i].next_ms = 0u;
        s->tasks[i].enabled = 0u;
    }
}

/**
 * @brief  注册一个周期任务
 * @param  s              调度器对象
 * @param  fn             任务函数
 * @param  ctx            传给任务函数的上下文指针（一般是模块对象）
 * @param  period_ms      多久跑一次（毫秒）
 * @param  start_delay_ms 第一次延迟多久跑（想立刻跑就传 0）
 * @retval >=0 任务编号（可用于 sched_enable）；<0 表示出错
 * @note   开始运行前，next_ms 里暂存的是"启动延时"；
 *         第一次 sched_run 时才会换算成绝对时刻。
 */
int16_t sched_add(sched_t *s, sched_fn_t fn, void *ctx,
                  bsp_time_ms_t period_ms, bsp_time_ms_t start_delay_ms)
{
    sched_task_t *t;

    if ((s == NULL) || (fn == NULL) || (period_ms == 0u)) {
        return (int16_t)BSP_ERR_PARAM;
    }
    if (s->count >= SCHED_MAX_TASKS) {
        return (int16_t)BSP_ERR_NOMEM;
    }

    t = &s->tasks[s->count];
    t->fn = fn;
    t->ctx = ctx;
    t->period_ms = period_ms;
    t->enabled = 1u;
    /* 首次 run 之前，next_ms 暂存"启动延时"，届时换算成绝对截止时刻 */
    t->next_ms = start_delay_ms;

    return (int16_t)s->count++;
}

/**
 * @brief  临时开/关某个任务
 * @param  s       调度器对象
 * @param  id      sched_add 返回的编号
 * @param  enabled 1=启用，0=暂停
 */
void sched_enable(sched_t *s, int16_t id, uint8_t enabled)
{
    if ((s != NULL) && (id >= 0) && ((uint8_t)id < s->count)) {
        s->tasks[id].enabled = enabled;
    }
}

/**
 * @brief  跑一拍：把所有到期的任务执行一次
 * @param  s   调度器对象
 * @param  now 当前毫秒（bsp_time_ms 的返回值）
 * @note   两个关键设计：
 *         1) 第一次调用只做时间基准对齐，不执行任务；
 *         2) 任务到期后 next_ms 从"当前时间"重算，而不是"上次计划时间 + 周期"。
 *            所以偶尔卡一下不会攒一堆任务连着爆发（防雪崩）。
 */
void sched_run(sched_t *s, bsp_time_ms_t now)
{
    uint8_t i;
    sched_task_t *t;

    if (s == NULL) {
        return;
    }

    if (s->started == 0u) {
        s->started = 1u;
        s->t0_ms = now;
        for (i = 0u; i < s->count; i++) {
            s->tasks[i].next_ms = s->t0_ms + s->tasks[i].next_ms;
        }
        return;     /* 首拍只做时间基准对齐 */
    }

    for (i = 0u; i < s->count; i++) {
        t = &s->tasks[i];
        if ((t->enabled == 0u) || (t->fn == NULL)) {
            continue;
        }
        if ((int32_t)(now - t->next_ms) >= 0) {
            if ((uint32_t)(now - t->next_ms) >= (2u * t->period_ms)) {
                s->overrun_cnt++;
            }
            t->next_ms = now + t->period_ms;
            t->fn(t->ctx);
        }
    }
}
