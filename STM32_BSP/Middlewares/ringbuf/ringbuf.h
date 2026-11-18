/**
 * @file    ringbuf.h
 * @brief   单生产者/单消费者无锁环形缓冲（字节流）。
 * @layer   Middlewares
 *
 * 用法约束：
 *   - 容量必须是 2 的幂（用 mask 代替取模，且天然避免回绕分支）
 *   - 生产者（UART ISR）只写 head，消费者（主循环）只写 tail
 *   - 在 Cortex-M 单核上，16 位对齐读写是原子的，因此无需关中断
 *   - 若平台不保证对齐原子性，请在 bsp_critical 保护下调用 rb_write
 */
#ifndef RINGBUF_H
#define RINGBUF_H

#include "bsp_types.h"

typedef struct {
    uint8_t          *buf;
    uint16_t          mask;      /**< capacity - 1 */
    volatile uint16_t head;      /**< 写指针，仅生产者修改 */
    volatile uint16_t tail;      /**< 读指针，仅消费者修改 */
} ringbuf_t;

bsp_status_t rb_init(ringbuf_t *rb, uint8_t *storage, uint16_t capacity);
uint16_t     rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len);
uint16_t     rb_read(ringbuf_t *rb, uint8_t *dst, uint16_t cap);
int16_t      rb_peek(const ringbuf_t *rb, uint16_t offset);
uint16_t     rb_count(const ringbuf_t *rb);
uint16_t     rb_free(const ringbuf_t *rb);
void         rb_clear(ringbuf_t *rb);

#endif /* RINGBUF_H */
