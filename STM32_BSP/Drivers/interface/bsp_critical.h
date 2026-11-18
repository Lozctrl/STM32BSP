/**
 * @file    bsp_critical.h
 * @brief   临界区契约（关中断/开中断），用于保护 ISR 与主循环共享的数据。
 * @layer   Drivers / Interface
 */
#ifndef BSP_CRITICAL_H
#define BSP_CRITICAL_H

#include "bsp_types.h"

typedef struct bsp_critical bsp_critical_t;

typedef struct {
    uint32_t (*lock)(bsp_critical_t *self);         /**< 返回进入前的状态字 */
    void     (*unlock)(bsp_critical_t *self, uint32_t state);
} bsp_critical_ops_t;

struct bsp_critical {
    const bsp_critical_ops_t *ops;
    void                     *hw;
};

/**
 * @brief  进临界区（关中断），返回进入前的状态
 * @param  self 临界区句柄
 * @retval 进入前的 PRIMASK 值，必须原样交给 unlock
 * @note   只包住"几条赋值语句"级别的操作，别把 I2C 收发包进来——
 *         关中断 3ms 会让串口直接丢字节。
 */
BSP_INLINE uint32_t bsp_critical_lock(bsp_critical_t *self)
{
    return self->ops->lock(self);
}

/**
 * @brief  出临界区（按进入前的状态恢复中断）
 * @param  self  临界区句柄
 * @param  state lock() 的返回值
 * @note   为什么不是"无条件开中断"？因为可能本来就处于关中断状态
 *         （嵌套调用），无条件开中断会把外层也放开了。所以必须恢复原状态。
 */
BSP_INLINE void bsp_critical_unlock(bsp_critical_t *self, uint32_t state)
{
    self->ops->unlock(self, state);
}

#endif /* BSP_CRITICAL_H */
