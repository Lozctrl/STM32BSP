/**
 * @file    ringbuf.c
 * @brief   无锁环形缓冲实现。
 * @layer   Middlewares
 */
#include "ringbuf.h"

/* 判断容量是不是 2 的幂。是的话就能用 mask 代替取模，而且回绕天然正确 */
static uint16_t rb_is_pow2(uint16_t v)
{
    return (uint16_t)((v != 0u) && ((v & (uint16_t)(v - 1u)) == 0u));
}

/**
 * @brief  初始化环形缓冲
 * @param  rb       对象
 * @param  storage  外部提供的存储区（本框架不动态分配内存）
 * @param  capacity 容量，必须是 2 的幂（16/32/64/128...）
 * @retval BSP_OK / BSP_ERR_PARAM（容量不是 2 的幂就拒绝）
 */
bsp_status_t rb_init(ringbuf_t *rb, uint8_t *storage, uint16_t capacity)
{
    if ((rb == NULL) || (storage == NULL) || (rb_is_pow2(capacity) == 0u)) {
        return BSP_ERR_PARAM;
    }
    rb->buf = storage;
    rb->mask = (uint16_t)(capacity - 1u);
    rb->head = 0u;
    rb->tail = 0u;
    return BSP_OK;
}

/**
 * @brief  当前缓冲区里有多少字节
 * @retval 字节数
 * @note   先读 head 再读 tail 是有讲究的：head 可能被中断随时改，
 *         先读它保证算出来的数量是"保守估计"，不会多算。
 */
uint16_t rb_count(const ringbuf_t *rb)
{
    uint16_t head = rb->head;   /* 先取唯一可能变化的 head */
    uint16_t tail = rb->tail;
    return (uint16_t)((head - tail) & rb->mask);
}

/**
 * @brief  还能再塞多少字节
 * @retval 剩余空间
 */
uint16_t rb_free(const ringbuf_t *rb)
{
    return (uint16_t)(rb->mask - rb_count(rb));
}

/**
 * @brief  写入数据（生产者调用，可以在中断里调）
 * @param  rb   对象
 * @param  data 数据
 * @param  len  想写多少
 * @retval 实际写进去多少（空间不够时只写一部分，多的丢掉）
 * @note   单生产者单消费者场景下不需要关中断：
 *         生产者只改 head，消费者只改 tail，两边各管一头，互不踩。
 */
uint16_t rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    uint16_t head;
    uint16_t tail;
    uint16_t space;
    uint16_t n;
    uint16_t i;

    if ((rb == NULL) || (data == NULL) || (len == 0u)) {
        return 0u;
    }

    head = rb->head;
    tail = rb->tail;
    space = (uint16_t)(rb->mask - (uint16_t)((head - tail) & rb->mask));
    n = (len < space) ? len : space;

    for (i = 0u; i < n; i++) {
        rb->buf[(uint16_t)((head + i) & rb->mask)] = data[i];
    }
    rb->head = (uint16_t)(head + n);     /* 单点提交，消费者看不到半包 */
    return n;
}

/**
 * @brief  读出数据（消费者调用，一般在主循环里）
 * @param  rb  对象
 * @param  dst 接收缓冲区
 * @param  cap 最多读多少
 * @retval 实际读到多少（可能是 0）
 */
uint16_t rb_read(ringbuf_t *rb, uint8_t *dst, uint16_t cap)
{
    uint16_t head;
    uint16_t tail;
    uint16_t avail;
    uint16_t n;
    uint16_t i;

    if ((rb == NULL) || (dst == NULL) || (cap == 0u)) {
        return 0u;
    }

    tail = rb->tail;
    head = rb->head;
    avail = (uint16_t)((head - tail) & rb->mask);
    n = (cap < avail) ? cap : avail;

    for (i = 0u; i < n; i++) {
        dst[i] = rb->buf[(uint16_t)((tail + i) & rb->mask)];
    }
    rb->tail = (uint16_t)(tail + n);
    return n;
}

/**
 * @brief  偷看某个位置的数据但不取走
 * @param  rb     对象
 * @param  offset 相对读指针的偏移（0 = 下一个要读的字节）
 * @retval 那个字节；越界返回 -1
 * @note   用来做"先判断帧头再决定要不要读"，避免读到一半发现不是帧。
 */
int16_t rb_peek(const ringbuf_t *rb, uint16_t offset)
{
    if ((rb == NULL) || (offset >= rb_count(rb))) {
        return -1;
    }
    return (int16_t)rb->buf[(uint16_t)((rb->tail + offset) & rb->mask)];
}

/**
 * @brief  丢掉所有数据（把读指针追到写指针）
 * @param  rb 对象
 */
void rb_clear(ringbuf_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->tail = rb->head;
}
