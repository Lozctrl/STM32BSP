/**
 * @file    drv_i2c.c
 * @brief   I2C 实现：GPIO 模拟（开漏）+ 硬件 I2C 外设，两套实现同一契约。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
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
 * 二、硬件 I2C 外设
 * ========================================================================== */
typedef struct {
    bsp_i2c_t api;
    I2C_TypeDef *reg;
    uint8_t  instance;
} hw_i2c_t;

static hw_i2c_t s_hw[DRV_I2C_POOL_SIZE];
static uint8_t  s_hw_used = 0u;

/* 等一个标志位，带循环次数上限。硬件 I2C 最容易死在"等不到标志"上，
   所以每个等待点都必须有超时 */
static uint8_t hw_i2c_wait_flag(I2C_TypeDef *reg, uint32_t flag, FlagStatus want)
{
    uint32_t guard = DRV_I2C_HW_TIMEOUT;
    while (I2C_GetFlagStatus(reg, flag) != want) {
        if (--guard == 0u) {
            return 0u;
        }
    }
    return 1u;
}

/* 硬件 I2C 发送：START -> 地址 -> 数据 -> STOP，每步都查标志 */
static bsp_status_t hw_i2c_ops_write(bsp_i2c_t *self, uint8_t addr7,
                                     const uint8_t *buf, uint16_t len)
{
    hw_i2c_t *h = (hw_i2c_t *)self;
    uint16_t i;

    I2C_GenerateSTART(h->reg, ENABLE);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_SB, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_TIMEOUT;
    }
    I2C_Send7bitAddress(h->reg, (uint8_t)(addr7 << 1), I2C_Direction_Transmitter);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_ADDR, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_NODEV;
    }
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR1);
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR2);

    for (i = 0u; i < len; i++) {
        if (hw_i2c_wait_flag(h->reg, I2C_FLAG_TXE, SET) == 0u) {
            I2C_GenerateSTOP(h->reg, ENABLE);
            return BSP_ERR_TIMEOUT;
        }
        I2C_SendData(h->reg, buf[i]);
    }
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_BTF, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_TIMEOUT;
    }
    I2C_GenerateSTOP(h->reg, ENABLE);
    return BSP_OK;
}

/* 硬件 I2C 接收：最后一个字节前关掉 ACK 并提前发 STOP，这是 STM32 的固定套路 */
static bsp_status_t hw_i2c_ops_read(bsp_i2c_t *self, uint8_t addr7,
                                    uint8_t *buf, uint16_t len)
{
    hw_i2c_t *h = (hw_i2c_t *)self;
    uint16_t i;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }

    I2C_AcknowledgeConfig(h->reg, ENABLE);
    I2C_GenerateSTART(h->reg, ENABLE);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_SB, SET) == 0u) {
        return BSP_ERR_TIMEOUT;
    }
    I2C_Send7bitAddress(h->reg, (uint8_t)((addr7 << 1) | 0x01u), I2C_Direction_Receiver);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_ADDR, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_NODEV;
    }
    /* F1 的 ADDR 必须"读 SR1 再读 SR2"才清除，否则不会开始接收数据 */
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR1);
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR2);

    for (i = 0u; i < len; i++) {
        if (i == (uint16_t)(len - 1u)) {
            I2C_AcknowledgeConfig(h->reg, DISABLE);    /* 最后一个字节回 NACK */
            I2C_GenerateSTOP(h->reg, ENABLE);
        }
        if (hw_i2c_wait_flag(h->reg, I2C_FLAG_RXNE, SET) == 0u) {
            return BSP_ERR_TIMEOUT;
        }
        buf[i] = I2C_ReceiveData(h->reg);
    }

    I2C_AcknowledgeConfig(h->reg, ENABLE);
    return BSP_OK;
}

/* 写寄存器：地址 + 寄存器号 + 数据 */
static bsp_status_t hw_i2c_ops_mem_write(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                         const uint8_t *buf, uint16_t len)
{
    hw_i2c_t *h = (hw_i2c_t *)self;
    uint16_t i;

    I2C_GenerateSTART(h->reg, ENABLE);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_SB, SET) == 0u) {
        return BSP_ERR_TIMEOUT;
    }
    I2C_Send7bitAddress(h->reg, (uint8_t)(addr7 << 1), I2C_Direction_Transmitter);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_ADDR, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_NODEV;
    }
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR1);
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR2);

    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_TXE, SET) == 0u) {
        return BSP_ERR_TIMEOUT;
    }
    I2C_SendData(h->reg, mem);

    for (i = 0u; i < len; i++) {
        if (hw_i2c_wait_flag(h->reg, I2C_FLAG_TXE, SET) == 0u) {
            I2C_GenerateSTOP(h->reg, ENABLE);
            return BSP_ERR_TIMEOUT;
        }
        I2C_SendData(h->reg, buf[i]);
    }
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_BTF, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_TIMEOUT;
    }
    I2C_GenerateSTOP(h->reg, ENABLE);
    return BSP_OK;
}

