/**
 * @file    bsp_irq.h
 * @brief   外部中断/边沿事件契约。用于对射式红外、霍尔、按键唤醒等"边沿计数"场景。
 * @layer   Drivers / Interface
 *
 * 约定：回调运行在中断上下文，只允许置标志/推队列，禁止调用任何会阻塞的函数。
 */
#ifndef BSP_IRQ_H
#define BSP_IRQ_H

#include "bsp_gpio.h"

typedef struct bsp_exti bsp_exti_t;

typedef enum {
    BSP_EDGE_RISING = 0,
    BSP_EDGE_FALLING,
    BSP_EDGE_BOTH
} bsp_edge_t;

typedef void (*bsp_irq_cb_t)(void *ctx);

typedef struct {
    bsp_status_t (*attach)(bsp_exti_t *self, bsp_gpio_t *pin, bsp_edge_t edge,
                           bsp_irq_cb_t cb, void *ctx);
    bsp_status_t (*detach)(bsp_exti_t *self, bsp_gpio_t *pin);
} bsp_exti_ops_t;

struct bsp_exti {
    const bsp_exti_ops_t *ops;
    void                 *hw;
};

/**
 * @brief  给某个引脚挂一个边沿中断回调
 * @param  self 中断管理器句柄
 * @param  pin  哪个引脚（gpio 句柄）
 * @param  edge 上升沿 / 下降沿 / 双边沿
 * @param  cb   回调函数
 * @param  ctx  回调时原样传回的上下文指针
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   回调跑在中断里，只允许"改个标志 / 加个计数"，
 *         不许调用任何会阻塞的函数（I2C 读写、打印、延时都不行）。
 */
BSP_INLINE bsp_status_t bsp_exti_attach(bsp_exti_t *self, bsp_gpio_t *pin,
                                        bsp_edge_t edge, bsp_irq_cb_t cb, void *ctx)
{
    return self->ops->attach(self, pin, edge, cb, ctx);
}

/**
 * @brief  取消某个引脚的中断
 * @param  self 中断管理器句柄
 * @param  pin  哪个引脚
 * @retval BSP_OK / BSP_ERR_PARAM
 */
BSP_INLINE bsp_status_t bsp_exti_detach(bsp_exti_t *self, bsp_gpio_t *pin)
{
    return self->ops->detach(self, pin);
}

#endif /* BSP_IRQ_H */
