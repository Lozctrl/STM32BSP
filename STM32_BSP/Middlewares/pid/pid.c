/**
 * @file    pid.c
 * @brief   PID 实现。
 * @layer   Middlewares
 */
#include "pid.h"

/* 把 v 限制在 [lo, hi] 之间。写成小函数是为了让主逻辑读起来像公式 */
static float pid_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/**
 * @brief  PID 初始化：设好参数、清空内部状态
 * @param  pid     要初始化的 PID 对象
 * @param  kp ki kd 三个系数
 * @param  out_min/out_max 输出限幅（比如电机是 -1000~1000 千分比）
 * @note   默认打开"微分先行"和"抗积分饱和"两个开关，这两个几乎总是要开的。
 */
void pid_init(pid_t *pid, float kp, float ki, float kd,
              float out_min, float out_max)
{
    if (pid == NULL) {
        return;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid->integ_min = out_min;
    pid->integ_max = out_max;
    pid->dt_min_s = PID_DEFAULT_DT_MIN_S;
    pid->dt_max_s = PID_DEFAULT_DT_MAX_S;
    pid->flags = (uint8_t)(PID_FLAG_D_ON_MEASUREMENT | PID_FLAG_ANTI_WINDUP);
    pid_reset(pid);
}

/**
 * @brief  单独修改输出限幅 / 积分限幅
 * @param  pid  PID 对象
 * @param  out_min/out_max     输出上下限
 * @param  integ_min/integ_max 积分项上下限
 * @note   调参时只改限幅不用重新 init，更方便。
 */
void pid_set_limits(pid_t *pid, float out_min, float out_max,
                    float integ_min, float integ_max)
{
    if (pid == NULL) {
        return;
    }
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid->integ_min = integ_min;
    pid->integ_max = integ_max;
    pid->integral = pid_clampf(pid->integral, integ_min, integ_max);
}

/**
 * @brief  修改行为开关
 * @param  pid   PID 对象
 * @param  flags PID_FLAG_D_ON_MEASUREMENT / PID_FLAG_ANTI_WINDUP 的组合
 */
void pid_set_flags(pid_t *pid, uint8_t flags)
{
    if (pid != NULL) {
        pid->flags = flags;
    }
}

/**
 * @brief  清空积分、微分历史、输出，回到"刚上电"的状态
 * @param  pid PID 对象
 * @note   切换模式、重新起步、长按标定时调用，避免带着旧积分冲出去。
 */
void pid_reset(pid_t *pid)
{
    if (pid == NULL) {
        return;
    }
    pid->integral = 0.0f;
    pid->prev_meas = 0.0f;
    pid->prev_err = 0.0f;
    pid->out = 0.0f;
    pid->primed = 0u;
    pid->saturation_cnt = 0u;
}

/**
 * @brief  只读上一次算出来的输出，不重新计算
 * @param  pid PID 对象
 * @retval 上次的输出值
 */
float pid_output(const pid_t *pid)
{
    return (pid != NULL) ? pid->out : 0.0f;
}

/**
 * @brief  PID 核心：喂进"目标值"和"当前值"，吐出控制量
 * @param  pid          PID 对象
 * @param  setpoint     目标值（比如想要 1200 counts/s）
 * @param  measurement  当前测量值（编码器算出来的实际速度）
 * @param  dt_s         距上次调用的时间（秒）。必须传真实值，不要写死 0.01
 * @retval 控制量，已按 out_min/out_max 限幅
 * @note   dt_s 传错是这个函数最常见的 bug 来源：
 *         调度抖动、OLED 刷新都会让周期变长，所以调用方应该用
 *         "本次 now - 上次 now" 算 dt，而不是假设固定周期。
 */
float pid_update(pid_t *pid, float setpoint, float measurement, float dt_s)
{
    float err;
    float p_term;
    float d_term = 0.0f;
    float i_candidate;
    float out;

    if (pid == NULL) {
        return 0.0f;
    }

    /* dt 卫生检查：首拍、超时丢拍、异常值一律走"无微分"分支 */
    if ((dt_s < pid->dt_min_s) || (dt_s > pid->dt_max_s)) {
        dt_s = pid->dt_min_s;
        pid->primed = 0u;
    }

    err = setpoint - measurement;

    if (pid->primed == 0u) {
        pid->prev_err = err;
        pid->prev_meas = measurement;
        pid->primed = 1u;
        d_term = 0.0f;
    } else if ((pid->flags & PID_FLAG_D_ON_MEASUREMENT) != 0u) {
        /* 微分先行：对测量值求导并取负，抑制设定值突变 */
        d_term = -pid->kd * (measurement - pid->prev_meas) / dt_s;
    } else {
        d_term = pid->kd * (err - pid->prev_err) / dt_s;
    }

    p_term = pid->kp * err;

    /* 条件积分抗饱和：先算输出，饱和方向一致时冻结积分 */
    i_candidate = pid->integral + pid->ki * err * dt_s;
    i_candidate = pid_clampf(i_candidate, pid->integ_min, pid->integ_max);

    out = p_term + i_candidate + d_term;
    if (out > pid->out_max) {
        out = pid->out_max;
        pid->saturation_cnt++;
        /*
         * 条件积分：输出顶到上限时，只有"能减小输出"的误差方向才允许继续积分。
         * err > 0 会把输出推得更高，必须冻结积分，否则退出饱和要等很久。
         */
        if (((pid->flags & PID_FLAG_ANTI_WINDUP) == 0u) || (err < 0.0f)) {
            pid->integral = i_candidate;
        }
    } else if (out < pid->out_min) {
        out = pid->out_min;
        pid->saturation_cnt++;
        if (((pid->flags & PID_FLAG_ANTI_WINDUP) == 0u) || (err > 0.0f)) {
            pid->integral = i_candidate;
        }
    } else {
        pid->integral = i_candidate;
    }

    pid->prev_err = err;
    pid->prev_meas = measurement;
    pid->out = out;
    return out;
}
