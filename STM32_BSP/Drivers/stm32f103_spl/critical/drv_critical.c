/**
 * @file    drv_critical.c
 * @brief   临界区实现：PRIMASK 保存/恢复，支持嵌套。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_critical.h"

/* 关中断并返回进入前的 PRIMASK。返回值必须原样交给 unlock，
   否则嵌套调用时会把外层的关中断状态也一起放开 */
static uint32_t drv_critical_lock(bsp_critical_t *self)
{
    uint32_t primask;

    BSP_UNUSED(self);
    primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* 恢复进入前的 PRIMASK。这就是"支持嵌套"的关键：不是无条件开中断 */
static void drv_critical_unlock(bsp_critical_t *self, uint32_t state)
{
    BSP_UNUSED(self);
    __set_PRIMASK(state);       /* 恢复进入前的状态，天然支持嵌套 */
}

static const bsp_critical_ops_t drv_critical_ops = {
    drv_critical_lock,
    drv_critical_unlock
};

static bsp_critical_t s_critical = { &drv_critical_ops, NULL };

/**
 * @brief  拿到临界区句柄
 * @retval 句柄
 */
bsp_critical_t *drv_critical_get(void)
{
    return &s_critical;
}
