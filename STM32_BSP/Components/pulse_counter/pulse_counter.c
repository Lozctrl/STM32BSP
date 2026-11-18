/**
 * @file    pulse_counter.c
 * @brief   边沿计数组件实现。
 * @layer   Components
 */
#include "pulse_counter.h"

/* 中断服务：这里只允许"加一"这一条语句。
   任何耗时操作（打印、I2C、延时）放进中断都会拖垮整个系统 */
static void pulse_counter_isr(void *ctx)
{
    pulse_counter_t *pc = (pulse_counter_t *)ctx;

    if (pc != NULL) {
        pc->isr_count++;        /* 中断上下文：只许这一条语句，禁止任何调用 */
    }
}

/**
 * @brief  初始化边沿计数
 * @param  pc   对象
 * @param  exti 中断管理器（注入）
 * @param  pin  信号脚
 * @param  edge 计数边沿（对射红外一般用下降沿）
 * @retval BSP_OK
 */
bsp_status_t pulse_counter_init(pulse_counter_t *pc, bsp_exti_t *exti,
                                bsp_gpio_t *pin, bsp_edge_t edge)
{
    if ((pc == NULL) || (exti == NULL) || (pin == NULL)) {
        return BSP_ERR_PARAM;
    }

    pc->pin = pin;
    pc->exti = exti;
    pc->isr_count = 0u;
    pc->last_seen = 0u;
    pc->delta = 0u;
    pc->glitch_cnt = 0u;

    (void)bsp_gpio_set_mode(pin, BSP_GPIO_MODE_IN_PULLUP);
    return bsp_exti_attach(exti, pin, edge, pulse_counter_isr, pc);
}

/**
 * @brief  取走"自上次调用以来的脉冲数"
 * @param  pc 对象
 * @retval 增量
 * @note   用 "本次 - 上次" 的差值算，所以计数器回绕（49 天）也不会算错。
 */
uint32_t pulse_counter_take(pulse_counter_t *pc)
{
    uint32_t now_count;

    if (pc == NULL) {
        return 0u;
    }
    now_count = pc->isr_count;      /* 32 位对齐读，单核上原子 */
    pc->delta = now_count - pc->last_seen;
    pc->last_seen = now_count;
    return pc->delta;
}

/**
 * @brief  读累计脉冲数（不清零）
 * @param  pc 对象
 * @retval 累计值
 */
uint32_t pulse_counter_total(const pulse_counter_t *pc)
{
    return (pc != NULL) ? pc->isr_count : 0u;
}
