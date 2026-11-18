/**
 * @file    drv_flash.c
 * @brief   内部 Flash 参数存储区（HAL 后端，最后一页 1KB）。
 * @layer   Drivers / stm32f103_hal
 *
 * 地址约定与 SPL 版一致：都是"相对参数区基址的偏移"，不是绝对 Flash 地址。
 */
#include "stm32f1xx_hal.h"
#include "drv_flash.h"
#include "drv_config.h"

#define DRV_FLASH_STORE_SIZE    DRV_FLASH_PAGE_SIZE

static uint8_t  s_page_buf[DRV_FLASH_PAGE_SIZE];
static uint32_t s_page_error;       /* HAL 擦除失败时回填出错的页地址 */

static bsp_status_t drv_flash_check(uint32_t offset, uint16_t len)
{
    if ((offset >= DRV_FLASH_STORE_SIZE) || ((offset + len) > DRV_FLASH_STORE_SIZE)) {
        return BSP_ERR_RANGE;
    }
    return BSP_OK;
}

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

/**
 * @brief  擦掉参数区那一页
 * @retval BSP_OK / BSP_ERR / BSP_ERR_TIMEOUT
 * @note   HAL 的擦除是同步阻塞的（内部轮询 BSY），一页约 20~40ms。
 */
static bsp_status_t drv_flash_erase_page(void)
{
    FLASH_EraseInitTypeDef erase;
    HAL_StatusTypeDef      st;

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.Banks       = FLASH_BANK_1;
    erase.PageAddress = DRV_FLASH_STORE_BASE;
    erase.NbPages     = 1u;

    HAL_FLASH_Unlock();
    st = HAL_FLASHEx_Erase(&erase, &s_page_error);
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? BSP_OK : BSP_ERR;
}

/**
 * @brief  写数据（读-改-写，对上层表现为"直接写字节"）
 * @param  self 句柄
 * @param  addr 偏移
 * @param  buf  数据
 * @param  len  长度
 * @retval BSP_OK / BSP_ERR_RANGE / BSP_ERR
 * @note   流程：整页读进 RAM → 改字节 → 擦页 → 整页写回。
 *         所以"写 1 个字节"也要 20~40ms，别在控制周期里调用。
 */
static bsp_status_t drv_flash_program(bsp_flash_t *self, uint32_t addr, const void *buf, uint16_t len)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint16_t       i;
    bsp_status_t   st;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }
    if (drv_flash_check(addr, len) != BSP_OK) {
        return BSP_ERR_RANGE;
    }

    (void)drv_flash_read(self, 0u, s_page_buf, (uint16_t)DRV_FLASH_PAGE_SIZE);
    for (i = 0u; i < len; i++) {
        s_page_buf[addr + i] = src[i];
    }

    st = drv_flash_erase_page();
    if (st != BSP_OK) {
        return st;
    }

    HAL_FLASH_Unlock();
    for (i = 0u; i < DRV_FLASH_PAGE_SIZE; i += 2u) {
        uint16_t half = (uint16_t)((uint16_t)s_page_buf[i] |
                                   ((uint16_t)s_page_buf[i + 1u] << 8));
        if (half == 0xFFFFu) {
            continue;       /* 擦除后就是 0xFF，不必重复编程 */
        }
        /* HAL 的数据参数是 64 位，半字编程只用到低 16 位 */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                              DRV_FLASH_STORE_BASE + i,
                              (uint64_t)half) != HAL_OK) {
            HAL_FLASH_Lock();
            return BSP_ERR;
        }
    }
    HAL_FLASH_Lock();
    return BSP_OK;
}

static bsp_status_t drv_flash_erase(bsp_flash_t *self, uint32_t addr, uint16_t len)
{
    BSP_UNUSED(self);
    BSP_UNUSED(addr);
    BSP_UNUSED(len);
    return drv_flash_erase_page();
}

static uint32_t drv_flash_page_size(bsp_flash_t *self)
{
    BSP_UNUSED(self);
    return DRV_FLASH_PAGE_SIZE;
}

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

bsp_flash_t *drv_flash_get(void)
{
    return &s_flash;
}
