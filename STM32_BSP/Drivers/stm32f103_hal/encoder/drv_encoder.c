/**
 * @file    drv_encoder.c
 * @brief   定时器编码器模式（HAL 后端，TIM3/TIM4）。
 * @layer   Drivers / stm32f103_hal
 *
 * SPL 用 TIM_EncoderInterfaceConfig，HAL 用 HAL_TIM_Encoder_Init +
 * HAL_TIM_Encoder_Start，读计数用 __HAL_TIM_GET_COUNTER 宏。
 */
#include "stm32f1xx_hal.h"
#include "drv_encoder.h"
#include "drv_config.h"
#include "drv_gpio.h"

typedef struct {
    bsp_encoder_t     api;
    TIM_HandleTypeDef htim;
} hal_encoder_t;

static hal_encoder_t s_pool[DRV_ENCODER_POOL_SIZE];
static uint8_t       s_used = 0u;

static int16_t drv_encoder_read_delta(bsp_encoder_t *self)
{
    hal_encoder_t *e = (hal_encoder_t *)self;
    int16_t        delta = (int16_t)__HAL_TIM_GET_COUNTER(&e->htim);

    __HAL_TIM_SET_COUNTER(&e->htim, 0u);
    return delta;
}

static int32_t drv_encoder_read_total(bsp_encoder_t *self)
{
    hal_encoder_t *e = (hal_encoder_t *)self;
    return (int32_t)__HAL_TIM_GET_COUNTER(&e->htim);
}

static void drv_encoder_reset(bsp_encoder_t *self)
{
    hal_encoder_t *e = (hal_encoder_t *)self;
    __HAL_TIM_SET_COUNTER(&e->htim, 0u);
}

static const bsp_encoder_ops_t drv_encoder_ops = {
    drv_encoder_read_delta,
    drv_encoder_read_total,
    drv_encoder_reset
};

/**
 * @brief  创建编码器接口
 * @param  tim 只支持 TIM3/TIM4（只有它们的 CH1/CH2 支持编码器模式）
 * @param  cha A 相引脚
 * @param  chb B 相引脚
 * @retval 句柄；不支持或池满返回 NULL
 * @note   TI12 = A/B 两相都计数（四倍频）。
 *         输入滤波器 0xF：滤掉电机换向时产生的毛刺，否则计数会乱跳。
 */
bsp_encoder_t *drv_encoder_create(drv_tim_t tim, bsp_gpio_t *cha, bsp_gpio_t *chb)
{
    hal_encoder_t        *e;
    GPIO_InitTypeDef      gpio;
    TIM_Encoder_InitTypeDef enc;
    drv_gpio_hw_t        *ga;
    drv_gpio_hw_t        *gb;

    if (((tim != DRV_TIM_3) && (tim != DRV_TIM_4)) ||
        (cha == NULL) || (chb == NULL) || (s_used >= DRV_ENCODER_POOL_SIZE)) {
        return NULL;
    }

    ga = (drv_gpio_hw_t *)cha->hw;
    gb = (drv_gpio_hw_t *)chb->hw;

    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;       /* 编码器多为开漏输出，必须上拉 */
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin   = ga->pin_mask;
    HAL_GPIO_Init((GPIO_TypeDef *)ga->port, &gpio);
    gpio.Pin   = gb->pin_mask;
    HAL_GPIO_Init((GPIO_TypeDef *)gb->port, &gpio);

    e = &s_pool[s_used];

    if (tim == DRV_TIM_3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        e->htim.Instance = TIM3;
    } else {
        __HAL_RCC_TIM4_CLK_ENABLE();
        e->htim.Instance = TIM4;
    }

    e->htim.Init.Prescaler         = 0u;
    e->htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    e->htim.Init.Period            = 65535u;
    e->htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    e->htim.Init.RepetitionCounter = 0u;
    e->htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    enc.EncoderMode  = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter    = 0x0Fu;
    enc.IC2Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter    = 0x0Fu;

    if (HAL_TIM_Encoder_Init(&e->htim, &enc) != HAL_OK) {
        return NULL;
    }

    (void)HAL_TIM_Encoder_Start(&e->htim, TIM_CHANNEL_ALL);
    __HAL_TIM_SET_COUNTER(&e->htim, 0u);

    e->api.ops = &drv_encoder_ops;
    e->api.hw  = e;
    s_used++;

    return &e->api;
}
