/**
 * @file    drv_i2c.c
 * @brief   I2C 实现：GPIO 模拟（开漏）+ 硬件 I2C 外设，两套实现同一契约。
 * @layer   Drivers / stm32f103_hal
 */
#include "stm32f1xx_hal.h"
#include "drv_i2c.h"
#include "drv_config.h"
#include "drv_time.h"
#include "drv_gpio.h"

#define DRV_I2C_HW_TIMEOUT      20000u      /* 忙等上限（约 200us@72MHz 量级） */
#define DRV_I2C_RECOVER_CLOCKS  9u

/* ==========================================================================
 * 一、GPIO 模拟 I2C
 * ========================================================================== */
typedef struct {
    bsp_i2c_t  api;
    bsp_gpio_t *scl;
    bsp_gpio_t *sda;
    bsp_time_t *time;
    uint32_t    half_us;
} soft_i2c_t;

static soft_i2c_t s_soft[DRV_I2C_POOL_SIZE];
static uint8_t    s_soft_used = 0u;

/* 拉高/拉低 SCL 并等半个时钟周期。
   I2C 的所有时序都是"变一次电平、等一会儿"堆出来的 */
static void si2c_scl(soft_i2c_t *b, uint8_t v)
{
    bsp_gpio_write(b->scl, (v != 0u) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW);
    bsp_time_delay_us(b->time, b->half_us);
}

/* 拉高/拉低 SDA。开漏输出：写 1 其实是"放手"，靠外部上拉电阻拉高，
   这样从机才有机会把线拉低（应答） */
static void si2c_sda(soft_i2c_t *b, uint8_t v)
{
    bsp_gpio_write(b->sda, (v != 0u) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW);
    bsp_time_delay_us(b->time, b->half_us);
}

/* 读 SDA 当前电平，同样等半个周期 */
static uint8_t si2c_read_sda(soft_i2c_t *b)
{
    uint8_t v = (bsp_gpio_read(b->sda) == BSP_GPIO_LEVEL_HIGH) ? 1u : 0u;
    bsp_time_delay_us(b->time, b->half_us);
    return v;
}

/* 起始条件：SCL 高电平期间，SDA 由高变低。这是 I2C 约定的"我要开始说话了" */
static void si2c_start(soft_i2c_t *b)
{
    si2c_sda(b, 1u);
    si2c_scl(b, 1u);
    si2c_sda(b, 0u);
    si2c_scl(b, 0u);
}

/* 停止条件：SCL 高电平期间，SDA 由低变高 */
static void si2c_stop(soft_i2c_t *b)
{
    si2c_sda(b, 0u);
    si2c_scl(b, 1u);
    si2c_sda(b, 1u);
}

/** @return 1 = 从机应答 */

/**
 * @brief  发一个字节，并读从机的应答
 * @param  b    软件 I2C 对象
 * @param  byte 要发的数据
 * @retval 1=从机应答(ACK)，0=没人应答(NACK)
 * @note   高位先发。第 9 个时钟是应答位：主机放手，从机拉低表示收到。
 */
static uint8_t si2c_write_byte(soft_i2c_t *b, uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0u; i < 8u; i++) {
        si2c_sda(b, (uint8_t)((byte & (uint8_t)(0x80u >> i)) != 0u));
        si2c_scl(b, 1u);
        si2c_scl(b, 0u);
    }

    si2c_sda(b, 1u);        /* 释放 SDA，由从机拉低表示 ACK */
    si2c_scl(b, 1u);
    ack = (uint8_t)(si2c_read_sda(b) == 0u);
    si2c_scl(b, 0u);
    return ack;
}

/**
 * @brief  读一个字节，并由主机回一个应答
 * @param  b    软件 I2C 对象
 * @param  more 1=后面还要继续读（回 ACK），0=这是最后一个字节（回 NACK）
 * @retval 读到的字节
 * @note   为什么最后一个要回 NACK？因为从机看到 NACK 才知道"别再发了"，
 *         否则它会一直占用总线。
 */
