/**
 * @file    oled.h
 * @brief   SSD1306 128x64 OLED 显示组件（带帧缓存 + 脏页刷新）。
 * @layer   Components
 *
 * 依赖倒置示范：
 *   oled_t 只持有一个 bsp_i2c_t*，从不出现 GPIO/延时/RCC。
 *   把软件 I2C 换成硬件 I2C、甚至换成 I3C/SPI 屏，本文件一行都不用改。
 *
 * 非阻塞设计：
 *   所有绘图函数只改内存帧缓存并置脏页标记；真正走 I2C 的只有 oled_flush()，
 *   由调度器按 20~30ms 周期调用，单次只发脏页，绝不在绘图时阻塞总线。
 */
#ifndef OLED_H
#define OLED_H

#include "bsp.h"

#define OLED_WIDTH              128u
#define OLED_HEIGHT             64u
#define OLED_PAGES              (OLED_HEIGHT / 8u)     /* 8 页 */
#define OLED_LINE_MAX           4u
#define OLED_COL_MAX            16u
#define OLED_ADDR_DEFAULT       0x3Cu                  /* 7bit 地址（原 0x78 右移一位） */
#define OLED_CMD_BYTE           0x00u
#define OLED_DATA_BYTE          0x40u
#define OLED_FLUSH_PERIOD_MS    25u

typedef struct {
    bsp_i2c_t *bus;                         /**< 注入的总线接口 */
    uint8_t    addr7;                       /**< 7 位从机地址 */
    uint8_t    fb[OLED_PAGES][OLED_WIDTH];  /**< 1KB 帧缓存（C8 的 20KB RAM 可承受） */
    uint8_t    dirty_mask;                  /**< bit n = 第 n 页需刷新 */
    uint8_t    online;                      /**< 首帧写成功置 1 */
} oled_t;

bsp_status_t oled_init(oled_t *o, bsp_i2c_t *bus, uint8_t addr7);

/* ---- 帧缓存操作（纯内存，不产生总线流量） ---- */
void oled_clear(oled_t *o);
void oled_fill(oled_t *o, uint8_t pattern);
void oled_draw_pixel(oled_t *o, uint8_t x, uint8_t y, uint8_t on);
void oled_draw_hline(oled_t *o, uint8_t x, uint8_t y, uint8_t w, uint8_t on);
void oled_draw_rect(oled_t *o, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on);
void oled_progress_bar(oled_t *o, uint8_t line, uint16_t permille);

/* ---- 文本 ---- */
void oled_show_char(oled_t *o, uint8_t line, uint8_t col, char ch);
void oled_show_string(oled_t *o, uint8_t line, uint8_t col, const char *str);
void oled_show_u32(oled_t *o, uint8_t line, uint8_t col, uint32_t value, uint8_t width);
void oled_show_i32(oled_t *o, uint8_t line, uint8_t col, int32_t value, uint8_t width);
void oled_show_f32(oled_t *o, uint8_t line, uint8_t col, float value, uint8_t decimals, uint8_t width);
/** @brief 清空整行后再显示，避免残留字符 */
void oled_show_string_padded(oled_t *o, uint8_t line, uint8_t col, const char *str);

/* ---- 总线刷新 ---- */
/**
 * @brief 全屏刷新（8 页一次发完，约 23ms@400kHz）。
 *        仅用于初始化/自检等非实时路径。
 */
bsp_status_t oled_flush(oled_t *o);
/**
 * @brief 分页刷新：最多发 max_pages 个脏页，单次阻塞约 2.9ms/页@400kHz。
 *        实时业务必须用这个，避免把 10ms 控制周期拖成 33ms。
 * @return 仍有脏页时返回 1
 */
uint8_t      oled_flush_step(oled_t *o, uint8_t max_pages);
void         oled_mark_all_dirty(oled_t *o);

#endif /* OLED_H */
