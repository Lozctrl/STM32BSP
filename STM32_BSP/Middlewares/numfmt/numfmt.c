/**
 * @file    numfmt.c
 * @brief   数字格式化实现（不使用 sprintf，避免链接大体积 printf 库）。
 * @layer   Middlewares
 */
#include "numfmt.h"

/* 往缓冲区第 pos 个位置放一个字符，越界就不写。
   返回值始终是"应该写到第几位"，方便调用方用 pos += numfmt_put(...) 链式拼接 */
static uint8_t numfmt_put(char *buf, uint8_t cap, uint8_t pos, char c)
{
    if (pos < cap) {
        buf[pos] = c;
    }
    return (uint8_t)(pos + 1u);
}

/**
 * @brief  无符号整数转字符串
 * @param  value 数值
 * @param  buf   输出缓冲区
 * @param  cap   缓冲区大小
 * @retval 写出的字符数（不写 '\0'，需要的话自己补）
 * @note   先倒着取余数、再反过来输出，这是整数转字符串的标准做法。
 */
uint8_t numfmt_u32(uint32_t value, char *buf, uint8_t cap)
{
    char tmp[10];
    uint8_t n = 0u;
    uint8_t pos = 0u;

    if ((buf == NULL) || (cap == 0u)) {
        return 0u;
    }

    if (value == 0u) {
        return numfmt_put(buf, cap, pos, '0');
    }

    while ((value > 0u) && (n < (uint8_t)sizeof(tmp))) {
        tmp[n] = (char)('0' + (char)(value % 10u));
        value /= 10u;
        n++;
    }
    while (n > 0u) {
        n--;
        pos = numfmt_put(buf, cap, pos, tmp[n]);
    }
    return pos;
}

/**
 * @brief  有符号整数转字符串（带负号）
 * @param  value 数值
 * @param  buf   输出缓冲区
 * @param  cap   缓冲区大小
 * @retval 写出的字符数
 * @note   处理 INT32_MIN 时不能直接取负（会溢出），所以先 +1 再取负再减 1。
 */
uint8_t numfmt_i32(int32_t value, char *buf, uint8_t cap)
{
    uint8_t pos = 0u;
    uint32_t mag;

    if ((buf == NULL) || (cap == 0u)) {
        return 0u;
    }
    if (value < 0) {
        pos = numfmt_put(buf, cap, pos, '-');
        mag = (uint32_t)(-(value + 1)) + 1u;    /* 规避 INT32_MIN 取负溢出 */
    } else {
        mag = (uint32_t)value;
    }
    pos = (uint8_t)(pos + numfmt_u32(mag, &buf[pos],
                   (uint8_t)((cap > pos) ? (int)(cap - pos) : 0)));
    return pos;
}

/**
 * @brief  浮点数转字符串，可指定小数位
 * @param  value    数值
 * @param  decimals 小数位数（0~6）
 * @param  buf      输出缓冲区
 * @param  cap      缓冲区大小
 * @retval 写出的字符数
 * @note   整个框架不链接 printf，就靠这个函数在 OLED 上显示带小数的数字。
 *         先算整数部分，再把小数部分放大成整数、补前导零，最后拼起来。
 */
uint8_t numfmt_f32(float value, uint8_t decimals, char *buf, uint8_t cap)
{
    uint8_t  pos = 0u;
    uint8_t  i;
    uint32_t scale = 1u;
    uint32_t ipart;
    uint32_t fpart;
    uint32_t thresh;

    if ((buf == NULL) || (cap == 0u)) {
        return 0u;
    }
    if (decimals > 6u) {
        decimals = 6u;
    }

    if (value < 0.0f) {
        pos = numfmt_put(buf, cap, pos, '-');
        value = -value;
    }
    if (value >= 1000000.0f) {
        value = 999999.9f;
    }

    for (i = 0u; i < decimals; i++) {
        scale *= 10u;
    }

    ipart = (uint32_t)value;
    fpart = (uint32_t)(((value - (float)ipart) * (float)scale) + 0.5f);
    if (fpart >= scale) {           /* 进位处理：0.999 -> 1.00 */
        fpart -= scale;
        ipart += 1u;
    }

    pos = (uint8_t)(pos + numfmt_u32(ipart, &buf[pos],
                   (uint8_t)((cap > pos) ? (int)(cap - pos) : 0)));

    if (decimals > 0u) {
        pos = numfmt_put(buf, cap, pos, '.');
        thresh = scale / 10u;       /* 10^(decimals-1) */
        while ((thresh > 1u) && (fpart < thresh)) {
            pos = numfmt_put(buf, cap, pos, '0');
            thresh /= 10u;
        }
        pos = (uint8_t)(pos + numfmt_u32(fpart, &buf[pos],
                       (uint8_t)((cap > pos) ? (int)(cap - pos) : 0)));
    }
    return pos;
}

/**
 * @brief  转成十六进制字符串（大写）
 * @param  value  数值
 * @param  digits 要几位（1~8），不足会在前面补 0
 * @param  buf    输出缓冲区
 * @param  cap    缓冲区大小
 * @retval 写出的字符数
 */
uint8_t numfmt_hex(uint32_t value, uint8_t digits, char *buf, uint8_t cap)
{
    static const char lut[] = "0123456789ABCDEF";
    uint8_t pos = 0u;
    int8_t  i;

    if ((buf == NULL) || (cap == 0u)) {
        return 0u;
    }
    if (digits == 0u) {
        digits = 1u;
    }
    if (digits > 8u) {
        digits = 8u;
    }

    for (i = (int8_t)(digits - 1u); i >= 0; i--) {
        uint8_t nib = (uint8_t)((value >> (uint8_t)(i * 4)) & 0x0Fu);
        pos = numfmt_put(buf, cap, pos, lut[nib]);
    }
    return pos;
}

/**
 * @brief  右对齐补位：把字符串往右挪，左边用 pad 填满到 width
 * @param  buf   字符串（原地修改）
 * @param  len   当前长度
 * @param  width 目标宽度
 * @param  pad   填充字符（一般是空格）
 * @note   显示数字时用它对齐，避免 5 和 1200 一个宽一个窄，界面跳来跳去。
 */
void numfmt_pad(char *buf, uint8_t len, uint8_t width, char pad)
{
    uint8_t shift;
    int16_t i;

    if ((buf == NULL) || (len >= width)) {
        return;
    }
    shift = (uint8_t)(width - len);
    for (i = (int16_t)(len - 1u); i >= 0; i--) {
        buf[i + shift] = buf[i];
    }
    for (i = 0; i < (int16_t)shift; i++) {
        buf[i] = pad;
    }
}
