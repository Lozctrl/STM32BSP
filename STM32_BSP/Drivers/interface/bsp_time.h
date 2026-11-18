/**
 * @file    bsp_time.h
 * @brief   时间基准契约。所有模块的非阻塞节拍都由此驱动。
 * @layer   Drivers / Interface
 *
 * 注意：delay_us() 仅允许在"上电时序"等极短硬件时序中使用；
 *       业务层禁止使用任何阻塞延时（框架没有提供 delay_ms 契约）。
 */
#ifndef BSP_TIME_H
#define BSP_TIME_H

#include "bsp_types.h"

typedef struct bsp_time bsp_time_t;

typedef struct {
    bsp_time_ms_t (*get_ms)(bsp_time_t *self);              /**< 单调递增毫秒计数 */
    void          (*delay_us)(bsp_time_t *self, bsp_time_us_t us); /**< 微秒级忙等，仅驱动内部时序用 */
} bsp_time_ops_t;

struct bsp_time {
    const bsp_time_ops_t *ops;
    void                 *hw;
};

/**
 * @brief  取系统运行毫秒数（上电后一直累加，约 49 天清零一次）
 * @param  self 时间句柄，由 drv_time_get() 拿到
 * @retval 毫秒计数
 * @note   所有非阻塞逻辑都靠这个数：消抖、超时、PID 的 dt、调度器周期。
 *         先取 now = bsp_time_ms(t); 再拿它去算差值，不要自己写 for 循环等时间。
 */
BSP_INLINE bsp_time_ms_t bsp_time_ms(bsp_time_t *self)
{
    return self->ops->get_ms(self);
}

/**
 * @brief  微秒级忙等（仅给驱动内部的总线时序用）
 * @param  self 时间句柄
 * @param  us   要等多少微秒
 * @note   这是整个框架里唯一允许"死等"的地方，而且只给 I2C/SPI 的位时序用。
 *         业务代码里如果出现它，基本就是把实时性写坏了。
 */
BSP_INLINE void bsp_time_delay_us(bsp_time_t *self, bsp_time_us_t us)
{
    if ((self != NULL) && (us > 0u) && (self->ops->delay_us != NULL)) {
        self->ops->delay_us(self, us);
    }
}

/** @brief 判断是否已到期：now - start >= interval（自动处理 32 位回绕） */

/**
 * @brief  判断从 start 到现在是否已经过了 interval
 * @param  now      当前毫秒（bsp_time_ms 的返回值）
 * @param  start    起始时刻
 * @param  interval 间隔（毫秒）
 * @retval 1=到点了，0=还没到
 * @note   用无符号减法自动处理 32 位回绕，所以 49 天清零那一刻也不会误判。
 *         标准写法：if (bsp_time_elapsed(now, k->t_edge, KEY_DEBOUNCE_MS)) {...}
 */
BSP_INLINE uint8_t bsp_time_elapsed(bsp_time_ms_t now, bsp_time_ms_t start, bsp_time_ms_t interval)
{
    return (uint8_t)((bsp_time_ms_t)(now - start) >= interval);
}

#endif /* BSP_TIME_H */
