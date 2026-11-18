/**
 * @file    drv_pwm.c
 * @brief   通用定时器 PWM 实现（TIM1~TIM4）。
 * @layer   Drivers / stm32f103
 *
 * 关键点：CCR 由 Q16 占空比换算，ARR 自动选择使分辨率尽量高（目标 ~20000 级），
 *        所以同一个接口既能驱动 50Hz 舵机（1us 分辨率），也能驱动 20kHz 电机。
 */
#include "stm32f10x.h"
#include "drv_pwm.h"
#include "drv_config.h"
#include "drv_gpio.h"

#define DRV_PWM_TARGET_ARR      20000u
#define DRV_PWM_MAX_ARR         65536u

typedef struct {
    bsp_pwm_t    api;
    TIM_TypeDef *reg;
    uint8_t      channel;
    uint16_t     arr;           /* ARR（实际写入 arr-1） */
    uint16_t     psc;
    uint32_t     period_us;
} stm32_pwm_t;

static stm32_pwm_t s_pool[DRV_PWM_POOL_SIZE];
static uint8_t     s_used = 0u;

/* 枚举 -> 定时器寄存器基址。换芯片时只改这一个 switch */
static TIM_TypeDef *drv_pwm_reg_of(drv_tim_t tim)
{
    switch (tim) {
    case DRV_TIM_1: return TIM1;
    case DRV_TIM_2: return TIM2;
    case DRV_TIM_3: return TIM3;
    case DRV_TIM_4: return TIM4;
    default:        return NULL;
    }
}

/* 枚举 -> 时钟使能位。TIM1 挂在 APB2，TIM2~4 挂在 APB1，这里只是取位 */
static uint32_t drv_pwm_rcc_of(drv_tim_t tim)
{
    switch (tim) {
    case DRV_TIM_1: return RCC_APB2Periph_TIM1;
    case DRV_TIM_2: return RCC_APB1Periph_TIM2;
    case DRV_TIM_3: return RCC_APB1Periph_TIM3;
    case DRV_TIM_4: return RCC_APB1Periph_TIM4;
    default:        return 0u;
    }
}

/** @brief 计算 PSC/ARR，使 ARR 尽量接近 TARGET_ARR 以获得高占空比分辨率 */

/**
 * @brief  根据频率算出 PSC 和 ARR
 * @param  freq_hz 想要的载波频率
 * @param  psc     输出：预分频
 * @param  arr     输出：自动重装值（决定占空比分辨率）
 * @note   策略：优先让 ARR 接近 20000，这样占空比有 2 万级分辨率。
 *         50Hz 舵机因此能到 1us 精度；20kHz 电机则自动降分辨率保频率。
 */
static void drv_pwm_calc(uint32_t freq_hz, uint16_t *psc, uint16_t *arr)
{
    uint32_t p;
    uint32_t a;

    p = DRV_MCU_TIM_CLK_HZ / (freq_hz * DRV_PWM_TARGET_ARR);
    if (p == 0u) {
        p = 1u;
    }
    a = DRV_MCU_TIM_CLK_HZ / (freq_hz * p);

    while ((a > DRV_PWM_MAX_ARR) && (p < 65536u)) {
        p++;
        a = DRV_MCU_TIM_CLK_HZ / (freq_hz * p);
    }
    if (a == 0u) {
        a = 1u;
    }
    if (a > DRV_PWM_MAX_ARR) {
        a = DRV_PWM_MAX_ARR;
    }

    *psc = (uint16_t)(p - 1u);
    *arr = (uint16_t)a;
}

/**
 * @brief  写占空比：把 Q16 换算成比较寄存器 CCR
 * @param  self     PWM 通道
 * @param  duty_q16 0~65535
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   换算公式 CCR = duty × ARR / 65536，用整数乘法+右移完成，
 *         不需要浮点，也不会溢出（duty 和 ARR 都是 16 位）。
 */
static bsp_status_t drv_pwm_set_duty(bsp_pwm_t *self, uint16_t duty_q16)
{
    stm32_pwm_t *p = (stm32_pwm_t *)self;
    uint32_t ccr = ((uint32_t)duty_q16 * (uint32_t)p->arr) >> 16;

    if (ccr > (uint32_t)p->arr) {
        ccr = p->arr;
    }

    switch (p->channel) {
    case 1u: TIM_SetCompare1(p->reg, (uint16_t)ccr); break;
    case 2u: TIM_SetCompare2(p->reg, (uint16_t)ccr); break;
    case 3u: TIM_SetCompare3(p->reg, (uint16_t)ccr); break;
    case 4u: TIM_SetCompare4(p->reg, (uint16_t)ccr); break;
    default: return BSP_ERR_PARAM;
    }
    return BSP_OK;
}

