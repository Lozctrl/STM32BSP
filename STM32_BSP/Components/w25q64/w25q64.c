/**
 * @file    w25q64.c
 * @brief   W25Q64 组件实现。
 * @layer   Components
 *
 * 注意：page_program / erase 内部有"忙等待"，但都带 W25Q64_TIMEOUT_MS 上限。
 *      实时控制周期内请勿调用；需要在线写 Flash 时，请用 w25q64_is_busy()
 *      在应用层做状态机（本文件已提供该非阻塞原语）。
 */
#include "w25q64.h"

/* 指令集 */
#define W25Q64_CMD_WRITE_ENABLE     0x06u
#define W25Q64_CMD_READ_STATUS1     0x05u
#define W25Q64_CMD_READ_DATA        0x03u
#define W25Q64_CMD_PAGE_PROGRAM     0x02u
#define W25Q64_CMD_SECTOR_ERASE     0x20u
#define W25Q64_CMD_CHIP_ERASE       0xC7u
#define W25Q64_CMD_JEDEC_ID         0x9Fu

#define W25Q64_STATUS_BUSY          0x01u

/* 片选拉低 = "我要开始跟这颗 Flash 说话了"。SPI 是总线，
   同一时刻只能有一个从机被选中，所以片选由器件自己管，不归总线管 */
static void w25q64_cs_low(w25q64_t *f)
{
    bsp_gpio_write(f->cs, BSP_GPIO_LEVEL_LOW);
}

/* 片选拉高 = "我说完了"。指令必须靠它来划边界，否则 Flash 会一直等后续字节 */
static void w25q64_cs_high(w25q64_t *f)
{
    bsp_gpio_write(f->cs, BSP_GPIO_LEVEL_HIGH);
}

/* 收发一个字节：SPI 的收发是同一个动作，发出去的同时就收回来了 */
static uint8_t w25q64_xfer(w25q64_t *f, uint8_t tx)
{
    return bsp_spi_transfer_byte(f->spi, tx);
}

/* 写使能。Flash 有个安全设计：每次擦/写之前都必须先发这条指令，
   否则操作会被静默忽略（数据不会变，但也不报错，很容易误判成"写成功"） */
static bsp_status_t w25q64_write_enable(w25q64_t *f)
{
    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_WRITE_ENABLE);
    w25q64_cs_high(f);
    return BSP_OK;
}

/**
 * @brief  查芯片是否还在忙（擦除/写入中）
 * @param  f 对象
 * @retval 1=忙，0=空闲
 * @note   这是非阻塞原语，想自己写状态机的话用它，不要用 wait_ready 死等。
 */
uint8_t w25q64_is_busy(w25q64_t *f)
{
    uint8_t status;

    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_READ_STATUS1);
    status = w25q64_xfer(f, W25Q64_DUMMY);
    w25q64_cs_high(f);

    return (uint8_t)(status & W25Q64_STATUS_BUSY);
}

/**
 * @brief  等芯片空闲下来（带超时）
 * @param  f 对象
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   扇区擦除典型 45ms，整片擦除要几十秒。
 *         这是"有上限的等待"，不是死等，但依然不该放进控制周期里。
 */
bsp_status_t w25q64_wait_ready(w25q64_t *f)
{
    bsp_time_ms_t t0;

    if ((f == NULL) || (f->time == NULL)) {
        return BSP_ERR_PARAM;
    }

    t0 = bsp_time_ms(f->time);
    while (w25q64_is_busy(f) != 0u) {
        if (bsp_time_elapsed(bsp_time_ms(f->time), t0, W25Q64_TIMEOUT_MS)) {
            return BSP_ERR_TIMEOUT;
        }
    }
    return BSP_OK;
}

/**
 * @brief  读 JEDEC ID（芯片身份证）
 * @param  f 对象
 * @retval 24 位 ID，W25Q64 是 0xEF4017（EF=华邦，40=SPI Flash，17=64Mbit）
 * @note   上电自检用它判断 Flash 焊没焊、型号对不对。
 */
uint32_t w25q64_jedec_id(w25q64_t *f)
{
    uint32_t id = 0u;

    if (f == NULL) {
        return 0u;
    }
    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_JEDEC_ID);
    id  = (uint32_t)w25q64_xfer(f, W25Q64_DUMMY) << 16;
    id |= (uint32_t)w25q64_xfer(f, W25Q64_DUMMY) << 8;
    id |= (uint32_t)w25q64_xfer(f, W25Q64_DUMMY);
    w25q64_cs_high(f);
    return id;
}

/**
 * @brief  初始化：配置片选、读 ID 校验
 * @param  f    对象
 * @param  spi  SPI 总线（注入）
 * @param  cs   片选脚
 * @param  time 时间基准（忙等超时要用）
 * @retval BSP_OK / BSP_ERR_NODEV（ID 对不上）
 */
bsp_status_t w25q64_init(w25q64_t *f, bsp_spi_t *spi, bsp_gpio_t *cs, bsp_time_t *time)
{
    if ((f == NULL) || (spi == NULL) || (cs == NULL) || (time == NULL)) {
        return BSP_ERR_PARAM;
    }

    f->spi = spi;
    f->cs = cs;
    f->time = time;
    f->present = 0u;

    (void)bsp_gpio_set_mode(cs, BSP_GPIO_MODE_OUT_PP);
    w25q64_cs_high(f);

    f->jedec = w25q64_jedec_id(f);
    /* W25Q64 的 JEDEC ID = 0xEF4017 */
    if (f->jedec != 0xEF4017u) {
        return BSP_ERR_NODEV;
    }
    f->present = 1u;
    return BSP_OK;
}

