/**
 * @file    bsp_spi.h
 * @brief   SPI 主设备契约。片选 CS 由上层器件（如 W25Q64）以 GPIO 自行管理，
 *          因为"哪个引脚做 CS"属于器件接线，不属于总线。
 * @layer   Drivers / Interface
 */
#ifndef BSP_SPI_H
#define BSP_SPI_H

#include "bsp_types.h"

typedef struct bsp_spi bsp_spi_t;

typedef struct {
    /** 全双工收发，len 字节；rx 可以为 NULL（只发） */
    bsp_status_t (*transfer)(bsp_spi_t *self, const uint8_t *tx, uint8_t *rx, uint16_t len);
    /** 单字节收发（寄存器级驱动用） */
    uint8_t      (*transfer_byte)(bsp_spi_t *self, uint8_t tx);
} bsp_spi_ops_t;

struct bsp_spi {
    const bsp_spi_ops_t *ops;
    void                *hw;
};

/**
 * @brief  全双工收发：发 tx 的同时收 rx（SPI 本质就是"边发边收"）
 * @param  self 总线句柄
 * @param  tx   要发的数据；只收不发时传 NULL
 * @param  rx   接收缓冲区；只发不收时传 NULL
 * @param  len  字节数
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   片选（CS）不在这里管。哪个引脚当片选是"器件接线"，
 *         由器件组件（如 w25q64）自己拉，总线只管数据通道。
 */
BSP_INLINE bsp_status_t bsp_spi_transfer(bsp_spi_t *self, const uint8_t *tx,
                                         uint8_t *rx, uint16_t len)
{
    return self->ops->transfer(self, tx, rx, len);
}

/**
 * @brief  收发一个字节（写 Flash 指令时最常用）
 * @param  self 总线句柄
 * @param  tx   要发的字节
 * @retval 同时收到的那个字节
 * @note   很多 Flash 指令就是"发一个命令码，然后读返回值"，
 *         这个接口刚好对应这种一问一答的时序。
 */
BSP_INLINE uint8_t bsp_spi_transfer_byte(bsp_spi_t *self, uint8_t tx)
{
    return self->ops->transfer_byte(self, tx);
}

#endif /* BSP_SPI_H */
