/**
 * @file    w25q64.h
 * @brief   W25Q64 SPI NOR Flash 组件（片选、指令、忙等待均在本层）。
 * @layer   Components
 *
 * 这是"SPI 总线 + 独立 CS 引脚 + 时间基准"三种依赖同时注入的例子：
 *   bsp_spi_t  → 数据通道
 *   bsp_gpio_t → 片选
 *   bsp_time_t → 忙等待超时（有上限，绝不死等）
 */
#ifndef W25Q64_H
#define W25Q64_H

#include "bsp.h"

#define W25Q64_PAGE_SIZE        256u
#define W25Q64_SECTOR_SIZE      4096u
#define W25Q64_CAPACITY         (8u * 1024u * 1024u)
#define W25Q64_TIMEOUT_MS       500u
#define W25Q64_DUMMY            0xFFu

typedef struct {
    bsp_spi_t  *spi;
    bsp_gpio_t *cs;
    bsp_time_t *time;
    uint32_t    jedec;
    uint8_t     present;
} w25q64_t;

bsp_status_t w25q64_init(w25q64_t *f, bsp_spi_t *spi, bsp_gpio_t *cs, bsp_time_t *time);
uint32_t     w25q64_jedec_id(w25q64_t *f);
uint8_t      w25q64_is_busy(w25q64_t *f);
bsp_status_t w25q64_wait_ready(w25q64_t *f);
bsp_status_t w25q64_read(w25q64_t *f, uint32_t addr, void *buf, uint16_t len);
bsp_status_t w25q64_page_program(w25q64_t *f, uint32_t addr, const void *buf, uint16_t len);
bsp_status_t w25q64_write(w25q64_t *f, uint32_t addr, const void *buf, uint16_t len);
bsp_status_t w25q64_erase_sector(w25q64_t *f, uint32_t addr);
bsp_status_t w25q64_erase_chip(w25q64_t *f);

#endif /* W25Q64_H */
