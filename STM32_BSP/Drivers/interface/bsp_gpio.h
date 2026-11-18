/**
 * @file    bsp_gpio.h
 * @brief   通用数字 IO 契约：任何 MCU 只要实现 bsp_gpio_ops_t 的 4 个函数即可。
 * @layer   Drivers / Interface
 *
 * 设计要点：
 *   1. bsp_gpio_t 是"多态句柄"，内部 ops 指针决定实际行为；
 *   2. 上层只拿到 bsp_gpio_t*，永远不知道它是 PA1 还是 P1.3；
 *   3. 换芯片时，drv_gpio_claim() 换实现，上层代码零改动。
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include "bsp_types.h"

typedef struct bsp_gpio bsp_gpio_t;

typedef enum {
    BSP_GPIO_MODE_IN_FLOAT = 0,
    BSP_GPIO_MODE_IN_PULLUP,
    BSP_GPIO_MODE_IN_PULLDOWN,
    BSP_GPIO_MODE_OUT_PP,
    BSP_GPIO_MODE_OUT_OD
} bsp_gpio_mode_t;

typedef enum {
    BSP_GPIO_LEVEL_LOW  = 0,
    BSP_GPIO_LEVEL_HIGH = 1
} bsp_gpio_level_t;

typedef struct {
    bsp_status_t     (*set_mode)(bsp_gpio_t *self, bsp_gpio_mode_t mode);
    void             (*write)(bsp_gpio_t *self, bsp_gpio_level_t level);
    bsp_gpio_level_t (*read)(bsp_gpio_t *self);
    void             (*toggle)(bsp_gpio_t *self);
} bsp_gpio_ops_t;

struct bsp_gpio {
    const bsp_gpio_ops_t *ops;  /**< 具体芯片实现 */
    void                 *hw;   /**< 实现私有数据（端口/引脚/编号） */
};

/**
 * @brief  设置这个引脚的工作模式（输入/输出、上拉/下拉、推挽/开漏）
 * @param  self 引脚句柄，由 drv_gpio_claim() 拿到
 * @param  mode 见 bsp_gpio_mode_t：IN_FLOAT / IN_PULLUP / IN_PULLDOWN / OUT_PP / OUT_OD
 * @retval BSP_OK 成功；BSP_ERR_PARAM 句柄为空
 * @note   同一个引脚可以反复调用，重复设置不会报错（驱动内部是"重新配置"而非"占用"）
 */
BSP_INLINE bsp_status_t bsp_gpio_set_mode(bsp_gpio_t *self, bsp_gpio_mode_t mode)
{
    if ((self == NULL) || (self->ops == NULL) || (self->ops->set_mode == NULL)) {
        return BSP_ERR_PARAM;
    }
    return self->ops->set_mode(self, mode);
}

/**
 * @brief  往引脚写电平（输出模式下才有意义）
 * @param  self  引脚句柄
 * @param  level BSP_GPIO_LEVEL_HIGH 或 BSP_GPIO_LEVEL_LOW
 * @note   这里写的是"电平高低"，不是"灯亮灯灭"。
 *         低电平点亮的 LED，要由 led 组件去换算，驱动不掺和这件事。
 */
BSP_INLINE void bsp_gpio_write(bsp_gpio_t *self, bsp_gpio_level_t level)
{
    self->ops->write(self, level);
}

/**
 * @brief  读引脚当前电平
 * @param  self 引脚句柄
 * @retval BSP_GPIO_LEVEL_HIGH / BSP_GPIO_LEVEL_LOW
 * @note   输入模式下读的是外部电平；输出模式下读的是输出寄存器里的值
 */
BSP_INLINE bsp_gpio_level_t bsp_gpio_read(bsp_gpio_t *self)
{
    return self->ops->read(self);
}

/**
 * @brief  翻转引脚电平
 * @param  self 引脚句柄
 * @note   等价于"读一次再取反写回去"，但不是原子操作。
 *         如果多个地方同时翻同一个脚，请自己加临界区。
 */
BSP_INLINE void bsp_gpio_toggle(bsp_gpio_t *self)
{
    self->ops->toggle(self);
}

/**
 * @brief  按"逻辑开关"写引脚（1=高，0=低）
 * @param  self 引脚句柄
 * @param  on   非 0 写高电平，0 写低电平
 * @note   只是 bsp_gpio_write 的语法糖，方便写 led_set()/buzzer_set()
 */
BSP_INLINE void bsp_gpio_set(bsp_gpio_t *self, uint8_t on)
{
    bsp_gpio_write(self, (on != 0u) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW);
}

#endif /* BSP_GPIO_H */
