/**
 * @file    drv_critical.c
 * @brief   临界区实现（HAL 后端）：PRIMASK 保存/恢复。
 * @layer   Drivers / stm32f103_hal
 *
 * 这一段和 SPL 版几乎一样，因为关中断是内核指令（__disable_irq），
 * 不属于任何外设库。保留独立文件是为了让两套后端互不依赖。
 */
#include "stm32f1xx_hal.h"
#include "drv_critical.h"

static uint32_t drv_critical_lock(bsp_critical_t *self)
{
    uint32_t primask;

    BSP_UNUSED(self);
    primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

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

bsp_critical_t *drv_critical_get(void)
{
    return &s_critical;
}
