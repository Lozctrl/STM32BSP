/**
 * @file    oled.c
 * @brief   SSD1306 驱动组件实现（纯逻辑 + 总线抽象）。
 * @layer   Components
 */
#include "oled.h"
#include "oled_font.h"
#include "numfmt.h"

/* SSD1306 初始化序列：参数与江协教程一致，改的只是"怎么发" */
static const uint8_t oled_init_seq[] = {
    0xAE,               /* 关显示 */
    0xD5, 0x80,         /* 时钟分频 */
    0xA8, 0x3F,         /* 多路复用率 1/64 */
    0xD3, 0x00,         /* 显示偏移 */
    0x40,               /* 起始行 */
    0xA1,               /* 左右方向：正常 */
    0xC8,               /* 上下方向：正常 */
    0xDA, 0x12,         /* COM 引脚配置 */
    0x81, 0xCF,         /* 对比度 */
    0xD9, 0xF1,         /* 预充电周期 */
    0xDB, 0x30,         /* VCOMH */
    0xA4,               /* 显示跟随 RAM */
    0xA6,               /* 正常显示 */
    0x8D, 0x14,         /* 电荷泵使能 */
    0xAF                /* 开显示 */
};

/* 标记第 page 页"内容变了，下次要发出去"。
   用位图而不是逐字节比较，判断代价只有一次位运算 */
static void oled_mark_dirty(oled_t *o, uint8_t page)
{
    o->dirty_mask = (uint8_t)(o->dirty_mask | (uint8_t)(1u << page));
}

/* 发一条命令：控制字节 0x00 + 命令字。
   SSD1306 规定：控制字节 0x00 表示"后面是命令"，0x40 表示"后面是显存数据" */
static bsp_status_t oled_write_cmd(oled_t *o, uint8_t cmd)
{
    uint8_t buf[2];
    buf[0] = OLED_CMD_BYTE;
    buf[1] = cmd;
    return bsp_i2c_write(o->bus, o->addr7, buf, 2u);
}

/* 发一页显存数据：控制字节 0x40 + 最多 128 字节。
   一次事务发完 128 字节，而不是每字节起停一次——后者会慢十倍 */
static bsp_status_t oled_write_data(oled_t *o, const uint8_t *data, uint16_t len)
{
    /* 一次事务：控制字节 0x40 + 最多 128 字节数据，避免逐字节起停 */
    uint8_t tmp[OLED_WIDTH + 1u];
    uint16_t i;

    if (len > OLED_WIDTH) {
        return BSP_ERR_PARAM;
    }
    tmp[0] = OLED_DATA_BYTE;
    for (i = 0u; i < len; i++) {
        tmp[i + 1u] = data[i];
    }
    return bsp_i2c_write(o->bus, o->addr7, tmp, (uint16_t)(len + 1u));
}

/**
 * @brief  初始化屏幕：发一遍 SSD1306 配置序列，然后整屏刷一次
 * @param  o     对象
 * @param  bus   I2C 总线（注入，软件/硬件都行）
 * @param  addr7 7 位地址，通常是 0x3C（模块背面写着 0x78 的话就是它左移一位后的值）
 * @retval BSP_OK 成功；其他表示没接屏 / 地址不对
 * @note   如果屏幕带 RES 复位脚，记得在这之前等 100ms 再调本函数。
 */
bsp_status_t oled_init(oled_t *o, bsp_i2c_t *bus, uint8_t addr7)
{
    uint16_t i;
    bsp_status_t st = BSP_OK;

    if ((o == NULL) || (bus == NULL)) {
        return BSP_ERR_PARAM;
    }

    o->bus = bus;
    o->addr7 = (uint8_t)(addr7 & 0x7Fu);
    o->online = 0u;
    oled_clear(o);
    o->dirty_mask = 0u;

    for (i = 0u; i < (uint16_t)sizeof(oled_init_seq); i++) {
        st = oled_write_cmd(o, oled_init_seq[i]);
        if (st != BSP_OK) {
            return st;      /* 上电阶段就失败 => 屏没接好，交给上层决定是否重试 */
        }
    }

    oled_mark_all_dirty(o);
    st = oled_flush(o);
    if (st == BSP_OK) {
        o->online = 1u;
    }
    return st;
}

