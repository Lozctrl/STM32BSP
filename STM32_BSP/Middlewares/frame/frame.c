/**
 * @file    frame.c
 * @brief   串口帧编解码实现。
 * @layer   Middlewares
 */
#include "frame.h"

/* 增量 CRC8/ATM：crc = crc8_update(0x00, ...) */
static uint8_t frame_crc8_byte(uint8_t crc, uint8_t byte)
{
    uint8_t b;
    crc ^= byte;
    for (b = 0u; b < 8u; b++) {
        if ((crc & 0x80u) != 0u) {
            crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x07u);
        } else {
            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t frame_put_raw(uint8_t *out, uint16_t cap, uint16_t pos, uint8_t byte)
{
    if (pos >= cap) {
        return 0xFFFFu;     /* 溢出哨兵 */
    }
    out[pos] = byte;
    return (uint16_t)(pos + 1u);
}

static uint16_t frame_put_escaped(uint8_t *out, uint16_t cap, uint16_t pos, uint8_t byte)
{
    if ((byte == FRAME_SOF) || (byte == FRAME_ESC)) {
        pos = frame_put_raw(out, cap, pos, FRAME_ESC);
        if (pos == 0xFFFFu) {
            return pos;
        }
        byte = (uint8_t)(byte ^ FRAME_ESC_XOR);
    }
    return frame_put_raw(out, cap, pos, byte);
}

uint16_t frame_encode(uint8_t *out, uint16_t cap, const uint8_t *payload, uint8_t len)
{
    uint16_t pos = 0u;
    uint8_t  crc = 0x00u;
    uint8_t  i;

    if ((out == NULL) || (payload == NULL) || (len == 0u) || (len > FRAME_MAX_PAYLOAD)) {
        return 0u;
    }

    pos = frame_put_raw(out, cap, pos, FRAME_SOF);
    pos = frame_put_escaped(out, cap, pos, len);
    crc = frame_crc8_byte(crc, len);

    for (i = 0u; i < len; i++) {
        crc = frame_crc8_byte(crc, payload[i]);
        pos = frame_put_escaped(out, cap, pos, payload[i]);
        if (pos == 0xFFFFu) {
            return 0u;
        }
    }

    pos = frame_put_escaped(out, cap, pos, crc);
    if (pos == 0xFFFFu) {
        return 0u;
    }
    pos = frame_put_raw(out, cap, pos, FRAME_EOF);
    if (pos == 0xFFFFu) {
        return 0u;
    }
    return pos;
}

void frame_dec_init(frame_dec_t *d, frame_rx_cb_t cb, void *ctx)
{
    if (d == NULL) {
        return;
    }
    d->state = 0u;
    d->len = 0u;
    d->idx = 0u;
    d->crc_rx = 0u;
    d->escape = 0u;
    d->on_frame = cb;
    d->ctx = ctx;
    d->ok_cnt = 0u;
    d->err_cnt = 0u;
}

enum {
    FRAME_ST_WAIT_SOF = 0,
    FRAME_ST_LEN,
    FRAME_ST_PAYLOAD,
    FRAME_ST_CRC,
    FRAME_ST_EOF
};

void frame_dec_push(frame_dec_t *d, uint8_t byte)
{
    if (d == NULL) {
        return;
    }

    if (d->escape != 0u) {
        byte = (uint8_t)(byte ^ FRAME_ESC_XOR);
        d->escape = 0u;
    } else if (byte == FRAME_ESC) {
        d->escape = 1u;
        return;
    }

    switch (d->state) {
    case FRAME_ST_WAIT_SOF:
        if (byte == FRAME_SOF) {
            d->state = FRAME_ST_LEN;
            d->idx = 0u;
            d->len = 0u;
        }
        break;

    case FRAME_ST_LEN:
        if ((byte == 0u) || (byte > FRAME_MAX_PAYLOAD)) {
            d->err_cnt++;
            d->state = FRAME_ST_WAIT_SOF;
        } else {
            d->len = byte;
            d->crc_rx = frame_crc8_byte(0x00u, byte);
            d->idx = 0u;
            d->state = FRAME_ST_PAYLOAD;
        }
        break;

    case FRAME_ST_PAYLOAD:
        d->buf[d->idx] = byte;
        d->idx++;
        d->crc_rx = frame_crc8_byte(d->crc_rx, byte);
        if (d->idx >= d->len) {
            d->state = FRAME_ST_CRC;
        }
        break;

    case FRAME_ST_CRC:
        if (byte == d->crc_rx) {
            d->state = FRAME_ST_EOF;
        } else {
            d->err_cnt++;
            d->state = FRAME_ST_WAIT_SOF;
        }
        break;

    case FRAME_ST_EOF:
        if (byte == FRAME_EOF) {
            d->ok_cnt++;
            if (d->on_frame != NULL) {
                d->on_frame(d->ctx, d->buf, d->len);
            }
        } else {
            d->err_cnt++;
        }
        d->state = FRAME_ST_WAIT_SOF;
        break;

    default:
        d->state = FRAME_ST_WAIT_SOF;
        break;
    }
}

uint16_t frame_dec_feed(frame_dec_t *d, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint32_t before;

    if ((d == NULL) || (data == NULL)) {
        return 0u;
    }
    before = d->ok_cnt;
    for (i = 0u; i < len; i++) {
        frame_dec_push(d, data[i]);
    }
    return (uint16_t)(d->ok_cnt - before);
}