/**
 * @brief  读数据（不关心对齐，任意地址任意长度）
 * @param  f   对象
 * @param  addr 绝对地址（0 ~ 8MB）
 * @param  buf  输出缓冲区
 * @param  len  字节数
 * @retval BSP_OK / BSP_ERR_RANGE
 */
bsp_status_t w25q64_read(w25q64_t *f, uint32_t addr, void *buf, uint16_t len)
{
    uint8_t *p = (uint8_t *)buf;
    uint16_t i;

    if ((f == NULL) || (buf == NULL) || (addr >= W25Q64_CAPACITY)) {
        return BSP_ERR_PARAM;
    }
    if ((addr + len) > W25Q64_CAPACITY) {
        return BSP_ERR_RANGE;
    }

    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_READ_DATA);
    (void)w25q64_xfer(f, (uint8_t)(addr >> 16));
    (void)w25q64_xfer(f, (uint8_t)(addr >> 8));
    (void)w25q64_xfer(f, (uint8_t)(addr));
    for (i = 0u; i < len; i++) {
        p[i] = w25q64_xfer(f, W25Q64_DUMMY);
    }
    w25q64_cs_high(f);
    return BSP_OK;
}

/**
 * @brief  写一页（最多 256 字节，且不能跨页）
 * @param  f   对象
 * @param  addr 起始地址
 * @param  buf  数据
 * @param  len  字节数（1~256）
 * @retval BSP_OK / BSP_ERR_RANGE / BSP_ERR_TIMEOUT
 * @note   Flash 只能"1 写 0"，写之前必须擦过；跨页写会绕回页首覆盖数据，
 *         所以这里直接拒绝跨页，由上层 w25q64_write 负责拆包。
 */
bsp_status_t w25q64_page_program(w25q64_t *f, uint32_t addr, const void *buf, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint16_t i;

    if ((f == NULL) || (buf == NULL) || (len == 0u) || (len > W25Q64_PAGE_SIZE)) {
        return BSP_ERR_PARAM;
    }
    if ((addr >= W25Q64_CAPACITY) || ((addr + len) > W25Q64_CAPACITY)) {
        return BSP_ERR_RANGE;       /* 超过 24 位地址范围会被截断，回卷到 Flash 开头 */
    }
    if ((addr / W25Q64_PAGE_SIZE) != ((addr + len - 1u) / W25Q64_PAGE_SIZE)) {
        return BSP_ERR_RANGE;       /* 跨页写会回卷覆盖，必须由上层拆分 */
    }

    (void)w25q64_write_enable(f);

    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_PAGE_PROGRAM);
    (void)w25q64_xfer(f, (uint8_t)(addr >> 16));
    (void)w25q64_xfer(f, (uint8_t)(addr >> 8));
    (void)w25q64_xfer(f, (uint8_t)(addr));
    for (i = 0u; i < len; i++) {
        (void)w25q64_xfer(f, p[i]);
    }
    w25q64_cs_high(f);

    return w25q64_wait_ready(f);
}

/**
 * @brief  写任意长度（自动按 256 字节分页）
 * @param  f   对象
 * @param  addr 起始地址
 * @param  buf  数据
 * @param  len  字节数
 * @retval BSP_OK / BSP_ERR_PARAM / BSP_ERR_RANGE / 超时
 * @note   注意：这里只负责"写"，不负责"擦"。写之前那块区域必须是 0xFF，
 *         否则写进去的是新旧数据按位与的结果。
 */
bsp_status_t w25q64_write(w25q64_t *f, uint32_t addr, const void *buf, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint16_t remain = len;
    bsp_status_t st;

    if ((f == NULL) || (buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }
    if ((addr >= W25Q64_CAPACITY) || ((addr + len) > W25Q64_CAPACITY)) {
        return BSP_ERR_RANGE;
    }

    while (remain > 0u) {
        uint16_t page_left = (uint16_t)(W25Q64_PAGE_SIZE - (addr % W25Q64_PAGE_SIZE));
        uint16_t chunk = (remain < page_left) ? remain : page_left;

        st = w25q64_page_program(f, addr, p, chunk);
        if (st != BSP_OK) {
            return st;
        }
        addr += chunk;
        p += chunk;
        remain = (uint16_t)(remain - chunk);
    }
    return BSP_OK;
}

/**
 * @brief  擦除一个扇区（4KB）
 * @param  f   对象
 * @param  addr 扇区内任意地址
 * @retval BSP_OK / 超时
 * @note   擦除后整块变成 0xFF。这是 Flash 能重新写入的前提。
 */
bsp_status_t w25q64_erase_sector(w25q64_t *f, uint32_t addr)
{
    if ((f == NULL) || (addr >= W25Q64_CAPACITY)) {
        return BSP_ERR_PARAM;
    }

    (void)w25q64_write_enable(f);

    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_SECTOR_ERASE);
    (void)w25q64_xfer(f, (uint8_t)(addr >> 16));
    (void)w25q64_xfer(f, (uint8_t)(addr >> 8));
    (void)w25q64_xfer(f, (uint8_t)(addr));
    w25q64_cs_high(f);

    return w25q64_wait_ready(f);
}

/**
 * @brief  整片擦除
 * @param  f 对象
 * @retval BSP_OK / 超时
 * @note   8MB 要几十秒，只在"恢复出厂设置"时用，别放在上电流程里。
 */
bsp_status_t w25q64_erase_chip(w25q64_t *f)
{
    if (f == NULL) {
        return BSP_ERR_PARAM;
    }
    (void)w25q64_write_enable(f);

    w25q64_cs_low(f);
    (void)w25q64_xfer(f, W25Q64_CMD_CHIP_ERASE);
    w25q64_cs_high(f);

    return w25q64_wait_ready(f);
}