static uint8_t si2c_read_byte(soft_i2c_t *b, uint8_t more)
{
    uint8_t i;
    uint8_t byte = 0u;

    si2c_sda(b, 1u);
    for (i = 0u; i < 8u; i++) {
        si2c_scl(b, 1u);
        if (si2c_read_sda(b) != 0u) {
            byte = (uint8_t)(byte | (uint8_t)(0x80u >> i));
        }
        si2c_scl(b, 0u);
    }
    si2c_sda(b, (uint8_t)(more == 0u));     /* ack=1 表示主机要继续读，回 NACK */
    si2c_scl(b, 1u);
    si2c_scl(b, 0u);
    return byte;
}

/** @brief 总线卡死恢复：发 9 个时钟让从机释放 SDA */

/**
 * @brief  总线卡死后的恢复：手动补 9 个时钟 + 一个 STOP
 * @param  b 软件 I2C 对象
 * @note   典型场景：主循环在读的时候被复位了，从机还停在"等着发下一位"的状态，
 *         死死拉着 SDA 不放。补时钟让它把剩下的位发完，总线就松开了。
 */
static void si2c_bus_recover(soft_i2c_t *b)
{
    uint8_t i;

    bsp_gpio_write(b->sda, BSP_GPIO_LEVEL_HIGH);
    for (i = 0u; i < DRV_I2C_RECOVER_CLOCKS; i++) {
        si2c_scl(b, 1u);
        si2c_scl(b, 0u);
    }
    si2c_stop(b);
}

/** @brief 统一的失败收尾：先 STOP 再清理总线，避免下一次事务继续失败 */

/* 出错收尾：先发 STOP，再清一次总线，避免下一次事务接着失败 */
static bsp_status_t si2c_fail(soft_i2c_t *b)
{
    si2c_stop(b);
    si2c_bus_recover(b);
    return BSP_ERR_NODEV;
}

/* 通用发送：START + 地址 + 第一段 + 第二段 + STOP。
   两段设计是为了让 mem_write（寄存器 + 数据）复用同一份代码 */
static bsp_status_t si2c_transmit(soft_i2c_t *b, uint8_t addr7,
                                  const uint8_t *first, uint16_t first_len,
                                  const uint8_t *second, uint16_t second_len)
{
    uint16_t i;

    si2c_start(b);
    if (si2c_write_byte(b, (uint8_t)(addr7 << 1)) == 0u) {
        return si2c_fail(b);
    }
    for (i = 0u; i < first_len; i++) {
        if (si2c_write_byte(b, first[i]) == 0u) {
            return si2c_fail(b);
        }
    }
    for (i = 0u; i < second_len; i++) {
        if (si2c_write_byte(b, second[i]) == 0u) {
            return si2c_fail(b);
        }
    }
    si2c_stop(b);
    return BSP_OK;
}

/* 契约要求的 4 个入口，都是对 si2c_transmit/读时序的薄封装 */ 
static bsp_status_t si2c_ops_write(bsp_i2c_t *self, uint8_t addr7,
                                   const uint8_t *buf, uint16_t len)
{
    return si2c_transmit((soft_i2c_t *)self, addr7, buf, len, NULL, 0u);
}

static bsp_status_t si2c_ops_read(bsp_i2c_t *self, uint8_t addr7,
                                  uint8_t *buf, uint16_t len)
{
    soft_i2c_t *b = (soft_i2c_t *)self;
    uint16_t i;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }

    si2c_start(b);
    if (si2c_write_byte(b, (uint8_t)((addr7 << 1) | 0x01u)) == 0u) {
        return si2c_fail(b);
    }
    for (i = 0u; i < len; i++) {
        buf[i] = si2c_read_byte(b, (uint8_t)(i < (uint16_t)(len - 1u)));
    }
    si2c_stop(b);
    return BSP_OK;
}

