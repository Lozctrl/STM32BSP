/**
 * @file    servo.c
 * @brief   舵机组件实现。
 * @layer   Components
 */
#include "servo.h"

/**
 * @brief  初始化舵机
 * @param  s            对象
 * @param  pwm          PWM 通道（必须是 50Hz 那种）
 * @param  pulse_min_us 最小角度对应的脉宽（常见 500us）
 * @param  pulse_max_us 最大角度对应的脉宽（常见 2500us）
 * @param  angle_min    机械最小角度（常见 -90）
 * @param  angle_max    机械最大角度（常见 +90）
 * @retval BSP_OK / BSP_ERR_PARAM（PWM 报不出周期就会失败）
 * @note   为什么必须要 PWM 能报周期？因为角度最终要换算成占空比，
 *         而占空比 = 脉宽 / 周期。周期不知道就没法算。
 */
bsp_status_t servo_init(servo_t *s, bsp_pwm_t *pwm,
                        uint16_t pulse_min_us, uint16_t pulse_max_us,
                        int16_t angle_min, int16_t angle_max)
{
    if ((s == NULL) || (pwm == NULL) || (pulse_min_us >= pulse_max_us) ||
        (angle_min >= angle_max)) {
        return BSP_ERR_PARAM;
    }

    s->pwm = pwm;
    s->period_us = bsp_pwm_period_us(pwm);
    s->pulse_min_us = pulse_min_us;
    s->pulse_max_us = pulse_max_us;
    s->angle_min = angle_min;
    s->angle_max = angle_max;
    s->angle = 0;

    if (s->period_us == 0u) {
        return BSP_ERR_PARAM;       /* PWM 层必须能报出周期，否则无法换算 */
    }

    bsp_pwm_enable(pwm, 1u);
    servo_set_angle(s, 0);
    return BSP_OK;
}

/**
 * @brief  直接按脉宽控制舵机（跳过角度换算）
 * @param  s        对象
 * @param  pulse_us 脉宽（会被限制在 min~max 之间）
 */
void servo_set_pulse(servo_t *s, uint16_t pulse_us)
{
    uint32_t q16;

    if ((s == NULL) || (s->pwm == NULL)) {
        return;
    }

    pulse_us = (uint16_t)BSP_CLAMP(pulse_us, s->pulse_min_us, s->pulse_max_us);
    q16 = ((uint32_t)pulse_us * 65535u) / s->period_us;
    (void)bsp_pwm_set_duty_q16(s->pwm, (uint16_t)BSP_MIN(q16, 65535u));
}

/**
 * @brief  按角度控制舵机
 * @param  s         对象
 * @param  angle_deg 角度（会被限制在机械范围内）
 * @note   先线性映射成脉宽，再换算成 Q16 占空比，两步都是整数运算，不用浮点。
 */
void servo_set_angle(servo_t *s, int16_t angle_deg)
{
    int32_t span_angle;
    int32_t span_pulse;
    int32_t pulse;

    if (s == NULL) {
        return;
    }

    angle_deg = (int16_t)BSP_CLAMP(angle_deg, s->angle_min, s->angle_max);
    s->angle = angle_deg;

    span_angle = (int32_t)s->angle_max - (int32_t)s->angle_min;
    span_pulse = (int32_t)s->pulse_max_us - (int32_t)s->pulse_min_us;
    pulse = (int32_t)s->pulse_min_us
          + (span_pulse * ((int32_t)angle_deg - (int32_t)s->angle_min)) / span_angle;

    servo_set_pulse(s, (uint16_t)BSP_CLAMP(pulse, (int32_t)s->pulse_min_us,
                                           (int32_t)s->pulse_max_us));
}

/**
 * @brief  读回当前角度
 * @param  s 对象
 * @retval 角度
 */
int16_t servo_get_angle(const servo_t *s)
{
    return (s != NULL) ? s->angle : 0;
}
