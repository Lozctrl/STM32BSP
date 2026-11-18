/**
 * @file    bsp_i2c.h
 * @brief   I2C 总线契约（7 位地址）。软件 I2C / 硬件 I2C 共用同一套接口。
 * @layer   Drivers / Interface
 *
 * 上层（OLED / MPU6050 / 任何 I2C 从机）只持有 bsp_i2c_t*，
 * 完全不知道底层是 GPIO 模拟还是硬件 I2C 外设。
 */
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include "bsp_types.h"

typedef struct bsp_i2c bsp_i2c_t;

typedef struct {
    /** 写寄存器/命令流：START + addr(W) + buf ... + STOP */
    bsp_status_t (*write)(bsp_i2c_t *self, uint8_t addr7,
                          const uint8_t *buf, uint16_t len);
    /** 读：START + addr(R) + buf ... + STOP */
    bsp_status_t (*read)(bsp_i2c_t *self, uint8_t addr7,
                         uint8_t *buf, uint16_t len);
    /** 器件寄存器写：START + addr(W) + mem + buf ... + STOP */
    bsp_status_t (*mem_write)(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                              const uint8_t *buf, uint16_t len);
    /** 器件寄存器读：写 mem 后 repeated START + addr(R) + buf ... + STOP */
    bsp_status_t (*mem_read)(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                             uint8_t *buf, uint16_t len);
} bsp_i2c_ops_t;

struct bsp_i2c {
    const bsp_i2c_ops_t *ops;
    void                *hw;
};

/**
 * @brief  向从机连续写一串字节：START + 地址(写) + 数据... + STOP
 * @param  self  总线句柄
 * @param  addr7 7 位从机地址（注意：不是 0x78 这种 8 位地址）
 * @param  buf   要发的数据
 * @param  len   字节数
 * @retval BSP_OK 成功；BSP_ERR_NODEV 从机没应答（没接、地址错、总线被拉死）
 * @note   地址一律用 7 位。0x3C 是 OLED，不是 0x78——后者是左移一位后的写法，
 *         不同厂家的库对这个约定不一致，本框架统一用 7 位，避免踩坑。
 */
BSP_INLINE bsp_status_t bsp_i2c_write(bsp_i2c_t *self, uint8_t addr7,
                                      const uint8_t *buf, uint16_t len)
{
    return self->ops->write(self, addr7, buf, len);
}

/**
 * @brief  从从机连续读一串字节：START + 地址(读) + 数据... + STOP
 * @param  self  总线句柄
 * @param  addr7 7 位从机地址
 * @param  buf   接收缓冲区
 * @param  len   要读的字节数（读最后一个字节时驱动会自动回 NACK）
 * @retval BSP_OK / BSP_ERR_NODEV / BSP_ERR_PARAM
 */
BSP_INLINE bsp_status_t bsp_i2c_read(bsp_i2c_t *self, uint8_t addr7,
                                     uint8_t *buf, uint16_t len)
{
    return self->ops->read(self, addr7, buf, len);
}

/**
 * @brief  写从机的某个寄存器：START + 地址(写) + 寄存器号 + 数据... + STOP
 * @param  self  总线句柄
 * @param  addr7 7 位从机地址
 * @param  mem   寄存器地址（器件内部的编号，比如 MPU6050 的 0x6B）
 * @param  buf   要写的数据
 * @param  len   字节数
 * @retval BSP_OK / BSP_ERR_NODEV
 * @note   "寄存器"是器件自己的概念，不是 I2C 协议的一部分。
 *         只有 MPU6050、EEPROM 这类"带地址的从机"才用得上。
 */
BSP_INLINE bsp_status_t bsp_i2c_mem_write(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                          const uint8_t *buf, uint16_t len)
{
    return self->ops->mem_write(self, addr7, mem, buf, len);
}

/**
 * @brief  读从机的某个寄存器：先写寄存器号，再 repeated START 读回来
 * @param  self  总线句柄
 * @param  addr7 7 位从机地址
 * @param  mem   寄存器地址
 * @param  buf   接收缓冲区
 * @param  len   连续读几个字节（很多器件支持自增地址，一次读一整个结构）
 * @retval BSP_OK / BSP_ERR_NODEV
 * @note   中间用的是 repeated START（不发 STOP），这样从机不会释放总线，
 *         MPU6050 这类器件要求必须这样读，否则读出来全是 0xFF。
 */
BSP_INLINE bsp_status_t bsp_i2c_mem_read(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                         uint8_t *buf, uint16_t len)
{
    return self->ops->mem_read(self, addr7, mem, buf, len);
}

/** @brief 单字节辅助：读 8 位寄存器 */

/**
 * @brief  读一个 8 位寄存器（最常用的快捷写法）
 * @param  self  总线句柄
 * @param  addr7 7 位从机地址
 * @param  reg   寄存器地址
 * @retval 寄存器里的值；读失败返回 0xFF（调用方要用返回值判断就得自己再看错误）
 * @note   这是"方便但丢错误码"的接口，正式代码建议直接用 bsp_i2c_mem_read()
 */
BSP_INLINE uint8_t bsp_i2c_read_reg8(bsp_i2c_t *self, uint8_t addr7, uint8_t reg)
{
    uint8_t v = 0xFFu;
    (void)self->ops->mem_read(self, addr7, reg, &v, 1u);
    return v;
}

#endif /* BSP_I2C_H */
