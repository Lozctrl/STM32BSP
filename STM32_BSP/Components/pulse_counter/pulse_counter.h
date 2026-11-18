/**
 * @file    pulse_counter.h
 * @brief   边沿计数组件（对射式红外、霍尔、干簧管等）。
 * @layer   Components
 *
 * 中断只做 count++，所有计算放在 pulse_counter_poll() 里，
 * 这是"ISR 只置标志"原则的最小实现。
 */
#ifndef PULSE_COUNTER_H
#define PULSE_COUNTER_H

#include "bsp.h"

typedef struct {
    bsp_gpio_t   *pin;
    bsp_exti_t   *exti;
    volatile uint32_t isr_count;    /**< 中断里累加，volatile 必须有 */
    uint32_t      last_seen;
    uint32_t      delta;
    uint32_t      glitch_cnt;
} pulse_counter_t;

bsp_status_t pulse_counter_init(pulse_counter_t *pc, bsp_exti_t *exti,
                                bsp_gpio_t *pin, bsp_edge_t edge);
/** @brief 返回自上次调用以来的脉冲数（主循环安全读取 volatile） */
uint32_t     pulse_counter_take(pulse_counter_t *pc);
uint32_t     pulse_counter_total(const pulse_counter_t *pc);

#endif /* PULSE_COUNTER_H */
