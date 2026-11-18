/**
 * @file    drv_adc.c
 * @brief   ADC1 单通道单次转换实现。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_adc.h"
#include "drv_config.h"

#define DRV_ADC_TIMEOUT     50000u

typedef struct {
    bsp_adc_t  api;
    uint8_t    channel;
    uint8_t    initialized;
} stm32_adc_t;

static stm32_adc_t s_pool[DRV_ADC_POOL_SIZE];
static uint8_t     s_used = 0u;

/** @brief F103 的通道→引脚是固定的：CH0~7=PA0~7，CH8=PB0，CH9=PB1 */

/* F103 的 ADC 通道和引脚是硬绑定的：
   CH0~7 = PA0~PA7，CH8 = PB0，CH9 = PB1。
   这里按通道号反查引脚，调用方只需要说"我要 CH4" */
static void drv_adc_config_pin(uint8_t channel)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Mode = GPIO_Mode_AIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    if (channel <= 7u) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
        gpio.GPIO_Pin = (uint16_t)(1u << channel);
        GPIO_Init(GPIOA, &gpio);
    } else if (channel == 8u) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        gpio.GPIO_Pin = GPIO_Pin_0;
        GPIO_Init(GPIOB, &gpio);
    } else {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        gpio.GPIO_Pin = GPIO_Pin_1;
        GPIO_Init(GPIOB, &gpio);
    }
}

/**
 * @brief  触发一次转换并把结果读回来
 * @param  self 句柄
 * @param  raw  输出码值
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   每次都要重新配一遍通道，因为多个传感器可能共用 ADC1。
 *         自带超时上限，转不出来就返回错误，不会死等。
 */
static bsp_status_t drv_adc_read(bsp_adc_t *self, uint16_t *raw)
{
    stm32_adc_t *a = (stm32_adc_t *)self;
    uint32_t guard;

    if (raw == NULL) {
        return BSP_ERR_PARAM;
    }

    ADC_RegularChannelConfig(ADC1, (uint8_t)(ADC_Channel_0 + a->channel), 1u,
                             ADC_SampleTime_55Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    guard = DRV_ADC_TIMEOUT;
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) {
        if (--guard == 0u) {
            return BSP_ERR_TIMEOUT;
        }
    }
    *raw = ADC_GetConversionValue(ADC1);
    return BSP_OK;
}

/* F103 是 12 位 ADC，满量程 4095 */
static uint16_t drv_adc_full_scale(bsp_adc_t *self)
{
    BSP_UNUSED(self);
    return 4095u;
}

static const bsp_adc_ops_t drv_adc_ops = { drv_adc_read, drv_adc_full_scale };

/**
 * @brief  创建一个 ADC 通道
 * @param  instance ADC 编号（本板只用 1）
 * @param  channel  通道号 0~9
 * @retval 句柄；参数非法或池满返回 NULL
 * @note   每次创建通道都会校准一次（F1 校准只要几十微秒），所有通道共用 ADC1。
 *         校准要等硬件完成，所以循环里带超时保护。
 */
bsp_adc_t *drv_adc_create(uint8_t instance, uint8_t channel)
{
    stm32_adc_t *a;
    ADC_InitTypeDef cfg;

    if ((instance != 1u) || (channel > 9u) || (s_used >= DRV_ADC_POOL_SIZE)) {
        return NULL;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);       /* 72/6 = 12MHz，≤14MHz 上限 */

    drv_adc_config_pin(channel);

    cfg.ADC_Mode = ADC_Mode_Independent;
    cfg.ADC_ScanConvMode = DISABLE;
    cfg.ADC_ContinuousConvMode = DISABLE;
    cfg.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    cfg.ADC_DataAlign = ADC_DataAlign_Right;
    cfg.ADC_NbrOfChannel = 1u;
    ADC_Init(ADC1, &cfg);
    ADC_Cmd(ADC1, ENABLE);

    /* 校准：每次创建通道执行一次，之后所有通道共用这颗 ADC1 */
    ADC_ResetCalibration(ADC1);
    {
        uint32_t guard = DRV_ADC_TIMEOUT;
        while ((ADC_GetResetCalibrationStatus(ADC1) == SET) && (--guard != 0u)) {
        }
    }
    ADC_StartCalibration(ADC1);
    {
        uint32_t guard = DRV_ADC_TIMEOUT;
        while ((ADC_GetCalibrationStatus(ADC1) == SET) && (--guard != 0u)) {
        }
    }

    a = &s_pool[s_used];
    s_used++;
    a->channel = channel;
    a->initialized = 1u;
    a->api.ops = &drv_adc_ops;
    a->api.hw = a;

    return &a->api;
}
