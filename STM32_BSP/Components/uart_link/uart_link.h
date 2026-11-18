/**
 * @file    uart_link.h
 * @brief   串口链路组件：UART 抽象 + 帧协议 + 非阻塞收发。
 * @layer   Components
 *
 * 原教程把 USART1_IRQHandler 写在 Serial.c 里，中断服务函数与业务强耦合。
 * 本设计：驱动负责"收字节进环形缓冲"，本组件负责"把字节流还原成帧"，
 *        应用只关心 on_frame 回调，层次清晰。
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include "bsp.h"
#include "frame.h"

#define UART_LINK_RX_CHUNK      32u
#define UART_LINK_TX_TIMEOUT_MS 50u

typedef void (*uart_link_cb_t)(void *ctx, const uint8_t *payload, uint8_t len);

typedef struct {
    bsp_uart_t      *uart;
    frame_dec_t      dec;
    uart_link_cb_t   on_frame;
    void            *user_ctx;
    uint8_t          rx_chunk[UART_LINK_RX_CHUNK];
    uint8_t          tx_scratch[(FRAME_MAX_PAYLOAD * 2u) + 8u];
    uint32_t         rx_frames;
    uint32_t         tx_frames;
    uint32_t         rx_bytes;
    uint32_t         busy_cnt;
} uart_link_t;

void         uart_link_init(uart_link_t *l, bsp_uart_t *uart,
                            uart_link_cb_t cb, void *ctx);
/** @brief 周期调用：把驱动缓冲里的字节全部喂给协议解析器 */
void         uart_link_poll(uart_link_t *l);
bsp_status_t uart_link_send(uart_link_t *l, const uint8_t *payload, uint8_t len);
bsp_status_t uart_link_send_str(uart_link_t *l, const char *str);

#endif /* UART_LINK_H */
