/**
 * @file    drv_encoder.c
 * @brief   定时器编码器模式（TIM3/TIM4，TI12 四倍频）。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_encoder.h"
#include "drv_config.h"
#include "drv_gpio.h"

typedef struct {
    bsp_encoder_t api;
    TIM_TypeDef  *reg;
} stm32_encoder_t;

static stm32_encoder_t s_pool[DRV_ENCODER_POOL_SIZE];
static uint8_t         s_used = 0u;

/* 读 CNT 后立刻清零，两次读之间的差值就是增量。
   清零和读之间没有临界区保护——单核上主循环调用是安全的，
   但如果将来在中断里也读，必须加临界区 */
static int16_t drv_encoder_read_delta(bsp_encoder_t *self)
{
    stm32_encoder_t *e = (stm32_encoder_t *)self;
    int16_t delta = (int16_t)TIM_GetCounter(e->reg);

    TIM_SetCounter(e->reg, 0u);
    return delta;
}

/* 只读不清零。注意这是 16 位硬件计数器，到 65535 会绕回 */
static int32_t drv_encoder_read_total(bsp_encoder_t *self)
{
    stm32_encoder_t *e = (stm32_encoder_t *)self;
    return (int32_t)TIM_GetCounter(e->reg);
}

/* 计数器清零 */
static void drv_encoder_reset(bsp_encoder_t *self)
{
    stm32_encoder_t *e = (stm32_encoder_t *)self;
    TIM_SetCounter(e->reg, 0u);
}

static const bsp_encoder_ops_t drv_encoder_ops = {
    drv_encoder_read_delta,
    drv_encoder_read_total,
    drv_encoder_reset
};

/**
 * @brief  创建一个编码器接口（定时器编码器模式）
 * @param  tim 定时器（只支持 TIM3/TIM4，因为这两个才有 CH1/CH2 编码器模式）
 * @param  cha A 相引脚
 * @param  chb B 相引脚
 * @retval 句柄；参数不支持或池满返回 NULL
 * @note   TI12 模式 = 两相都计数的四倍频，比单相计数分辨率高 4 倍。
 *         滤波器设 0xF 是为了滤掉电机换向时的毛刺。
 */
bsp_encoder_t *drv_encoder_create(drv_tim_t tim, bsp_gpio_t *cha, bsp_gpio_t *chb)
{
    stm32_encoder_t *e;
    TIM_TimeBaseInitTypeDef tb;
    TIM_ICInitTypeDef ic;
    drv_gpio_hw_t *ga;
    drv_gpio_hw_t *gb;
    GPIO_InitTypeDef gpio;

    if (((tim != DRV_TIM_3) && (tim != DRV_TIM_4)) ||
        (cha == NULL) || (chb == NULL) || (s_used >= DRV_ENCODER_POOL_SIZE)) {
        return NULL;
    }

    ga = (drv_gpio_hw_t *)cha->hw;
    gb = (drv_gpio_hw_t *)chb->hw;

    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = ga->pin_mask;
    GPIO_Init((GPIO_TypeDef *)ga->port, &gpio);
    gpio.GPIO_Pin = gb->pin_mask;
    GPIO_Init((GPIO_TypeDef *)gb->port, &gpio);

    e = &s_pool[s_used];
    s_used++;

    if (tim == DRV_TIM_3) {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
        e->reg = TIM3;
    } else {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
        e->reg = TIM4;
    }

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 65535u;
    tb.TIM_Prescaler = 0u;
    tb.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(e->reg, &tb);

    TIM_ICStructInit(&ic);
    ic.TIM_Channel = TIM_Channel_1;
    ic.TIM_ICFilter = 0x0Fu;
    TIM_ICInit(e->reg, &ic);
    ic.TIM_Channel = TIM_Channel_2;
    ic.TIM_ICFilter = 0x0Fu;
    TIM_ICInit(e->reg, &ic);

    TIM_EncoderInterfaceConfig(e->reg, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_SetCounter(e->reg, 0u);
    TIM_Cmd(e->reg, ENABLE);

    e->api.ops = &drv_encoder_ops;
    e->api.hw = e;
    return &e->api;
}