/* 开/关整个定时器。关了之后输出停在低电平，电机就是断电状态 */
static void drv_pwm_enable(bsp_pwm_t *self, uint8_t on)
{
    stm32_pwm_t *p = (stm32_pwm_t *)self;
    TIM_Cmd(p->reg, (on != 0u) ? ENABLE : DISABLE);
}

/* 周期 = ARR × (PSC+1) / 定时器时钟。舵机组件靠它换算脉宽 */
static bsp_time_us_t drv_pwm_period_us(bsp_pwm_t *self)
{
    return ((stm32_pwm_t *)self)->period_us;
}

static const bsp_pwm_ops_t drv_pwm_ops = {
    drv_pwm_set_duty,
    drv_pwm_enable,
    drv_pwm_period_us
};

/**
 * @brief  创建一个 PWM 通道
 * @param  tim      定时器（TIM1~TIM4）
 * @param  channel  通道号 1~4
 * @param  pin      输出引脚（驱动内部把它切成复用推挽）
 * @param  freq_hz  载波频率：舵机 50、电机 20k、LED 1k
 * @retval 句柄；参数非法或池满返回 NULL
 * @note   引脚信息是从 bsp_gpio_t 内部取出来的——上层不需要知道它是 PA0 还是 PB6。
 *         TIM1 是高级定时器，必须额外开 MOE，否则引脚没输出。
 */
bsp_pwm_t *drv_pwm_create(drv_tim_t tim, uint8_t channel, bsp_gpio_t *pin, uint32_t freq_hz)
{
    stm32_pwm_t     *p;
    TIM_TypeDef     *reg;
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef oc;
    drv_gpio_hw_t   *gh;

    if ((pin == NULL) || (freq_hz == 0u) || (channel < 1u) || (channel > 4u) ||
        (s_used >= DRV_PWM_POOL_SIZE)) {
        return NULL;
    }
    reg = drv_pwm_reg_of(tim);
    if (reg == NULL) {
        return NULL;
    }

    if (tim == DRV_TIM_1) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(drv_pwm_rcc_of(tim), ENABLE);
    }

    /* 引脚切复用推挽：端口信息从 bsp_gpio_t 内部取，不向上层暴露 */
    gh = (drv_gpio_hw_t *)pin->hw;
    gpio.GPIO_Pin = gh->pin_mask;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init((GPIO_TypeDef *)gh->port, &gpio);

    p = &s_pool[s_used];
    s_used++;

    drv_pwm_calc(freq_hz, &p->psc, &p->arr);
    p->reg = reg;
    p->channel = channel;
    p->period_us = (uint32_t)(((uint32_t)(p->arr) * (uint32_t)(p->psc + 1u)) /
                              (DRV_MCU_TIM_CLK_HZ / 1000000u));
    p->api.ops = &drv_pwm_ops;
    p->api.hw = p;

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = (uint16_t)(p->arr - 1u);
    tb.TIM_Prescaler = p->psc;
    tb.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(reg, &tb);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0u;

    switch (channel) {
    case 1u: TIM_OC1Init(reg, &oc); TIM_OC1PreloadConfig(reg, TIM_OCPreload_Enable); break;
    case 2u: TIM_OC2Init(reg, &oc); TIM_OC2PreloadConfig(reg, TIM_OCPreload_Enable); break;
    case 3u: TIM_OC3Init(reg, &oc); TIM_OC3PreloadConfig(reg, TIM_OCPreload_Enable); break;
    case 4u: TIM_OC4Init(reg, &oc); TIM_OC4PreloadConfig(reg, TIM_OCPreload_Enable); break;
    default: break;
    }

    TIM_ARRPreloadConfig(reg, ENABLE);
    TIM_CtrlPWMOutputs(reg, ENABLE);    /* 高级定时器 TIM1 必须开 MOE */
    TIM_Cmd(reg, ENABLE);

    return &p->api;
}
