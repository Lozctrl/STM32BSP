/**
 * @file    drv_adc.c
 * @brief   ADC1 单通道单次转换（HAL 后端）。
 * @layer   Drivers / stm32f103_hal
 */
#include "stm32f1xx_hal.h"
#include "drv_adc.h"
#include "drv_config.h"

#define DRV_ADC_HAL_TIMEOUT_MS  5u

typedef struct {
    bsp_adc_t          api;
    ADC_HandleTypeDef  hadc;
    uint8_t            channel;
} hal_adc_t;

static hal_adc_t s_pool[DRV_ADC_POOL_SIZE];
static uint8_t   s_used = 0u;

/* F103 的 ADC 时钟必须 <= 14MHz，这里用 PCLK2/6 = 12MHz */
static void drv_adc_clock_config(void)
{
    RCC_PeriphCLKInitTypeDef periph;

    periph.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    periph.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
    (void)HAL_RCCEx_PeriphCLKConfig(&periph);
}

/* 通道和引脚是硬绑定的：CH0~7=PA0~7，CH8=PB0，CH9=PB1 */
static void drv_adc_config_pin(uint8_t channel)
{
    GPIO_InitTypeDef gpio;

    gpio.Mode  = GPIO_MODE_ANALOG;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    if (channel <= 7u) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin = (uint16_t)(1u << channel);
        HAL_GPIO_Init(GPIOA, &gpio);
    } else if (channel == 8u) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_0;
        HAL_GPIO_Init(GPIOB, &gpio);
    } else {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_1;
        HAL_GPIO_Init(GPIOB, &gpio);
    }
}

/**
 * @brief  触发一次转换并读数
 * @param  self 句柄
 * @param  raw  输出码值
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   每次都要重新配通道，因为多个传感器可能共用 ADC1。
 *         自带超时，不会像裸写 while(EOC) 那样死等。
 */
static bsp_status_t drv_adc_read(bsp_adc_t *self, uint16_t *raw)
{
    hal_adc_t            *a = (hal_adc_t *)self;
    ADC_ChannelConfTypeDef ch;

    if (raw == NULL) {
        return BSP_ERR_PARAM;
    }

    ch.Channel      = (uint32_t)(a->channel);
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    if (HAL_ADC_ConfigChannel(&a->hadc, &ch) != HAL_OK) {
        return BSP_ERR_PARAM;
    }

    if (HAL_ADC_Start(&a->hadc) != HAL_OK) {
        return BSP_ERR_BUSY;
    }
    if (HAL_ADC_PollForConversion(&a->hadc, DRV_ADC_HAL_TIMEOUT_MS) != HAL_OK) {
        (void)HAL_ADC_Stop(&a->hadc);
        return BSP_ERR_TIMEOUT;
    }

    *raw = (uint16_t)HAL_ADC_GetValue(&a->hadc);
    (void)HAL_ADC_Stop(&a->hadc);
    return BSP_OK;
}

static uint16_t drv_adc_full_scale(bsp_adc_t *self)
{
    BSP_UNUSED(self);
    return 4095u;
}

static const bsp_adc_ops_t drv_adc_ops = { drv_adc_read, drv_adc_full_scale };

/**
 * @brief  创建 ADC 通道
 * @param  instance ADC 编号（本板只用 1）
 * @param  channel  通道号 0~9
 * @retval 句柄；参数非法 / 池满 / HAL 初始化失败返回 NULL
 * @note   F1 的 ADC 必须上电校准一次，否则读数有偏移。
 */
bsp_adc_t *drv_adc_create(uint8_t instance, uint8_t channel)
{
    hal_adc_t *a;

    if ((instance != 1u) || (channel > 9u) || (s_used >= DRV_ADC_POOL_SIZE)) {
        return NULL;
    }

    __HAL_RCC_ADC1_CLK_ENABLE();
    drv_adc_clock_config();
    drv_adc_config_pin(channel);

    a = &s_pool[s_used];
    a->channel = channel;
    a->hadc.Instance = ADC1;
    a->hadc.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    a->hadc.Init.ContinuousConvMode    = DISABLE;
    a->hadc.Init.DiscontinuousConvMode = DISABLE;
    a->hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    a->hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    a->hadc.Init.NbrOfConversion       = 1u;

    if (HAL_ADC_Init(&a->hadc) != HAL_OK) {
        return NULL;
    }

    /* 校准：只在上电做一次，之后所有通道共用 */
    (void)HAL_ADCEx_Calibration_Start(&a->hadc);

    a->api.ops = &drv_adc_ops;
    a->api.hw  = a;
    s_used++;

    return &a->api;
}
