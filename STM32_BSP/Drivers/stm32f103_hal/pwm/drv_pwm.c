/**
 * @file    drv_pwm.c
 * @brief   通用定时器 PWM 实现（HAL 后端，TIM1~TIM4）。
 * @layer   Drivers / stm32f103_hal
 *
 * 和 SPL 版的差别：
 *   SPL: TIM_TimeBaseInit + TIM_OCxInit + TIM_SetCompareN
 *   HAL: HAL_TIM_PWM_Init + HAL_TIM_PWM_ConfigChannel + __HAL_TIM_SET_COMPARE
 * 占空比换算（Q16 → CCR）的逻辑完全一样，因为那是数学，不是库。
 */
#include "stm32f1xx_hal.h"
#include "drv_pwm.h"
#include "drv_config.h"
#include "drv_gpio.h"

#define DRV_PWM_TARGET_ARR      20000u
#define DRV_PWM_MAX_ARR         65536u

typedef struct {
    bsp_pwm_t         api;
    TIM_HandleTypeDef htim;
    uint8_t           channel;      /* 1~4 */
    uint16_t          arr;          /* 目标自动重装值（实际写 arr-1） */
    uint16_t          psc;
    uint32_t          period_us;
} hal_pwm_t;

static hal_pwm_t s_pool[DRV_PWM_POOL_SIZE];
static uint8_t   s_used = 0u;

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

static void drv_pwm_clk_enable(drv_tim_t tim)
{
    switch (tim) {
    case DRV_TIM_1: __HAL_RCC_TIM1_CLK_ENABLE(); break;
    case DRV_TIM_2: __HAL_RCC_TIM2_CLK_ENABLE(); break;
    case DRV_TIM_3: __HAL_RCC_TIM3_CLK_ENABLE(); break;
    case DRV_TIM_4: __HAL_RCC_TIM4_CLK_ENABLE(); break;
    default: break;
    }
}

static uint32_t drv_pwm_channel_of(uint8_t channel)
{
    switch (channel) {
    case 1u: return TIM_CHANNEL_1;
    case 2u: return TIM_CHANNEL_2;
    case 3u: return TIM_CHANNEL_3;
    case 4u: return TIM_CHANNEL_4;
    default: return TIM_CHANNEL_1;
    }
}

/**
 * @brief  取定时器的实际计数时钟（不是 PCLK：APB 分频不为 1 时定时器时钟翻倍）
 * @note   F1 上 TIM1 挂 APB2，TIM2~TIM4 挂 APB1。
 *         用运行时频率而不是编译期常量，HSE 起不来退到 HSI 64MHz 时也准。
 */
static uint32_t drv_pwm_tim_clk_hz(drv_tim_t tim)
{
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    uint32_t pclk = (tim == DRV_TIM_1) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();

    return (pclk == hclk) ? pclk : (pclk * 2u);
}

/**
 * @brief  根据频率算 PSC/ARR（和 SPL 版逐字相同）
 * @param  clk_hz  定时器计数时钟
 * @note   目标是让 ARR 接近 20000：这样占空比有 2 万级分辨率，
 *         舵机能做到 1us 精度，20kHz 电机则自动降分辨率保频率。
 */
