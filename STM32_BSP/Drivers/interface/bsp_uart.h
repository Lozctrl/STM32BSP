/**
 * @file    bsp_uart.h
 * @brief   串口契约。read() 永远非阻塞（返回实际字节数），由驱动内部环形缓冲承接 ISR。
 * @layer   Drivers / Interface
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include "bsp_types.h"

typedef struct bsp_uart bsp_uart_t;

typedef struct {
    /** 阻塞发送，timeout_ms 用传输上限保护；BSP_NEVER 表示不限 */
    bsp_status_t (*write)(bsp_uart_t *self, const uint8_t *buf, uint16_t len,
                          bsp_time_ms_t timeout_ms);
    /** 非阻塞读，返回读到的字节数（可能为 0） */
    uint16_t     (*read)(bsp_uart_t *self, uint8_t *buf, uint16_t cap);
    /** 接收缓冲区中待读字节数 */
    uint16_t     (*rx_available)(bsp_uart_t *self);
    /** 清空接收缓冲（协议重同步时用） */
    void         (*rx_flush)(bsp_uart_t *self);
} bsp_uart_ops_t;

struct bsp_uart {
    const bsp_uart_ops_t *ops;
    void                 *hw;
};

/**
 * @brief  发送一串字节（阻塞，但有超时保护）
 * @param  self       串口句柄
 * @param  buf        数据
 * @param  len        字节数
 * @param  timeout_ms 最长等多久；传 BSP_NEVER 表示不限
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   发 100 字节 @9600 大约 104ms，比一个控制周期还长。
 *         所以遥测要"发得出去就发，发不出去就丢帧"，不能让控制环等它。
 */
BSP_INLINE bsp_status_t bsp_uart_write(bsp_uart_t *self, const uint8_t *buf,
                                       uint16_t len, bsp_time_ms_t timeout_ms)
{
    return self->ops->write(self, buf, len, timeout_ms);
}

/**
 * @brief  读接收缓冲里的数据（**永不阻塞**）
 * @param  self 串口句柄
 * @param  buf  接收缓冲区
 * @param  cap  缓冲区最多能放多少字节
 * @retval 实际读到的字节数，可能是 0（表示暂时没数据）
 * @note   "非阻塞"是这个接口的硬性要求：
 *         驱动在中断里把字节塞进环形缓冲，主循环来取，取不到就下次再来。
 *         如果你移植新芯片时把这里写成 while(!RXNE); 整个框架的实时性就废了。
 */
BSP_INLINE uint16_t bsp_uart_read(bsp_uart_t *self, uint8_t *buf, uint16_t cap)
{
    return self->ops->read(self, buf, cap);
}

/**
 * @brief  查询还有多少字节没被读走
 * @param  self 串口句柄
 * @retval 待读字节数
 * @note   典型用法：if (bsp_uart_rx_available(u) >= 一帧长度) 再读，
 *         避免把半帧数据读出来解析。
 */
BSP_INLINE uint16_t bsp_uart_rx_available(bsp_uart_t *self)
{
    return self->ops->rx_available(self);
}

/**
 * @brief  清空接收缓冲
 * @param  self 串口句柄
 * @note   协议解析卡死、或者刚上电收到半截垃圾数据时用，
 *         相当于"这条线我不要旧数据了，重新开始"。
 */
BSP_INLINE void bsp_uart_rx_flush(bsp_uart_t *self)
{
    self->ops->rx_flush(self);
}

#endif /* BSP_UART_H */
