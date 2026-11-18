/**
 * @file    ramp.c
 * @brief   斜率限制器实现。
 * @layer   Middlewares
 */
#include "ramp.h"

/**
 * @brief  初始化斜率限制器
 * @param  r          对象
 * @param  up_per_s   每秒最多上升多少（单位跟你的目标值一致）
 * @param  down_per_s 每秒最多下降多少（一般比上升快一点）
 */
void ramp_init(ramp_t *r, float up_per_s, float down_per_s)
{
    if (r == NULL) {
        return;
    }
    r->value = 0.0f;
    r->up_per_s = (up_per_s > 0.0f) ? up_per_s : 0.0f;
    r->down_per_s = (down_per_s > 0.0f) ? down_per_s : 0.0f;
}

/**
 * @brief  把当前值直接设成 value（不经过斜坡）
 * @param  r     对象
 * @param  value 想从哪个值开始
 */
void ramp_reset(ramp_t *r, float value)
{
    if (r != NULL) {
        r->value = value;
    }
}

/**
 * @brief  朝目标值靠近一步，但每步不超过"速率 × dt"
 * @param  r      对象
 * @param  target 目标值
 * @param  dt_s   距上次调用的时间（秒）
 * @retval 限制后的当前值（可以直接拿去驱动电机）
 * @note   作用：设定值突然从 0 跳到 1000 时，电机不会瞬间满占空比，
 *         电流冲击小、齿轮不容易崩、轮胎也不容易打滑。
 */
float ramp_update(ramp_t *r, float target, float dt_s)
{
    float delta;
    float max_up;
    float max_down;

    if (r == NULL) {
        return target;
    }
    if (dt_s <= 0.0f) {
        return r->value;
    }

    delta = target - r->value;
    max_up = r->up_per_s * dt_s;
    max_down = r->down_per_s * dt_s;

    if (delta > max_up) {
        delta = max_up;
    } else if (delta < -max_down) {
        delta = -max_down;
    }

    r->value += delta;
    return r->value;
}