/**
 * @brief  清屏（把显存全写 0），并把 8 页都标记为脏
 * @param  o 对象
 * @note   只改内存，不动总线。真正发出去要等 oled_flush / oled_flush_step。
 */
void oled_clear(oled_t *o)
{
    uint8_t p;
    uint8_t x;

    if (o == NULL) {
        return;
    }
    for (p = 0u; p < OLED_PAGES; p++) {
        for (x = 0u; x < OLED_WIDTH; x++) {
            o->fb[p][x] = 0x00u;
        }
    }
    o->dirty_mask = 0xFFu;
}

/**
 * @brief  整屏填充某个字节（0xFF=全亮，0x00=全灭，0xAA=横条纹，可以用来测屏）
 * @param  o       对象
 * @param  pattern 填充值
 */
void oled_fill(oled_t *o, uint8_t pattern)
{
    uint8_t p;
    uint8_t x;

    if (o == NULL) {
        return;
    }
    for (p = 0u; p < OLED_PAGES; p++) {
        for (x = 0u; x < OLED_WIDTH; x++) {
            o->fb[p][x] = pattern;
        }
    }
    o->dirty_mask = 0xFFu;
}

/**
 * @brief  画一个点
 * @param  o  对象
 * @param  x  横坐标 0~127
 * @param  y  纵坐标 0~63
 * @param  on 1=点亮，0=熄灭
 * @note   SSD1306 的显存是"竖着 8 个点一个字节"，所以 y 要拆成
 *         页号（y/8）和位号（y%8），这是驱动屏幕最容易搞混的地方。
 */
void oled_draw_pixel(oled_t *o, uint8_t x, uint8_t y, uint8_t on)
{
    uint8_t page;
    uint8_t bit;

    if ((o == NULL) || (x >= OLED_WIDTH) || (y >= OLED_HEIGHT)) {
        return;
    }
    page = (uint8_t)(y >> 3);
    bit = (uint8_t)(1u << (y & 0x07u));

    if (on != 0u) {
        o->fb[page][x] |= bit;
    } else {
        o->fb[page][x] = (uint8_t)(o->fb[page][x] & (uint8_t)(~bit));
    }
    oled_mark_dirty(o, page);
}

/**
 * @brief  画一条水平线
 * @param  o 对象 @param x y 起点 @param w 长度 @param on 1=亮 0=灭
 */
void oled_draw_hline(oled_t *o, uint8_t x, uint8_t y, uint8_t w, uint8_t on)
{
    uint16_t i;
    for (i = 0u; i < w; i++) {
        oled_draw_pixel(o, (uint8_t)(x + i), y, on);
    }
}

/**
 * @brief  画一个实心矩形（进度条的边框和填充都靠它）
 * @param  o 对象 @param x y 左上角 @param w h 宽高 @param on 1=亮 0=灭
 */
void oled_draw_rect(oled_t *o, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on)
{
    uint8_t r;
    uint8_t c;

    if ((o == NULL) || (w == 0u) || (h == 0u)) {
        return;
    }
    for (r = 0u; r < h; r++) {
        for (c = 0u; c < w; c++) {
            oled_draw_pixel(o, (uint8_t)(x + c), (uint8_t)(y + r), on);
        }
    }
}

/**
 * @brief  在第 line 行画一个进度条（带边框）
 * @param  o        对象
 * @param  line     行号 1~4
 * @param  permille 千分比 0~1000
 * @note   先画边框再填充，所以填充不会盖住边。
 */
void oled_progress_bar(oled_t *o, uint8_t line, uint16_t permille)
{
    const uint8_t y = (uint8_t)((line - 1u) * 16u + 4u);
    const uint8_t width = 120u;
    uint16_t fill;

    if ((o == NULL) || (line < 1u) || (line > OLED_LINE_MAX)) {
        return;
    }
    if (permille > 1000u) {
        permille = 1000u;
    }

    oled_draw_rect(o, 4u, y, width, 9u, 0u);
    oled_draw_rect(o, 4u, y, width, 1u, 1u);
    oled_draw_rect(o, 4u, (uint8_t)(y + 8u), width, 1u, 1u);
    oled_draw_rect(o, 4u, y, 1u, 9u, 1u);
    oled_draw_rect(o, (uint8_t)(3u + width), y, 1u, 9u, 1u);

    fill = (uint16_t)(((uint32_t)(width - 2u) * permille) / 1000u);
    if (fill > 0u) {
        oled_draw_rect(o, 5u, (uint8_t)(y + 1u), (uint8_t)fill, 7u, 1u);
    }
}

