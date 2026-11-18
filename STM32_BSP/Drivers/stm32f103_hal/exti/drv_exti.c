/**
 * @file    drv_exti.c
 * @brief   外部中断管理（HAL 后端）。
 * @layer   Drivers / stm32f103_hal
 *
 * HAL 已经帮我们做掉了"16 条线共用中断向量"的分发：
 *   硬件中断 → HAL_GPIO_EXTI_IRQHandler(pin) → HAL_GPIO_EXTI_Callback(pin)
 * 所以我们只需要覆写那个 __weak 回调，按线号找到注册的业务函数即可。
 * 相比 SPL 版自己写 7 个 IRQHandler，这里少了 60 行，而且不会漏清标志位。
 */
#include "stm32f1xx_hal.h"
#include "drv_exti.h"
#include "drv_config.h"
#include "drv_gpio.h"

typedef struct {
    bsp_irq_cb_t cb;
    void        *ctx;
    uint8_t      active;
} exti_slot_t;

static exti_slot_t s_lines[16];

static uint8_t drv_exti_pin_of(uint16_t mask)
{
    uint8_t i;

    for (i = 0u; i < 16u; i++) {
        if ((mask & (uint16_t)(1u << i)) != 0u) {
            return i;
        }
    }
    return 0xFFu;
}

static IRQn_Type drv_exti_irqn(uint8_t line)
{
    if (line <= 4u) {
        return (IRQn_Type)((int32_t)EXTI0_IRQn + (int32_t)line);
    }
    if (line <= 9u) {
        return EXTI9_5_IRQn;
    }
    return EXTI15_10_IRQn;
}

/**
 * @brief  给引脚挂中断回调
 * @param  self 管理器
 * @param  pin  引脚句柄
 * @param  edge 边沿
 * @param  cb   回调
 * @param  ctx  上下文
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   HAL 里"中断模式"是 GPIO_Init 的一部分（GPIO_MODE_IT_xxx），
 *         不再像 SPL 那样单独配 EXTI。
 */
static bsp_status_t drv_exti_attach(bsp_exti_t *self, bsp_gpio_t *pin, bsp_edge_t edge,
                                    bsp_irq_cb_t cb, void *ctx)
{
    drv_gpio_hw_t   *hw;
    GPIO_InitTypeDef init;
    uint8_t          line;

    BSP_UNUSED(self);

    if ((pin == NULL) || (cb == NULL)) {
        return BSP_ERR_PARAM;
    }

    hw = (drv_gpio_hw_t *)pin->hw;
    line = drv_exti_pin_of(hw->pin_mask);
    if (line > 15u) {
        return BSP_ERR_PARAM;
    }

    init.Pin   = hw->pin_mask;
    init.Pull  = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (edge) {
    case BSP_EDGE_RISING:  init.Mode = GPIO_MODE_IT_RISING; break;
    case BSP_EDGE_BOTH:    init.Mode = GPIO_MODE_IT_RISING_FALLING; break;
    case BSP_EDGE_FALLING:
    default:               init.Mode = GPIO_MODE_IT_FALLING; break;
    }
    HAL_GPIO_Init((GPIO_TypeDef *)hw->port, &init);

    s_lines[line].cb = cb;
    s_lines[line].ctx = ctx;
    s_lines[line].active = 1u;

    /* 抢占优先级 1：比串口(2)高，保证边沿不丢 */
    HAL_NVIC_SetPriority(drv_exti_irqn(line), 1u, 0u);
    HAL_NVIC_EnableIRQ(drv_exti_irqn(line));
    return BSP_OK;
}

static bsp_status_t drv_exti_detach(bsp_exti_t *self, bsp_gpio_t *pin)
{
    drv_gpio_hw_t   *hw;
    GPIO_InitTypeDef init;
    uint8_t          line;

    BSP_UNUSED(self);

    if (pin == NULL) {
        return BSP_ERR_PARAM;
    }
    hw = (drv_gpio_hw_t *)pin->hw;
    line = drv_exti_pin_of(hw->pin_mask);
    if (line > 15u) {
        return BSP_ERR_PARAM;
    }

    /* 重新配成普通输入，就等于关掉这条线的中断 */
    init.Pin   = hw->pin_mask;
    init.Mode  = GPIO_MODE_INPUT;
    init.Pull  = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init((GPIO_TypeDef *)hw->port, &init);

    s_lines[line].active = 0u;
    s_lines[line].cb = NULL;
    return BSP_OK;
}

static const bsp_exti_ops_t drv_exti_ops = { drv_exti_attach, drv_exti_detach };
static bsp_exti_t s_exti = { &drv_exti_ops, NULL };

bsp_exti_t *drv_exti_get(void)
{
    return &s_exti;
}

/* ---------------- HAL 回调 + 中断入口 ---------------- */

/**
 * @brief  HAL 的 EXTI 总回调：按引脚号找到注册项
 * @param  GPIO_Pin 触发的那一位（GPIO_PIN_x）
 * @note   跑在中断里，只许置标志/计数。
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint8_t line = drv_exti_pin_of(GPIO_Pin);

    if ((line <= 15u) && (s_lines[line].active != 0u) && (s_lines[line].cb != NULL)) {
        s_lines[line].cb(s_lines[line].ctx);
    }
}

#if (DRV_CFG_EXTI_OWNS_IRQ != 0u)
/*
 * 这 7 个入口如果 CubeMX 已经在 stm32f1xx_it.c 里生成了，
 * 请把 drv_config.h 的 DRV_CFG_EXTI_OWNS_IRQ 改成 0，否则链接期会重复定义。
 */
void EXTI0_IRQHandler(void)      { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void EXTI1_IRQHandler(void)      { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1); }
void EXTI2_IRQHandler(void)      { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2); }
void EXTI3_IRQHandler(void)      { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3); }
void EXTI4_IRQHandler(void)      { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}
void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}
#endif /* DRV_CFG_EXTI_OWNS_IRQ */