static bsp_status_t si2c_ops_mem_write(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                       const uint8_t *buf, uint16_t len)
{
    return si2c_transmit((soft_i2c_t *)self, addr7, &mem, 1u, buf, len);
}

static bsp_status_t si2c_ops_mem_read(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                      uint8_t *buf, uint16_t len)
{
    soft_i2c_t *b = (soft_i2c_t *)self;
    uint16_t i;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }

    si2c_start(b);
    if (si2c_write_byte(b, (uint8_t)(addr7 << 1)) == 0u) {
        return si2c_fail(b);
    }
    if (si2c_write_byte(b, mem) == 0u) {
        return si2c_fail(b);
    }

    si2c_start(b);          /* repeated START */
    if (si2c_write_byte(b, (uint8_t)((addr7 << 1) | 0x01u)) == 0u) {
        return si2c_fail(b);
    }
    for (i = 0u; i < len; i++) {
        buf[i] = si2c_read_byte(b, (uint8_t)(i < (uint16_t)(len - 1u)));
    }
    si2c_stop(b);
    return BSP_OK;
}

static const bsp_i2c_ops_t s_soft_ops = {
    si2c_ops_write,
    si2c_ops_read,
    si2c_ops_mem_write,
    si2c_ops_mem_read
};

/**
 * @brief  创建一条 GPIO 模拟 I2C 总线
 * @param  scl      时钟线引脚
 * @param  sda      数据线引脚
 * @param  speed_hz 目标速率（100k / 400k）
 * @retval 总线句柄；参数非法或池满返回 NULL
 * @note   两条线都配成开漏输出并初始拉高（总线空闲态）。
 *         半周期时长 = 500000/speed_hz 微秒，四舍五入取整。
 */
bsp_i2c_t *drv_i2c_soft_create(bsp_gpio_t *scl, bsp_gpio_t *sda, uint32_t speed_hz)
{
    soft_i2c_t *b;

    if ((scl == NULL) || (sda == NULL) || (speed_hz == 0u) || (s_soft_used >= DRV_I2C_POOL_SIZE)) {
        return NULL;
    }

    b = &s_soft[s_soft_used];
    s_soft_used++;

    b->scl = scl;
    b->sda = sda;
    b->time = drv_time_get();
    b->half_us = (500000u / speed_hz) + 1u;     /* 半周期，向上取整 */
    b->api.ops = &s_soft_ops;
    b->api.hw = b;

    (void)bsp_gpio_set_mode(scl, BSP_GPIO_MODE_OUT_OD);
    (void)bsp_gpio_set_mode(sda, BSP_GPIO_MODE_OUT_OD);
    bsp_gpio_write(scl, BSP_GPIO_LEVEL_HIGH);
    bsp_gpio_write(sda, BSP_GPIO_LEVEL_HIGH);

    return &b->api;
}

/* ==========================================================================
 * 二、硬件 I2C 外设（HAL 后端）
 * ========================================================================== */
typedef struct {
    bsp_i2c_t         api;
    I2C_HandleTypeDef hi2c;
    uint8_t           instance;
} hal_i2c_t;

static hal_i2c_t s_hw[DRV_I2C_POOL_SIZE];
static uint8_t   s_hw_used = 0u;

#define DRV_I2C_HAL_TIMEOUT_MS  10u

/* HAL 的返回值 -> 本框架的返回码。HAL_ERROR 在 I2C 上基本都是"从机没应答" */
static bsp_status_t drv_i2c_from_hal(HAL_StatusTypeDef st)
{
    switch (st) {
    case HAL_OK:      return BSP_OK;
    case HAL_TIMEOUT: return BSP_ERR_TIMEOUT;
    case HAL_BUSY:    return BSP_ERR_BUSY;
    default:          return BSP_ERR_NODEV;
    }
}

/**
 * @brief  写一串字节
 * @note   HAL 的地址参数是"8 位地址"（已经左移过的），所以这里要 addr7 << 1。
 *         这是 SPL 转 HAL 最容易错的一处：SPL 传 7 位，HAL 传 8 位。
 */