/**
 * @brief  在指定位置显示一个字符
 * @param  o    对象
 * @param  line 行号 1~4（每行 16 像素高）
 * @param  col  列号 1~16（每个字符 8 像素宽）
 * @param  ch   要显示的 ASCII 字符
 * @note   字库里每个字符是 8x16 位图，正好占 2 页、8 列。
 *         不在字库范围内的字符会显示成 '?'。
 */
void oled_show_char(oled_t *o, uint8_t line, uint8_t col, char ch)
{
    uint8_t page;
    uint8_t x;
    uint8_t i;
    uint8_t idx;

    if ((o == NULL) || (line < 1u) || (line > OLED_LINE_MAX) ||
        (col < 1u) || (col > OLED_COL_MAX)) {
        return;
    }
    if ((ch < OLED_FONT_FIRST_CHAR) || ((uint8_t)ch > 0x7Eu)) {
        ch = '?';
    }

    idx = (uint8_t)((uint8_t)ch - (uint8_t)OLED_FONT_FIRST_CHAR);
    x = (uint8_t)((col - 1u) * OLED_FONT_WIDTH);
    page = (uint8_t)((line - 1u) * 2u);

    for (i = 0u; i < OLED_FONT_WIDTH; i++) {
        o->fb[page][x + i] = OLED_F8x16[idx][i];
        o->fb[page + 1u][x + i] = OLED_F8x16[idx][i + OLED_FONT_WIDTH];
    }
    oled_mark_dirty(o, page);
    oled_mark_dirty(o, (uint8_t)(page + 1u));
}

/**
 * @brief  显示一个字符串
 * @param  o    对象
 * @param  line 起始行 1~4
 * @param  col  起始列 1~16
 * @param  str  字符串（遇到 '\0' 结束，超出屏幕右边会自动截断）
 */
void oled_show_string(oled_t *o, uint8_t line, uint8_t col, const char *str)
{
    uint8_t i;

    if ((o == NULL) || (str == NULL)) {
        return;
    }
    for (i = 0u; (str[i] != '\0') && ((col + i) <= OLED_COL_MAX); i++) {
        oled_show_char(o, line, (uint8_t)(col + i), str[i]);
    }
}

/**
 * @brief  先清空整行，再显示字符串
 * @param  o 对象 @param line 行号 @param col 起始列 @param str 字符串
 * @note   动态刷新数字时用它，否则 999 变成 100 会剩下一个 "9" 的残影。
 */
void oled_show_string_padded(oled_t *o, uint8_t line, uint8_t col, const char *str)
{
    uint8_t i;

    for (i = col; i <= OLED_COL_MAX; i++) {
        oled_show_char(o, line, i, ' ');
    }
    oled_show_string(o, line, col, str);
}

/**
 * @brief  显示无符号整数（右对齐到 width 列）
 * @param  o 对象 @param line 行号 @param col 起始列 @param value 数值 @param width 占几列
 */
void oled_show_u32(oled_t *o, uint8_t line, uint8_t col, uint32_t value, uint8_t width)
{
    char buf[12];
    uint8_t len;

    len = numfmt_u32(value, buf, (uint8_t)sizeof(buf));
    numfmt_pad(buf, len, width, ' ');
    oled_show_string(o, line, col, buf);
}

/**
 * @brief  显示有符号整数（带 +/- 号，右对齐）
 * @param  o 对象 @param line 行号 @param col 起始列 @param value 数值 @param width 占几列
 */
void oled_show_i32(oled_t *o, uint8_t line, uint8_t col, int32_t value, uint8_t width)
{
    char buf[12];
    uint8_t len;

    len = numfmt_i32(value, buf, (uint8_t)sizeof(buf));
    numfmt_pad(buf, len, width, ' ');
    oled_show_string(o, line, col, buf);
}

