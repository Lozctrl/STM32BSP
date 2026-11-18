/**
 * @file    bsp_encoder.h
 * @brief   正交编码器契约（硬件编码器接口或软件解码均可实现）。
 * @layer   Drivers / Interface
 */
#ifndef BSP_ENCODER_H
#define BSP_ENCODER_H

#include "bsp_types.h"

typedef struct bsp_encoder bsp_encoder_t;

typedef struct {
    /** 读取并清零增量计数（原子操作，主循环安全） */
    int16_t  (*read_delta)(bsp_encoder_t *self);
    /** 读取累计计数（不清零） */
    int32_t  (*read_total)(bsp_encoder_t *self);
    /** 计数清零 */
    void     (*reset)(bsp_encoder_t *self);
} bsp_encoder_ops_t;

struct bsp_encoder {
    const bsp_encoder_ops_t *ops;
    void                    *hw;
};

/**
 * @brief  读"这段时间转了多少"，读完自动清零
 * @param  self 编码器句柄
 * @retval 有符号增量（正=正转，负=反转）
 * @note   为什么用 int16？一次采样周期内的增量本来就很小，16 位够用还省 RAM。
 *         10ms 内要超过 32767 个计数才会溢出，正常电机碰不到。
 */
BSP_INLINE int16_t bsp_encoder_read_delta(bsp_encoder_t *self)
{
    return self->ops->read_delta(self);
}

/**
 * @brief  读当前累计计数值（不清零）
 * @param  self 编码器句柄
 * @retval 计数值
 * @note   硬件计数器只有 16 位，到 65535 会回绕。
 *         真正要"总里程"请用 encoder 组件里的 encoder_total()（软件累加）。
 */
BSP_INLINE int32_t bsp_encoder_read_total(bsp_encoder_t *self)
{
    return self->ops->read_total(self);
}

/**
 * @brief  把计数器清零
 * @param  self 编码器句柄
 * @note   现场标定、或者上电第一次跑之前调一下，别把上次的残值带进来。
 */
BSP_INLINE void bsp_encoder_reset(bsp_encoder_t *self)
{
    self->ops->reset(self);
}

#endif /* BSP_ENCODER_H */
