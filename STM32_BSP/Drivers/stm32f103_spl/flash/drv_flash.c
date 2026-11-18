/**
 * @file    drv_flash.c
 * @brief   内部 Flash 参数存储区（最后一页 1KB）。
 * @layer   Drivers / stm32f103
 *
 * 约定：地址是"相对于存储区基址"的偏移（0~1023），不是绝对 Flash 地址，
 *       从接口上杜绝写坏程序区的可能。
 */
#include "stm32f10x.h"
#include "drv_flash.h"
#include "drv_config.h"

#define DRV_FLASH_STORE_SIZE    DRV_FLASH_PAGE_SIZE

static uint8_t s_page_buf[DRV_FLASH_PAGE_SIZE];

/* 边界检查：只允许访问参数区那一页，从接口上杜绝写坏程序代码的可能 */
static bsp_status_t drv_flash_check(uint32_t offset, uint16_t len)
{
    if ((offset >= DRV_FLASH_STORE_SIZE) || ((offset + len) > DRV_FLASH_STORE_SIZE)) {
        return BSP_ERR_RANGE;
    }
    return BSP_OK;
}

/* 读 Flash：直接按指针取字节。Flash 读不需要解锁，也不需要擦 */
static bsp_status_t drv_flash_read(bsp_flash_t *self, uint32_t addr, void *buf, uint16_t len)
{
    const uint8_t *src;
    uint8_t       *dst = (uint8_t *)buf;
    uint16_t       i;

    BSP_UNUSED(self);

    if (buf == NULL) {
        return BSP_ERR_PARAM;
    }
    if (drv_flash_check(addr, len) != BSP_OK) {
        return BSP_ERR_RANGE;
    }

    src = (const uint8_t *)(DRV_FLASH_STORE_BASE + addr);
    for (i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
    return BSP_OK;
}

/* 擦一页。擦除是最慢的操作（几十毫秒），所以带状态轮询上限 */
static bsp_status_t drv_flash_erase_page(void)
{
    uint32_t guard = 100000u;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    if (FLASH_ErasePage(DRV_FLASH_STORE_BASE) != FLASH_COMPLETE) {
        FLASH_Lock();
        return BSP_ERR;
    }
    while ((FLASH_GetStatus() == FLASH_BUSY) && (--guard != 0u)) {
    }
    FLASH_Lock();
    return (guard == 0u) ? BSP_ERR_TIMEOUT : BSP_OK;
}

/**
 * @brief  写数据（读-改-写，对上层表现为"直接写字节"）
 * @param  self 句柄
 * @param  addr 偏移
 * @param  buf  数据
 * @param  len  长度
 * @retval BSP_OK / BSP_ERR_RANGE / 超时
 * @note   Flash 只能整页擦，所以流程是：
 *         1) 把整页读进 RAM 缓冲区
 *         2) 改掉要改的那几个字节
 *         3) 擦页
 *         4) 整页写回去
 *         这就是为什么写一个字节也要几十毫秒。
 */
static bsp_status_t drv_flash_program(bsp_flash_t *self, uint32_t addr, const void *buf, uint16_t len)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint16_t i;
    bsp_status_t st;

    BSP_UNUSED(self);

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }
    if (drv_flash_check(addr, len) != BSP_OK) {
        return BSP_ERR_RANGE;
    }

    /* 读-改-写：整页搬到 RAM，改完擦页再整页写回，接口上表现为"直接写字节" */
    (void)drv_flash_read(self, 0u, s_page_buf, (uint16_t)DRV_FLASH_PAGE_SIZE);
    for (i = 0u; i < len; i++) {
        s_page_buf[addr + i] = src[i];
    }

    st = drv_flash_erase_page();
    if (st != BSP_OK) {
        return st;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    for (i = 0u; i < DRV_FLASH_PAGE_SIZE; i += 2u) {
        uint16_t half = (uint16_t)((uint16_t)s_page_buf[i] | ((uint16_t)s_page_buf[i + 1u] << 8));
        if (half == 0xFFFFu) {
            continue;       /* 已擦除的半个字不必重复编程 */
        }
        if (FLASH_ProgramHalfWord(DRV_FLASH_STORE_BASE + i, half) != FLASH_COMPLETE) {
            FLASH_Lock();
            return BSP_ERR;
        }
    }
    FLASH_Lock();
    return BSP_OK;
}

/* 只支持"擦整页"，参数保留是为了符合契约（目前实现忽略 addr/len） */
static bsp_status_t drv_flash_erase(bsp_flash_t *self, uint32_t addr, uint16_t len)
{
    BSP_UNUSED(self);
    BSP_UNUSED(addr);
    BSP_UNUSED(len);
    return drv_flash_erase_page();
}

/* 页大小：F103C8 是 1KB 一页 */
static uint32_t drv_flash_page_size(bsp_flash_t *self)
{
    BSP_UNUSED(self);
    return DRV_FLASH_PAGE_SIZE;
}

/* 参数区容量：就一页 1KB */
static uint32_t drv_flash_capacity(bsp_flash_t *self)
{
    BSP_UNUSED(self);
    return DRV_FLASH_STORE_SIZE;
}

static const bsp_flash_ops_t drv_flash_ops = {
    drv_flash_read,
    drv_flash_program,
    drv_flash_erase,
    drv_flash_page_size,
    drv_flash_capacity
};

static bsp_flash_t s_flash = { &drv_flash_ops, NULL };

/**
 * @brief  拿到内部 Flash 参数区句柄
 * @retval 句柄
 * @note   地址是"相对参数区基址的偏移"，不是芯片绝对地址。
 */
bsp_flash_t *drv_flash_get(void)
{
    return &s_flash;
}