/**
 * @brief  显示浮点数（可指定小数位，右对齐）
 * @param  o        对象
 * @param  line     行号
 * @param  col      起始列
 * @param  value    数值
 * @param  decimals 小数位数 0~6
 * @param  width    占几列
 */
void oled_show_f32(oled_t *o, uint8_t line, uint8_t col, float value, uint8_t decimals, uint8_t width)
{
    char buf[16];
    uint8_t len;

    len = numfmt_f32(value, decimals, buf, (uint8_t)sizeof(buf));
    numfmt_pad(buf, len, width, ' ');
    oled_show_string(o, line, col, buf);
}

/**
 * @brief  把 8 页全部标记为脏（下次刷新会整屏重发）
 * @param  o 对象
 */
void oled_mark_all_dirty(oled_t *o)
{
    if (o != NULL) {
        o->dirty_mask = 0xFFu;
    }
}

/** @brief 发送一页（128 字节）并清脏标记，约 2.9ms@400kHz */

/* 发一页：先设页地址和列地址，再一次性写 128 字节。
   一页大约 2.9ms@400kHz —— 这就是为什么刷新要分页，不能整屏一次发 */
static bsp_status_t oled_send_page(oled_t *o, uint8_t page)
{
    bsp_status_t st;

    st = oled_write_cmd(o, (uint8_t)(0xB0u | page));
    if (st != BSP_OK) {
        return st;
    }
    st = oled_write_cmd(o, 0x10u);      /* 列地址高 4 位 = 0 */
    if (st != BSP_OK) {
        return st;
    }
    st = oled_write_cmd(o, 0x00u);      /* 列地址低 4 位 = 0 */
    if (st != BSP_OK) {
        return st;
    }
    st = oled_write_data(o, o->fb[page], OLED_WIDTH);
    if (st != BSP_OK) {
        return st;
    }
    o->dirty_mask = (uint8_t)(o->dirty_mask & (uint8_t)(~(1u << page)));
    return BSP_OK;
}

/**
 * @brief  整屏刷新：把所有脏页一次发完
 * @param  o 对象
 * @retval BSP_OK / 总线错误
 * @note   整屏约 23ms@400kHz。**只适合初始化、自检这种不赶时间的场合**，
 *         实时业务请用 oled_flush_step()。
 */
bsp_status_t oled_flush(oled_t *o)
{
    uint8_t page;
    bsp_status_t st;

    if (o == NULL) {
        return BSP_ERR_PARAM;
    }
    if (o->dirty_mask == 0u) {
        return BSP_OK;      /* 没有脏页就不占总线，帧率不足也不会拖慢主循环 */
    }

    for (page = 0u; page < OLED_PAGES; page++) {
        if ((o->dirty_mask & (uint8_t)(1u << page)) == 0u) {
            continue;
        }
        st = oled_send_page(o, page);
        if (st != BSP_OK) {
            return st;
        }
    }
    return BSP_OK;
}

/**
 * @brief  分页刷新：最多发 max_pages 个脏页就返回
 * @param  o         对象
 * @param  max_pages 这次最多发几页（建议 1）
 * @retval 1=还有脏页没发完，0=全部发完了
 * @note   这是实时系统的正确做法：把 23ms 的一次性阻塞拆成 2.9ms 的小块，
 *         控制环最多被推迟 2.9ms 而不是 23ms。
 *         配合"渲染 200ms 一次、刷新 25ms 一页"，屏幕 200ms 更新一遍且不卡控制。
 */
uint8_t oled_flush_step(oled_t *o, uint8_t max_pages)
{
    uint8_t page;
    uint8_t sent = 0u;

    if ((o == NULL) || (max_pages == 0u)) {
        return 0u;
    }

    for (page = 0u; (page < OLED_PAGES) && (sent < max_pages); page++) {
        if ((o->dirty_mask & (uint8_t)(1u << page)) == 0u) {
            continue;
        }
        if (oled_send_page(o, page) != BSP_OK) {
            return (uint8_t)(o->dirty_mask != 0u);
        }
        sent++;
    }
    return (uint8_t)(o->dirty_mask != 0u);
}
