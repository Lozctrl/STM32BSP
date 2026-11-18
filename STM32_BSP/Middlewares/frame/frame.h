/**
 * @file    frame.h
 * @brief   串口帧编解码（纯算法）：SOF | LEN | PAYLOAD | CRC8 | EOF，带转义。
 * @layer   Middlewares
 *
 * 线格式：
 *   SOF = 0x7E, EOF = 0x7E
 *   转义 = 0x7D；载荷/长度/CRC 中出现的 0x7E、0x7D 均编码为 0x7D, byte^0x20
 *   校验 = CRC-8/ATM，覆盖 LEN + PAYLOAD
 *
 * 相比原教程的 "0xFF + 4字节 + 0xFE"：
 *   - 长度可变，不写死 4 字节
 *   - 有 CRC，能发现比特错误
 *   - 有转义，载荷可以包含任意字节（包括帧头帧尾）
 */
#ifndef FRAME_H
#define FRAME_H

#include "bsp_types.h"

#define FRAME_SOF           0x7Eu
#define FRAME_EOF           0x7Eu
#define FRAME_ESC           0x7Du
#define FRAME_ESC_XOR       0x20u
#define FRAME_MAX_PAYLOAD   32u

typedef void (*frame_rx_cb_t)(void *ctx, const uint8_t *payload, uint8_t len);

typedef struct {
    uint8_t        state;
    uint8_t        len;
    uint8_t        idx;
    uint8_t        crc_rx;
    uint8_t        escape;
    uint8_t        buf[FRAME_MAX_PAYLOAD];
    frame_rx_cb_t  on_frame;
    void          *ctx;
    uint32_t       ok_cnt;
    uint32_t       err_cnt;
} frame_dec_t;

/** @brief 编码一帧，返回写入 out 的字节数（0 表示参数错误/缓冲不足） */
uint16_t frame_encode(uint8_t *out, uint16_t cap, const uint8_t *payload, uint8_t len);

void frame_dec_init(frame_dec_t *d, frame_rx_cb_t cb, void *ctx);
/** @brief 逐字节喂入（可在中断中直接调用） */
void frame_dec_push(frame_dec_t *d, uint8_t byte);
/** @brief 喂入一段数据，返回解析出的完整帧数量 */
uint16_t frame_dec_feed(frame_dec_t *d, const uint8_t *data, uint16_t len);

#endif /* FRAME_H */
