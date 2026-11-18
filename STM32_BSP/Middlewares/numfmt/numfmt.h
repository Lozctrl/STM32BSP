/**
 * @file    numfmt.h
 * @brief   数字→字符串格式化（无 printf 依赖，可安全用于中断以外的任意上下文）。
 * @layer   Middlewares
 */
#ifndef NUMFMT_H
#define NUMFMT_H

#include "bsp_types.h"

uint8_t numfmt_u32(uint32_t value, char *buf, uint8_t cap);
uint8_t numfmt_i32(int32_t value, char *buf, uint8_t cap);
uint8_t numfmt_f32(float value, uint8_t decimals, char *buf, uint8_t cap);
uint8_t numfmt_hex(uint32_t value, uint8_t digits, char *buf, uint8_t cap);
/** @brief 在 buf 内把已有字符串左/右补空格到 width（不写入 '\0' 之外的内容） */
void    numfmt_pad(char *buf, uint8_t len, uint8_t width, char pad);

#endif /* NUMFMT_H */
