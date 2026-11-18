/**
 * @file    adc_sensor.c
 * @brief   模拟量传感器组件实现。
 * @layer   Components
 */
#include "adc_sensor.h"

/**
 * @brief  初始化模拟量传感器
 * @param  s      对象
 * @param  adc    ADC 通道（注入）
 * @param  scale  换算系数：物理量 = 电压(V) × scale + offset
 * @param  offset 换算偏移
 * @note   例：光敏想显示 0~100 的"亮度百分比"，
 *         就设 scale = 100 / 3.3 ≈ 30.3，offset = 0。
 */
void adc_sensor_init(adc_sensor_t *s, bsp_adc_t *adc, float scale, float offset)
{
    if ((s == NULL) || (adc == NULL)) {
        return;
    }
    s->adc = adc;
    s->raw = 0u;
    s->volts = 0.0f;
    s->value = 0.0f;
    s->scale = scale;
    s->offset = offset;
    s->inverted = 0u;
    s->hyst_state = 0u;
    flt_ma_init(&s->avg, s->avg_buf, ADC_SENSOR_AVG_N);
}

/**
 * @brief  采一次样，更新 raw / volts / value
 * @param  s 对象
 * @note   内部先把 mV 送进滑动平均，再换算成 V。
 *         先平均再缩放，比先缩放再平均更省浮点误差。
 */
void adc_sensor_update(adc_sensor_t *s)
{
    uint16_t full;
    float v;

    if ((s == NULL) || (s->adc == NULL)) {
        return;
    }

    if (bsp_adc_read(s->adc, &s->raw) != BSP_OK) {
        return;
    }

    full = bsp_adc_full_scale(s->adc);
    if (full == 0u) {
        return;
    }

    v = ((float)s->raw * (float)ADC_SENSOR_VREF_MV) / (float)full;
    s->volts = flt_ma_push(&s->avg, v) / 1000.0f;    /* 先对 mV 做平均，减少噪声 */
    s->value = (s->volts * s->scale) + s->offset;
}

/**
 * @brief  读最近一次的换算结果
 * @param  s 对象
 * @retval 物理量（单位由 scale 决定）
 */
float adc_sensor_value(const adc_sensor_t *s)
{
    return (s != NULL) ? s->value : 0.0f;
}

/**
 * @brief  带"回差"的阈值判断
 * @param  s          对象
 * @param  threshold  阈值
 * @param  hysteresis 回差宽度
 * @retval 1=当前在阈值上方（稳定），0=在下方
 * @note   为什么要回差？因为如果只在阈值上比较，
 *         数值在阈值附近抖一下就会疯狂开关，加 0.1V 回差就稳了。
 */
uint8_t adc_sensor_above(adc_sensor_t *s, float threshold, float hysteresis)
{
    float v;

    if (s == NULL) {
        return 0u;
    }
    v = s->value;
    if (v > (threshold + hysteresis)) {
        s->hyst_state = 1u;
    } else if (v < (threshold - hysteresis)) {
        s->hyst_state = 0u;
    }
    return s->hyst_state;
}
