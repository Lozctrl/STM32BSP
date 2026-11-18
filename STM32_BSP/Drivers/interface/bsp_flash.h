/**
 * @file    bsp_flash.h
 * @brief   NVM 契约（内部 Flash / 外部 EEPROM 均可实现）。上层用字节地址。
 * @layer   Drivers / Interface
 */
#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include "bsp_types.h"

typedef struct bsp_flash bsp_flash_t;

typedef struct {
    bsp_status_t      (*read)(bsp_flash_t *self, uint32_t addr, void *buf, uint16_t len);
    /** 写入（内部已自动按页补齐，调用方无需擦除） */
    bsp_status_t      (*program)(bsp_flash_t *self, uint32_t addr, const void *buf, uint16_t len);
    bsp_status_t      (*erase)(bsp_flash_t *self, uint32_t addr, uint16_t len);
    uint32_t          (*page_size)(bsp_flash_t *self);
    uint32_t          (*capacity)(bsp_flash_t *self);
} bsp_flash_ops_t;

struct bsp_flash {
    const bsp_flash_ops_t *ops;
    void                  *hw;
};

/**
 * @brief  从存储区读数据
 * @param  self 存储句柄
 * @param  addr 偏移地址（从存储区开头算，不是芯片绝对地址）
 * @param  buf  输出缓冲区
 * @param  len  字节数
 * @retval BSP_OK / BSP_ERR_RANGE
 */
BSP_INLINE bsp_status_t bsp_flash_read(bsp_flash_t *self, uint32_t addr, void *buf, uint16_t len)
{
    return self->ops->read(self, addr, buf, len);
}

/**
 * @brief  往存储区写数据（内部已处理擦除，调用方不用先擦）
 * @param  self 存储句柄
 * @param  addr 偏移地址
 * @param  buf  要写的数据
 * @param  len  字节数
 * @retval BSP_OK / BSP_ERR_RANGE
 * @note   写一页 Flash 要几十毫秒，**不要在控制周期里调用**。
 *         上电读、停机写，才是它的正确用法。
 */
BSP_INLINE bsp_status_t bsp_flash_program(bsp_flash_t *self, uint32_t addr, const void *buf, uint16_t len)
{
    return self->ops->program(self, addr, buf, len);
}

/**
 * @brief  擦除一段区域（按页擦）
 * @param  self 存储句柄
 * @param  addr 起始偏移
 * @param  len  长度（内部会按页对齐处理）
 * @retval BSP_OK / BSP_ERR_RANGE
 */
BSP_INLINE bsp_status_t bsp_flash_erase(bsp_flash_t *self, uint32_t addr, uint16_t len)
{
    return self->ops->erase(self, addr, len);
}

#endif /* BSP_FLASH_H */
