/**
 * @file    uart_link.c
 * @brief   串口链路组件实现。
 * @layer   Components
 */
#include "uart_link.h"
#include <string.h>

/* 协议层解出一帧后回调这里：先统计，再转给应用注册的回调。
   应用只需要关心"收到一帧数据"，不用管字节怎么拼的、CRC 怎么算的 */
static void uart_link_dispatch(void *ctx, const uint8_t *payload, uint8_t len)
{
    uart_link_t *l = (uart_link_t *)ctx;

    l->rx_frames++;
    if (l->on_frame != NULL) {
        l->on_frame(l->user_ctx, payload, len);
    }
}

/**
 * @brief  初始化串口链路
 * @param  l    对象
 * @param  uart 串口（注入）
 * @param  cb   收到一帧时的回调（可以先传 NULL，之后再赋值）
 * @param  ctx  传给回调的上下文（一般传给 board 指针）
 */
void uart_link_init(uart_link_t *l, bsp_uart_t *uart,
                    uart_link_cb_t cb, void *ctx)
{
    if ((l == NULL) || (uart == NULL)) {
        return;
    }
    l->uart = uart;
    l->on_frame = cb;
    l->user_ctx = ctx;
    l->rx_frames = 0u;
    l->tx_frames = 0u;
    l->rx_bytes = 0u;
    l->busy_cnt = 0u;
    frame_dec_init(&l->dec, uart_link_dispatch, l);
    bsp_uart_rx_flush(uart);
}

/**
 * @brief  把串口缓冲里的字节全部取出来喂给协议解析器
 * @param  l 对象
 * @note   必须周期性调用（建议 5ms）。取的时候用 do-while 一次取空，
 *         避免"边收边丢一半"。
 */
void uart_link_poll(uart_link_t *l)
{
    uint16_t n;

    if ((l == NULL) || (l->uart == NULL)) {
        return;
    }

    /* 一次取空驱动缓冲，避免边收边丢掉一半 */
    do {
        n = bsp_uart_read(l->uart, l->rx_chunk, (uint16_t)UART_LINK_RX_CHUNK);
        if (n > 0u) {
            l->rx_bytes += n;
            (void)frame_dec_feed(&l->dec, l->rx_chunk, n);
        }
    } while (n == UART_LINK_RX_CHUNK);
}

/**
 * @brief  把一段数据打包成一帧发出去
 * @param  l       对象
 * @param  payload 数据（最多 FRAME_MAX_PAYLOAD 字节）
 * @param  len     长度
 * @retval BSP_OK / BSP_ERR_TIMEOUT（发不出去就丢帧，不重试）
 * @note   发不出去为什么不重试？因为这是遥测数据，
 *         旧的一帧丢了没关系，卡住等它反而是灾难。
 */
bsp_status_t uart_link_send(uart_link_t *l, const uint8_t *payload, uint8_t len)
{
    uint16_t n;

    if ((l == NULL) || (l->uart == NULL) || (payload == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }

    n = frame_encode(l->tx_scratch, (uint16_t)sizeof(l->tx_scratch), payload, len);
    if (n == 0u) {
        return BSP_ERR_RANGE;
    }

    if (bsp_uart_write(l->uart, l->tx_scratch, n, (bsp_time_ms_t)UART_LINK_TX_TIMEOUT_MS) != BSP_OK) {
        l->busy_cnt++;
        return BSP_ERR_TIMEOUT;
    }

    l->tx_frames++;
    return BSP_OK;
}

/**
 * @brief  发一个字符串（太长会自动拆成多帧）
 * @param  l   对象
 * @param  str 以 '\0' 结尾的字符串
 * @retval BSP_OK / 发送超时
 */
bsp_status_t uart_link_send_str(uart_link_t *l, const char *str)
{
    uint16_t len;

    if (str == NULL) {
        return BSP_ERR_PARAM;
    }
    len = (uint16_t)strlen(str);
    while (len > 0u) {
        uint8_t chunk = (uint8_t)BSP_MIN(len, (uint16_t)FRAME_MAX_PAYLOAD);
        bsp_status_t st = uart_link_send(l, (const uint8_t *)str, chunk);
        if (st != BSP_OK) {
            return st;
        }
        str += chunk;
        len = (uint16_t)(len - chunk);
    }
    return BSP_OK;
}
