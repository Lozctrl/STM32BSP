/**
 * @file    motor.c
 * @brief   TB6612 电机组件实现。
 * @layer   Components
 */
#include "motor.h"

/* 写两个方向脚。TB6612 的真值表：
     IN1=1 IN2=0 -> 正转
     IN1=0 IN2=1 -> 反转
     IN1=IN2=0   -> 停转（电机两端悬空，靠惯性滑行）
     IN1=IN2=1   -> 刹车（电机两端短接，很快停下）  */
static void motor_write_dir(motor_t *m, uint8_t in1, uint8_t in2)
{
    bsp_gpio_write(m->in1, (in1 != 0u) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW);
    bsp_gpio_write(m->in2, (in2 != 0u) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW);
}

/**
 * @brief  初始化一路 TB6612 电机
 * @param  m    对象
 * @param  pwm  速度通道（注入进来，谁产生 PWM 由 board.c 决定）
 * @param  in1  方向脚 1
 * @param  in2  方向脚 2
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   方向脚设成推挽输出，PWM 先给 0，电机保持不动。
 */
bsp_status_t motor_init(motor_t *m, bsp_pwm_t *pwm, bsp_gpio_t *in1, bsp_gpio_t *in2)
{
    if ((m == NULL) || (pwm == NULL) || (in1 == NULL) || (in2 == NULL)) {
        return BSP_ERR_PARAM;
    }

    m->pwm = pwm;
    m->in1 = in1;
    m->in2 = in2;
    m->speed = 0;
    m->mode = MOTOR_MODE_COAST;
    m->invert = 0u;

    (void)bsp_gpio_set_mode(in1, BSP_GPIO_MODE_OUT_PP);
    (void)bsp_gpio_set_mode(in2, BSP_GPIO_MODE_OUT_PP);
    motor_write_dir(m, 0u, 0u);
    (void)bsp_pwm_set_duty_q16(pwm, 0u);
    bsp_pwm_enable(pwm, 1u);
    return BSP_OK;
}

/**
 * @brief  设定转速
 * @param  m        对象
 * @param  permille 千分比：+1000=正转最快，-1000=反转最快，0=停
 * @note   正负号决定方向，绝对值决定快慢。
 *         如果这路电机是镜像装的（左右轮对调），把 m->invert 置 1 就行，
 *         不用在业务代码里到处乘 -1。
 */
void motor_set_speed(motor_t *m, int16_t permille)
{
    int16_t cmd;
    uint16_t duty;

    if (m == NULL) {
        return;
    }

    cmd = (m->invert != 0u) ? (int16_t)(-permille) : permille;
    cmd = (int16_t)BSP_CLAMP(cmd, (int16_t)MOTOR_SPEED_MIN, (int16_t)MOTOR_SPEED_MAX);
    m->speed = cmd;
    m->mode = MOTOR_MODE_RUN;

    if (cmd > 0) {
        motor_write_dir(m, 1u, 0u);
        duty = (uint16_t)cmd;
    } else if (cmd < 0) {
        motor_write_dir(m, 0u, 1u);
        duty = (uint16_t)(-cmd);
    } else {
        motor_write_dir(m, 0u, 0u);
        duty = 0u;
    }

    (void)bsp_pwm_set_duty_permille(m->pwm, duty);
}

/**
 * @brief  滑行停车：断开电机，靠惯性慢慢停
 * @param  m 对象
 * @note   机器人转弯时常用"一侧滑行"，比急刹更顺。
 */
void motor_coast(motor_t *m)
{
    if (m == NULL) {
        return;
    }
    m->speed = 0;
    m->mode = MOTOR_MODE_COAST;
    motor_write_dir(m, 0u, 0u);
    (void)bsp_pwm_set_duty_q16(m->pwm, 0u);
}

/**
 * @brief  刹车：电机两端短接，快速停下
 * @param  m 对象
 * @note   注意刹车时 PWM 是给满的，靠 TB6612 内部把电机短接来制动。
 */
void motor_brake(motor_t *m)
{
    if (m == NULL) {
        return;
    }
    m->speed = 0;
    m->mode = MOTOR_MODE_BRAKE;
    motor_write_dir(m, 1u, 1u);
    (void)bsp_pwm_set_duty_q16(m->pwm, 0xFFFFu);
}

/**
 * @brief  读回当前设定的转速
 * @param  m 对象
 * @retval 千分比
 */
int16_t motor_get_speed(const motor_t *m)
{
    return (m != NULL) ? m->speed : 0;
}
