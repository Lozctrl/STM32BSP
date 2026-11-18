/**
 * @file    ramp.h
 * @brief   斜率限制器（Slew Rate Limiter）：给电机、舵机加"加速度上限"，
 *          避免设定值阶跃导致电流冲击与机械抖动。
 * @layer   Middlewares
 */
#ifndef RAMP_H
#define RAMP_H

#include "bsp_types.h"

typedef struct {
    float value;
    float up_per_s;     /**< 每秒最大上升量 */
    float down_per_s;   /**< 每秒最大下降量 */
} ramp_t;

void  ramp_init(ramp_t *r, float up_per_s, float down_per_s);
void  ramp_reset(ramp_t *r, float value);
float ramp_update(ramp_t *r, float target, float dt_s);

#endif /* RAMP_H */