static bsp_status_t hw_i2c_ops_write(bsp_i2c_t *self, uint8_t addr7,
                                     const uint8_t *buf, uint16_t len)
{
    hal_i2c_t *h = (hal_i2c_t *)self;

    return drv_i2c_from_hal(HAL_I2C_Master_Transmit(&h->hi2c, (uint16_t)(addr7 << 1),
                                                    (uint8_t *)buf, len,
                                                    DRV_I2C_HAL_TIMEOUT_MS));
}

static bsp_status_t hw_i2c_ops_read(bsp_i2c_t *self, uint8_t addr7,
                                    uint8_t *buf, uint16_t len)
{
    hal_i2c_t *h = (hal_i2c_t *)self;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }
    return drv_i2c_from_hal(HAL_I2C_Master_Receive(&h->hi2c, (uint16_t)(addr7 << 1),
                                                   buf, len, DRV_I2C_HAL_TIMEOUT_MS));
}

static bsp_status_t hw_i2c_ops_mem_write(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                         const uint8_t *buf, uint16_t len)
{
    hal_i2c_t *h = (hal_i2c_t *)self;

    return drv_i2c_from_hal(HAL_I2C_Mem_Write(&h->hi2c, (uint16_t)(addr7 << 1), mem,
                                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len,
                                              DRV_I2C_HAL_TIMEOUT_MS));
}

static bsp_status_t hw_i2c_ops_mem_read(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                        uint8_t *buf, uint16_t len)
{
    hal_i2c_t *h = (hal_i2c_t *)self;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }
    return drv_i2c_from_hal(HAL_I2C_Mem_Read(&h->hi2c, (uint16_t)(addr7 << 1), mem,
                                             I2C_MEMADD_SIZE_8BIT, buf, len,
                                             DRV_I2C_HAL_TIMEOUT_MS));
}

static const bsp_i2c_ops_t s_hw_ops = {
    hw_i2c_ops_write,
    hw_i2c_ops_read,
    hw_i2c_ops_mem_write,
    hw_i2c_ops_mem_read
};

/**
 * @brief  创建硬件 I2C（HAL 后端）
 * @param  instance 1 或 2
 * @retval 句柄；参数非法 / 池满 / HAL 初始化失败返回 NULL
 * @note   400kHz 快速模式。STM32F1 的硬件 I2C 有已知 errata，
 *         上板先跑一次地址扫描确认能读到设备，再谈业务。
 */
bsp_i2c_t *drv_i2c_hw_create(uint8_t instance)
{
    hal_i2c_t       *h;
    GPIO_InitTypeDef gpio;

    if (((instance != 1u) && (instance != 2u)) || (s_hw_used >= DRV_I2C_POOL_SIZE)) {
        return NULL;
    }

    h = &s_hw[s_hw_used];

    gpio.Mode  = GPIO_MODE_AF_OD;       /* I2C 必须复用开漏 */
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    if (instance == 1u) {
        h->hi2c.Instance = I2C1;
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_I2C1_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;     /* SCL=PB6, SDA=PB7 */
        HAL_GPIO_Init(GPIOB, &gpio);
    } else {
        h->hi2c.Instance = I2C2;
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_I2C2_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;   /* SCL=PB10, SDA=PB11 */
        HAL_GPIO_Init(GPIOB, &gpio);
    }

    h->hi2c.Init.ClockSpeed      = 400000u;
    h->hi2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    h->hi2c.Init.OwnAddress1     = 0x0Au;   /* 主机模式，随便给一个 */
    h->hi2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    h->hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->hi2c.Init.OwnAddress2     = 0u;
    h->hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h->hi2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&h->hi2c) != HAL_OK) {
        return NULL;
    }

    h->instance = instance;
    h->api.ops  = &s_hw_ops;
    h->api.hw   = h;
    s_hw_used++;

    return &h->api;
}
