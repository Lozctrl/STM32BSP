/**
 * @file    filter.c
 * @brief   滤波器实现。
 * @layer   Middlewares
 */
#include "filter.h"
#include <math.h>

/**
 * @brief  初始化滑动平均滤波器
 * @param  f       滤波器对象
 * @param  storage 外部提供的数组，长度 cap（本框架不动态分配内存）
 * @param  cap     窗口长度：越大越平滑，但响应越慢
 */
void flt_ma_init(flt_ma_t *f, float *storage, uint16_t cap)
{
    uint16_t i;
    if ((f == NULL) || (storage == NULL) || (cap == 0u)) {
        return;
    }
    f->buf = storage;
    f->cap = cap;
    f->idx = 0u;
    f->filled = 0u;
    f->sum = 0.0f;
    for (i = 0u; i < cap; i++) {
        storage[i] = 0.0f;
    }
}

/**
 * @brief  清空滑动平均的窗口和累加和
 * @param  f 滤波器对象
 */
void flt_ma_reset(flt_ma_t *f)
{
    uint16_t i;
    if (f == NULL) {
        return;
    }
    f->idx = 0u;
    f->filled = 0u;
    f->sum = 0.0f;
    for (i = 0u; i < f->cap; i++) {
        f->buf[i] = 0.0f;
    }
}

/**
 * @brief  塞一个新值进去，返回当前平均值
 * @param  f 滤波器对象
 * @param  x 新采样值
 * @retval 窗口内的平均值（窗口没填满时按已填个数平均，不会偏小）
 * @note   用"加新值减旧值"维护累加和，所以每次只要一次加一次减，很快。
 */
float flt_ma_push(flt_ma_t *f, float x)
{
    if ((f == NULL) || (f->buf == NULL) || (f->cap == 0u)) {
        return x;
    }
    f->sum -= f->buf[f->idx];
    f->buf[f->idx] = x;
    f->sum += x;
    f->idx++;
    if (f->idx >= f->cap) {
        f->idx = 0u;
    }
    if (f->filled < f->cap) {
        f->filled++;
    }
    return f->sum / (float)f->filled;
}

/**
 * @brief  直接指定滤波系数 alpha
 * @param  f     滤波器对象
 * @param  alpha 0~1，越大越相信新值（越不平滑）
 */
void flt_lpf_init_alpha(flt_lpf_t *f, float alpha)
{
    if (f == NULL) {
        return;
    }
    f->alpha = BSP_CLAMP(alpha, 0.0f, 1.0f);
    f->y = 0.0f;
    f->primed = 0u;
}

/**
 * @brief  按"截止频率"初始化一阶低通
 * @param  f         滤波器对象
 * @param  cutoff_hz 截止频率（低于它的信号能过，高于它的被压掉）
 * @param  fs_hz     采样频率（比如 100Hz 的环就传 100）
 * @note   内部算出 alpha = dt / (RC + dt)，这是标准的一阶 RC 离散化。
 */
void flt_lpf_init_hz(flt_lpf_t *f, float cutoff_hz, float fs_hz)
{
    float rc;
    float dt;
    if ((f == NULL) || (cutoff_hz <= 0.0f) || (fs_hz <= 0.0f)) {
        if (f != NULL) {
            flt_lpf_init_alpha(f, 0.0f);
        }
        return;
    }
    dt = 1.0f / fs_hz;
    rc = 1.0f / (2.0f * 3.14159265f * cutoff_hz);
    flt_lpf_init_alpha(f, dt / (rc + dt));
}

/**
 * @brief  把滤波器输出直接置为某个值（并标记已初始化）
 * @param  f       滤波器对象
 * @param  initial 初始值
 * @note   上电时先 reset 成当前真实值，可以避免第一拍从 0 冲上去。
 */
void flt_lpf_reset(flt_lpf_t *f, float initial)
{
    if (f == NULL) {
        return;
    }
    f->y = initial;
    f->primed = 1u;
}

/**
 * @brief  喂一个新值，返回滤波后的结果
 * @param  f 滤波器对象
 * @param  x 新采样值
 * @retval 滤波后的值
 * @note   第一次调用会直接返回 x（不滤波），避免从 0 慢慢爬上来。
 */
float flt_lpf_update(flt_lpf_t *f, float x)
{
    if (f == NULL) {
        return x;
    }
    if (f->primed == 0u) {
        f->y = x;
        f->primed = 1u;
        return f->y;
    }
    f->y += f->alpha * (x - f->y);
    return f->y;
}

/**
 * @brief  初始化互补滤波器（用来算倾角）
 * @param  f     滤波器对象
 * @param  alpha 陀螺权重，典型 0.96~0.98；越大越相信陀螺、越少看加速度计
 */
void flt_comp_init(flt_comp_t *f, float alpha)
{
    if (f == NULL) {
        return;
    }
    f->angle = 0.0f;
    f->alpha = BSP_CLAMP(alpha, 0.0f, 1.0f);
    f->primed = 0u;
}

/**
 * @brief  把角度直接置为某个值
 * @param  f         滤波器对象
 * @param  angle_deg 角度（度）
 */
void flt_comp_reset(flt_comp_t *f, float angle_deg)
{
    if (f == NULL) {
        return;
    }
    f->angle = angle_deg;
    f->primed = 1u;
}

/**
 * @brief  互补滤波一步：陀螺积分 + 加速度计修正
 * @param  f              滤波器对象
 * @param  gyro_dps       陀螺角速度（度/秒）
 * @param  acc_angle_deg  加速度计算出的角度（度）
 * @param  dt_s           采样周期（秒）
 * @retval 融合后的角度（度）
 * @note   为什么这样融合？陀螺短期准但会漂，加速度计长期准但振动时乱跳。
 *         alpha 取 0.96 就是"短期信陀螺 96%，长期靠加速度计拉回来"。
 */
float flt_comp_update(flt_comp_t *f, float gyro_dps, float acc_angle_deg, float dt_s)
{
    if (f == NULL) {
        return 0.0f;
    }
    if (f->primed == 0u) {
        f->angle = acc_angle_deg;
        f->primed = 1u;
        return f->angle;
    }
    f->angle = f->alpha * (f->angle + (gyro_dps * dt_s))
             + (1.0f - f->alpha) * acc_angle_deg;
    return f->angle;
}
