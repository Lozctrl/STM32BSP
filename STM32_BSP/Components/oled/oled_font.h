/**
 * @file    oled_font.h
 * @brief   8x16 ASCII 字模声明（来自江协科技 OLED 字库，点阵未改动）。
 * @layer   Components
 */
#ifndef OLED_FONT_H
#define OLED_FONT_H

#include <stdint.h>

#define OLED_FONT_FIRST_CHAR    ' '
#define OLED_FONT_WIDTH         8u
#define OLED_FONT_HEIGHT        16u

extern const uint8_t OLED_F8x16[][16];

#endif /* OLED_FONT_H */
