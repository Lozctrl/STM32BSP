/**
 * @file    drv_adc.h
 * @brief   ADC 通道工厂。
 * @layer   Drivers / stm32f103
 */
#ifndef DRV_ADC_H
#define DRV_ADC_H

#include "bsp.h"

/** @brief instance = 1；channel 0~9（内部按 F103 引脚映射 PA0~PA7、PB0~PB1） */
bsp_adc_t *drv_adc_create(uint8_t instance, uint8_t channel);

#endif /* DRV_ADC_H */