/* 读寄存器：先写寄存器号（不发 STOP），再 repeated START 读回来 */
static bsp_status_t hw_i2c_ops_mem_read(bsp_i2c_t *self, uint8_t addr7, uint8_t mem,
                                        uint8_t *buf, uint16_t len)
{
    hw_i2c_t *h = (hw_i2c_t *)self;
    bsp_status_t st;

    /* 先写寄存器地址（不发 STOP），再 repeated START 读 */
    I2C_GenerateSTART(h->reg, ENABLE);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_SB, SET) == 0u) {
        return BSP_ERR_TIMEOUT;
    }
    I2C_Send7bitAddress(h->reg, (uint8_t)(addr7 << 1), I2C_Direction_Transmitter);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_ADDR, SET) == 0u) {
        I2C_GenerateSTOP(h->reg, ENABLE);
        return BSP_ERR_NODEV;
    }
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR1);
    (void)I2C_ReadRegister(h->reg, I2C_Register_SR2);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_TXE, SET) == 0u) {
        return BSP_ERR_TIMEOUT;
    }
    I2C_SendData(h->reg, mem);
    if (hw_i2c_wait_flag(h->reg, I2C_FLAG_BTF, SET) == 0u) {
        return BSP_ERR_TIMEOUT;
    }

    st = hw_i2c_ops_read(self, addr7, buf, len);
    return st;
}

static const bsp_i2c_ops_t s_hw_ops = {
    hw_i2c_ops_write,
    hw_i2c_ops_read,
    hw_i2c_ops_mem_write,
    hw_i2c_ops_mem_read
};

/**
 * @brief  创建硬件 I2C（用芯片自带的 I2C 外设）
 * @param  instance 1 或 2
 * @retval 总线句柄；参数非法或池满返回 NULL
 * @note   引脚是固定的（I2C1=PB6/PB7，I2C2=PB10/PB11），配成复用开漏。
 *         ST 的硬件 I2C 有已知 errata，上板前先用扫描程序确认能正常读地址。
 */
bsp_i2c_t *drv_i2c_hw_create(uint8_t instance)
{
    hw_i2c_t     *h;
    I2C_TypeDef  *reg;
    GPIO_TypeDef *port;
    uint16_t      scl_pin;
    uint16_t      sda_pin;
    uint32_t      gpio_rcc;
    GPIO_InitTypeDef init;

    if (((instance != 1u) && (instance != 2u)) || (s_hw_used >= DRV_I2C_POOL_SIZE)) {
        return NULL;
    }

    if (instance == 1u) {
        reg = I2C1;
        port = GPIOB;
        scl_pin = GPIO_Pin_6;
        sda_pin = GPIO_Pin_7;
        gpio_rcc = RCC_APB2Periph_GPIOB;
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    } else {
        reg = I2C2;
        port = GPIOB;
        scl_pin = GPIO_Pin_10;
        sda_pin = GPIO_Pin_11;
        gpio_rcc = RCC_APB2Periph_GPIOB;
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    }
    RCC_APB2PeriphClockCmd(gpio_rcc, ENABLE);

    init.GPIO_Mode = GPIO_Mode_AF_OD;
    init.GPIO_Pin = (uint16_t)(scl_pin | sda_pin);
    init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &init);

    I2C_DeInit(reg);
    {
        I2C_InitTypeDef cfg;
        cfg.I2C_Mode = I2C_Mode_I2C;
        cfg.I2C_DutyCycle = I2C_DutyCycle_2;
        cfg.I2C_OwnAddress1 = 0x0Au;
        cfg.I2C_Ack = I2C_Ack_Enable;
        cfg.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
        cfg.I2C_ClockSpeed = 400000u;
        I2C_Init(reg, &cfg);
    }
    I2C_Cmd(reg, ENABLE);

    h = &s_hw[s_hw_used];
    s_hw_used++;

    h->reg = reg;
    h->instance = instance;
    h->api.ops = &s_hw_ops;
    h->api.hw = h;

    return &h->api;
}
