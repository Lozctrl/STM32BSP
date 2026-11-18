/**
 * @file    adc_sensor.h
 * @brief   模拟量传感器组件（光敏电阻、电池电压、电位器等通用）。
 * @layer   Components
 */
#ifndef ADC_SENSOR_H
#define ADC_SENSOR_H

#include "bsp.h"
#include "filter.h"

#define ADC_SENSOR_AVG_N        8u
#define ADC_SENSOR_VREF_MV      3300u

typedef struct {
    bsp_adc_t *adc;
    uint16_t   raw;
    float      volts;
    float      value;       /**< 换算后的物理量 = volts * scale + offset */
    float      scale;
    float      offset;
    uint8_t    inverted;    /**< 1=值越大越"暗"（光敏电阻典型接法） */
    uint8_t    hyst_state;  /**< 带回差判断的当前状态（每实例独立） */
    flt_ma_t   avg;
    float      avg_buf[ADC_SENSOR_AVG_N];
} adc_sensor_t;

void  adc_sensor_init(adc_sensor_t *s, bsp_adc_t *adc, float scale, float offset);
void  adc_sensor_update(adc_sensor_t *s);
float adc_sensor_value(const adc_sensor_t *s);
/** @brief 带回差的阈值判断，避免阈值附近抖动 */
uint8_t adc_sensor_above(adc_sensor_t *s, float threshold, float hysteresis);

#endif /* ADC_SENSOR_H */
