/**
 * @file    filter.h
 * @brief   常用数字滤波器：滑动平均、一阶低通、互补滤波（姿态融合）。
 * @layer   Middlewares
 */
#ifndef FILTER_H
#define FILTER_H

#include "bsp_types.h"

/* ------------------------------------------------------------------ */
/* 滑动平均：调用方提供存储，零动态内存                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    float  *buf;
    uint16_t cap;
    uint16_t idx;
    uint16_t filled;
    float   sum;
} flt_ma_t;

void  flt_ma_init(flt_ma_t *f, float *storage, uint16_t cap);
void  flt_ma_reset(flt_ma_t *f);
float flt_ma_push(flt_ma_t *f, float x);

/* ------------------------------------------------------------------ */
/* 一阶 RC 低通：alpha = dt / (RC + dt)                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    float alpha;
    float y;
    uint8_t primed;
} flt_lpf_t;

void  flt_lpf_init_hz(flt_lpf_t *f, float cutoff_hz, float fs_hz);
void  flt_lpf_init_alpha(flt_lpf_t *f, float alpha);
void  flt_lpf_reset(flt_lpf_t *f, float initial);
float flt_lpf_update(flt_lpf_t *f, float x);

/* ------------------------------------------------------------------ */
/* 互补滤波：陀螺积分 + 加速度计角度修正                                 */
/*   angle: 当前角度（度），返回融合后的角度                              */
/*   gyro_dps: 陀螺角速度（度/秒）                                        */
/*   acc_angle: 加速度计解算角度（度）                                    */
/*   alpha: 陀螺权重，典型 0.98；dt_s 为采样周期                          */
/* ------------------------------------------------------------------ */
typedef struct {
    float angle;
    float alpha;
    uint8_t primed;
} flt_comp_t;

void  flt_comp_init(flt_comp_t *f, float alpha);
void  flt_comp_reset(flt_comp_t *f, float angle_deg);
float flt_comp_update(flt_comp_t *f, float gyro_dps, float acc_angle_deg, float dt_s);

#endif /* FILTER_H */
