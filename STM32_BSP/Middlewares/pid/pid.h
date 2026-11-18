/**
 * @file    pid.h
 * @brief   位置式/增量式通用 PID（纯算法，无任何硬件依赖）。
 * @layer   Middlewares
 *
 * 特性：
 *   - 微分先行（可选 on-measurement），避免设定值阶跃引起的微分冲击
 *   - 积分限幅 + 反算抗饱和（conditional integration）
 *   - 输出限幅、dt 合法性保护（防止首拍/丢拍导致 D 项爆炸）
 *   - 全 float 实现；在无 FPU 的 M3 上建议 50~100Hz 调用
 */
#ifndef PID_H
#define PID_H

#include "bsp_types.h"

#define PID_DEFAULT_DT_MIN_S    0.001f
#define PID_DEFAULT_DT_MAX_S    0.200f

typedef enum {
    PID_FLAG_D_ON_MEASUREMENT = 0x01,   /**< D 项对测量值求导（推荐用于速度环） */
    PID_FLAG_ANTI_WINDUP      = 0x02    /**< 输出饱和时停止积分（推荐） */
} pid_flag_t;

typedef struct {
    /* ---- 配置 ---- */
    float   kp;
    float   ki;
    float   kd;
    float   out_min;
    float   out_max;
    float   integ_min;
    float   integ_max;
    float   dt_min_s;
    float   dt_max_s;
    uint8_t flags;

    /* ---- 状态 ---- */
    float   integral;
    float   prev_meas;
    float   prev_err;
    float   out;
    uint8_t primed;         /**< 是否已完成首拍初始化 */
    uint32_t saturation_cnt;/**< 饱和次数统计（调参用） */
} pid_t;

void  pid_init(pid_t *pid, float kp, float ki, float kd,
               float out_min, float out_max);
void  pid_set_limits(pid_t *pid, float out_min, float out_max,
                     float integ_min, float integ_max);
void  pid_set_flags(pid_t *pid, uint8_t flags);
void  pid_reset(pid_t *pid);
float pid_update(pid_t *pid, float setpoint, float measurement, float dt_s);
float pid_output(const pid_t *pid);

#endif /* PID_H */
