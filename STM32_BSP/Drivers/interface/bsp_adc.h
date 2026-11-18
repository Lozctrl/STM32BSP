/**
 * @file    bsp_adc.h
 * @brief   单次转换 ADC 契约。
 * @layer   Drivers / Interface
 *
 * read() 允许极短的硬件等待（一次转换 < 20us），但不允许软件延时轮询死等；
 * 连续采样/滤波应当放到 Components + Middlewares 的非阻塞节奏里做。
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

#include "bsp_types.h"

typedef struct bsp_adc bsp_adc_t;

typedef struct {
    /** 触发一次转换并取回原始码值（0~4095 或平台对应量程） */
    bsp_status_t (*read)(bsp_adc_t *self, uint16_t *raw);
    /** 原始码值满量程（如 4095），用于换算电压 */
    uint16_t     (*full_scale)(bsp_adc_t *self);
} bsp_adc_ops_t;

struct bsp_adc {
    const bsp_adc_ops_t *ops;
    void                *hw;
};

/**
 * @brief  触发一次 AD 转换并取回原始码值
 * @param  self ADC 通道句柄
 * @param  raw  输出：0~满量程（F103 是 0~4095）
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   内部允许极短的硬件等待（一次转换约十几微秒），
 *         但连续采样、滤波要放到组件的非阻塞节奏里做。
 */
BSP_INLINE bsp_status_t bsp_adc_read(bsp_adc_t *self, uint16_t *raw)
{
    return self->ops->read(self, raw);
}

/**
 * @brief  查询满量程（用来把码值换算成电压）
 * @param  self ADC 通道句柄
 * @retval 满量程码值，F103 是 4095；平台不报告时默认 4095
 * @note   电压 = raw * VREF / full_scale，VREF 一般是 3300mV。
 */
BSP_INLINE uint16_t bsp_adc_full_scale(bsp_adc_t *self)
{
    return (self->ops->full_scale != NULL) ? self->ops->full_scale(self) : 4095u;
}

#endif /* BSP_ADC_H */