static void drv_pwm_calc(uint32_t clk_hz, uint32_t freq_hz, uint16_t *psc, uint16_t *arr)
{
    uint32_t p;
    uint32_t a;

    p = clk_hz / (freq_hz * DRV_PWM_TARGET_ARR);
    if (p == 0u) {
        p = 1u;
    }
    a = clk_hz / (freq_hz * p);

    while ((a > DRV_PWM_MAX_ARR) && (p < 65536u)) {
        p++;
        a = clk_hz / (freq_hz * p);
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
 * @brief  写占空比：Q16 -> CCR
 * @param  self     PWM 通道
 * @param  duty_q16 0~65535
 * @retval BSP_OK
 * @note   CCR = duty × ARR / 65536，整数运算，不引入浮点误差。
 */
static bsp_status_t drv_pwm_set_duty(bsp_pwm_t *self, uint16_t duty_q16)
{
    hal_pwm_t *p = (hal_pwm_t *)self;
    uint32_t   ccr = ((uint32_t)duty_q16 * (uint32_t)p->arr) >> 16;

    if (ccr > (uint32_t)p->arr) {
        ccr = p->arr;
    }
    __HAL_TIM_SET_COMPARE(&p->htim, drv_pwm_channel_of(p->channel), (uint16_t)ccr);
    return BSP_OK;
}

/* 开/关通道。HAL 的 Start 对 TIM1 会自动开 MOE，不用我们手动置位 */
static void drv_pwm_enable(bsp_pwm_t *self, uint8_t on)
{
    hal_pwm_t *p = (hal_pwm_t *)self;

    if (on != 0u) {
        (void)HAL_TIM_PWM_Start(&p->htim, drv_pwm_channel_of(p->channel));
    } else {
        (void)HAL_TIM_PWM_Stop(&p->htim, drv_pwm_channel_of(p->channel));
    }
}

static bsp_time_us_t drv_pwm_period_us(bsp_pwm_t *self)
{
    return ((hal_pwm_t *)self)->period_us;
}

static const bsp_pwm_ops_t drv_pwm_ops = {
    drv_pwm_set_duty,
    drv_pwm_enable,
    drv_pwm_period_us
};

/**
 * @brief  创建 PWM 通道
 * @param  tim      TIM1~TIM4
 * @param  channel  1~4
 * @param  pin      输出引脚（切成复用推挽）
 * @param  freq_hz  载波频率
 * @retval 句柄；参数非法 / 池满 / HAL 初始化失败返回 NULL
 */
bsp_pwm_t *drv_pwm_create(drv_tim_t tim, uint8_t channel, bsp_gpio_t *pin, uint32_t freq_hz)
{
    hal_pwm_t        *p;
    TIM_TypeDef      *reg;
    GPIO_InitTypeDef  gpio;
    TIM_OC_InitTypeDef oc;
    drv_gpio_hw_t    *gh;
    uint32_t          clk_hz;

    if ((pin == NULL) || (freq_hz == 0u) || (channel < 1u) || (channel > 4u) ||
        (s_used >= DRV_PWM_POOL_SIZE)) {
        return NULL;
    }
    reg = drv_pwm_reg_of(tim);
    if (reg == NULL) {
        return NULL;
    }

    drv_pwm_clk_enable(tim);

    /* 引脚复用推挽：端口信息从 bsp_gpio_t 内部取，不向上层暴露 */
    gh = (drv_gpio_hw_t *)pin->hw;
    gpio.Pin   = gh->pin_mask;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init((GPIO_TypeDef *)gh->port, &gpio);

    p = &s_pool[s_used];
    clk_hz = drv_pwm_tim_clk_hz(tim);
    drv_pwm_calc(clk_hz, freq_hz, &p->psc, &p->arr);
    p->channel  = channel;
    p->period_us = (uint32_t)(((uint32_t)p->arr * (uint32_t)(p->psc + 1u)) /
                              (clk_hz / 1000000u));

    p->htim.Instance               = reg;
    p->htim.Init.Prescaler         = p->psc;
    p->htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    p->htim.Init.Period            = (uint32_t)(p->arr - 1u);
    p->htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    p->htim.Init.RepetitionCounter = 0u;
    p->htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(&p->htim) != HAL_OK) {
        return NULL;
    }

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0u;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    if (HAL_TIM_PWM_ConfigChannel(&p->htim, &oc, drv_pwm_channel_of(channel)) != HAL_OK) {
        return NULL;
    }

    p->api.ops = &drv_pwm_ops;
    p->api.hw  = p;
    s_used++;

    return &p->api;
}
