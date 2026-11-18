/**
 * @file    motor.h
 * @brief   TB6612 单通道直流电机组件。
 * @layer   Components
 *
 * 依赖倒置示范：
 *   原教程 Motor_SetSpeed() 里直接 GPIO_SetBits + PWM_SetCompare3。
 *   本组件只持有 bsp_pwm_t* 与两个 bsp_gpio_t*，
 *   "谁产生 PWM"（TIM2_CH3 / TIM3_CH2 / 外部 PWM 芯片）与组件无关。
 */
#ifndef MOTOR_H
#define MOTOR_H

#include "bsp.h"

#define MOTOR_SPEED_MAX     1000    /* 千分比满量程 */
#define MOTOR_SPEED_MIN     (-1000)

typedef enum {
    MOTOR_MODE_COAST = 0,   /**< 惰行：IN1=IN2=0，PWM=0 */
    MOTOR_MODE_RUN,         /**< 正常运行 */
    MOTOR_MODE_BRAKE        /**< 刹车：IN1=IN2=1，PWM=满 */
} motor_mode_t;

typedef struct {
    bsp_pwm_t  *pwm;        /**< 速度执行器（注入） */
    bsp_gpio_t *in1;        /**< 方向脚 1 */
    bsp_gpio_t *in2;        /**< 方向脚 2 */
    int16_t     speed;      /**< -1000 ~ +1000 */
    motor_mode_t mode;
    uint8_t     invert;     /**< 机械安装方向修正 */
} motor_t;

bsp_status_t motor_init(motor_t *m, bsp_pwm_t *pwm, bsp_gpio_t *in1, bsp_gpio_t *in2);
void         motor_set_speed(motor_t *m, int16_t permille);
void         motor_coast(motor_t *m);
void         motor_brake(motor_t *m);
int16_t      motor_get_speed(const motor_t *m);

#endif /* MOTOR_H */
