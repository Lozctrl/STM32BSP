/**
 * @file    servo.h
 * @brief   舵机组件（50Hz PWM，脉宽 500~2500us）。
 * @layer   Components
 *
 * 角度→脉宽的换算完全在组件层完成，PWM 层只认 Q16 占空比，
 * 因此换成 100Hz 的舵机或换成总线舵机时，只需改本文件的参数。
 */
#ifndef SERVO_H
#define SERVO_H

#include "bsp.h"

#define SERVO_PULSE_MIN_US      500u
#define SERVO_PULSE_MAX_US      2500u
#define SERVO_ANGLE_MIN         (-90)
#define SERVO_ANGLE_MAX         90

typedef struct {
    bsp_pwm_t *pwm;
    bsp_time_us_t period_us;    /**< 缓存周期，避免每帧调用 ops */
    uint16_t  pulse_min_us;
    uint16_t  pulse_max_us;
    int16_t   angle_min;
    int16_t   angle_max;
    int16_t   angle;
} servo_t;

bsp_status_t servo_init(servo_t *s, bsp_pwm_t *pwm,
                        uint16_t pulse_min_us, uint16_t pulse_max_us,
                        int16_t angle_min, int16_t angle_max);
void servo_set_angle(servo_t *s, int16_t angle_deg);
void servo_set_pulse(servo_t *s, uint16_t pulse_us);
int16_t servo_get_angle(const servo_t *s);

#endif /* SERVO_H */
