/**
 * @file    drv_exti.c
 * @brief   EXTI0~15 共享中断分发器。
 * @layer   Drivers / stm32f103
 *
 * 教程把 EXTI15_10_IRQHandler 直接写在传感器文件里，一个引脚绑死一个中断函数。
 * 这里做成"线号 → 回调"的注册表，同一条线上挂什么传感器由上层决定。
 */
#include "stm32f10x.h"
#include "drv_exti.h"
#include "drv_gpio.h"

typedef struct {
    bsp_irq_cb_t cb;
    void        *ctx;
    uint8_t      active;
} exti_slot_t;

static exti_slot_t s_lines[16];

/* 从 16 位掩码里找出是第几位（EXTI 线号就是引脚号 0~15） */
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

/* 端口 -> AFIO 的端口选择码。F103 的 EXTI 是"7 个端口共用 16 条线"，
   所以要先用 AFIO 指定"这条线接到哪个端口" */
static uint8_t drv_exti_port_source(uint8_t port_index)
{
    switch (port_index) {
    case 0u: return GPIO_PortSourceGPIOA;
    case 1u: return GPIO_PortSourceGPIOB;
    case 2u: return GPIO_PortSourceGPIOC;
    case 3u: return GPIO_PortSourceGPIOD;
#ifdef GPIOE
    case 4u: return GPIO_PortSourceGPIOE;
#endif
    default: return GPIO_PortSourceGPIOA;
    }
}

/* 线号 -> 中断向量。0~4 各自独立，5~9 共用一个，10~15 共用一个 */
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
 * @param  edge 边沿类型
 * @param  cb   回调
 * @param  ctx  回调上下文
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   同一时刻一个引脚只能挂一个回调（覆盖式），
 *         因为硬件上一条 EXTI 线就只有一个触发源。
 */
static bsp_status_t drv_exti_attach(bsp_exti_t *self, bsp_gpio_t *pin, bsp_edge_t edge,
                                    bsp_irq_cb_t cb, void *ctx)
{
    drv_gpio_hw_t    *hw;
    uint8_t           line;
    EXTI_InitTypeDef  init;

    BSP_UNUSED(self);

    if ((pin == NULL) || (cb == NULL)) {
        return BSP_ERR_PARAM;
    }

    hw = (drv_gpio_hw_t *)pin->hw;
    line = drv_exti_pin_of(hw->pin_mask);
    if (line > 15u) {
        return BSP_ERR_PARAM;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_EXTILineConfig((uint8_t)drv_exti_port_source(hw->port_index), line);

    init.EXTI_Line = (uint32_t)(1u << line);
    init.EXTI_LineCmd = ENABLE;
    init.EXTI_Mode = EXTI_Mode_Interrupt;
    switch (edge) {
    case BSP_EDGE_RISING:  init.EXTI_Trigger = EXTI_Trigger_Rising; break;
    case BSP_EDGE_BOTH:    init.EXTI_Trigger = EXTI_Trigger_Rising_Falling; break;
    case BSP_EDGE_FALLING:
    default:               init.EXTI_Trigger = EXTI_Trigger_Falling; break;
    }
    EXTI_Init(&init);

    s_lines[line].cb = cb;
    s_lines[line].ctx = ctx;
    s_lines[line].active = 1u;

    NVIC_SetPriority(drv_exti_irqn(line), 1u);
    NVIC_EnableIRQ(drv_exti_irqn(line));
    return BSP_OK;
}

/**
 * @brief  关掉某个引脚的中断并清掉回调
 * @param  self 管理器
 * @param  pin  引脚句柄
 * @retval BSP_OK / BSP_ERR_PARAM
 */
static bsp_status_t drv_exti_detach(bsp_exti_t *self, bsp_gpio_t *pin)
{
    drv_gpio_hw_t   *hw;
    uint8_t          line;
    EXTI_InitTypeDef init;

    BSP_UNUSED(self);

    if (pin == NULL) {
        return BSP_ERR_PARAM;
    }
    hw = (drv_gpio_hw_t *)pin->hw;
    line = drv_exti_pin_of(hw->pin_mask);
    if (line > 15u) {
        return BSP_ERR_PARAM;
    }

    init.EXTI_Line = (uint32_t)(1u << line);
    init.EXTI_LineCmd = DISABLE;
    init.EXTI_Mode = EXTI_Mode_Interrupt;
    init.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_Init(&init);

    s_lines[line].active = 0u;
    s_lines[line].cb = NULL;
    return BSP_OK;
}

static const bsp_exti_ops_t drv_exti_ops = { drv_exti_attach, drv_exti_detach };
static bsp_exti_t s_exti = { &drv_exti_ops, NULL };

/**
 * @brief  拿到中断管理器句柄
 * @retval 句柄
 */
bsp_exti_t *drv_exti_get(void)
{
    return &s_exti;
}

/* ---------------- 中断入口 ---------------- */

/* 公共分发逻辑：查挂起位 -> 调回调 -> 清挂起位。
   必须在调回调之后清标志，否则边沿期间新来的脉冲会丢 */
static void drv_exti_dispatch(uint8_t first, uint8_t last)
{
    uint8_t line;

    for (line = first; line <= last; line++) {
        if ((EXTI_GetITStatus((uint32_t)(1u << line)) == SET) &&
            (s_lines[line].active != 0u) && (s_lines[line].cb != NULL)) {
            s_lines[line].cb(s_lines[line].ctx);
        }
        EXTI_ClearITPendingBit((uint32_t)(1u << line));
    }
}

/* 7 个中断入口，全部转发给同一个分发函数。
   这就是"注册表"式设计：加一个新传感器不用改任何中断代码 */ 
void EXTI0_IRQHandler(void)      { drv_exti_dispatch(0u, 0u); }
void EXTI1_IRQHandler(void)      { drv_exti_dispatch(1u, 1u); }
void EXTI2_IRQHandler(void)      { drv_exti_dispatch(2u, 2u); }
void EXTI3_IRQHandler(void)      { drv_exti_dispatch(3u, 3u); }
void EXTI4_IRQHandler(void)      { drv_exti_dispatch(4u, 4u); }
void EXTI9_5_IRQHandler(void)    { drv_exti_dispatch(5u, 9u); }
void EXTI15_10_IRQHandler(void)  { drv_exti_dispatch(10u, 15u); }
