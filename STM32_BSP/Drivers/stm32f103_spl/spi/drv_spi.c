/**
 * @file    drv_spi.c
 * @brief   SPI 主设备实现：软件（模式 0）+ 硬件 SPI1/SPI2。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_spi.h"
#include "drv_config.h"
#include "drv_time.h"

/* ---------------- 软件 SPI ---------------- */
typedef struct {
    bsp_spi_t  api;
    bsp_gpio_t *sck;
    bsp_gpio_t *mosi;
    bsp_gpio_t *miso;
    bsp_time_t *time;
    uint32_t    half_us;
} soft_spi_t;

static soft_spi_t s_soft[DRV_SPI_POOL_SIZE];
static uint8_t    s_soft_used = 0u;

/* 软件 SPI 的半周期延时。SPI 比 I2C 宽容得多，慢一点也没事 */
static void sspi_delay(soft_spi_t *s)
{
    bsp_time_delay_us(s->time, s->half_us);
}

/**
 * @brief  软件 SPI 收发一个字节（模式 0：空闲低电平，上升沿采样）
 * @param  s  软件 SPI 对象
 * @param  tx 要发的字节
 * @retval 同时收到的字节
 * @note   高位先发。每发一位都把 SCK 拉高再拉低，从机在上升沿把数据读走。
 */
static uint8_t sspi_xfer(soft_spi_t *s, uint8_t tx)
{
    uint8_t rx = 0u;
    uint8_t i;

    for (i = 0u; i < 8u; i++) {
        bsp_gpio_write(s->mosi, ((tx & 0x80u) != 0u) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW);
        tx = (uint8_t)(tx << 1);
        sspi_delay(s);
        bsp_gpio_write(s->sck, BSP_GPIO_LEVEL_HIGH);
        rx = (uint8_t)(rx << 1);
        if (bsp_gpio_read(s->miso) == BSP_GPIO_LEVEL_HIGH) {
            rx = (uint8_t)(rx | 0x01u);
        }
        sspi_delay(s);
        bsp_gpio_write(s->sck, BSP_GPIO_LEVEL_LOW);
    }
    return rx;
}

/* 连续收发：tx 为 NULL 表示只收（发 0xFF 占位），rx 为 NULL 表示只发 */
static bsp_status_t sspi_transfer(bsp_spi_t *self, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    soft_spi_t *s = (soft_spi_t *)self;
    uint16_t i;

    for (i = 0u; i < len; i++) {
        uint8_t out = (tx != NULL) ? tx[i] : 0xFFu;
        uint8_t in = sspi_xfer(s, out);
        if (rx != NULL) {
            rx[i] = in;
        }
    }
    return BSP_OK;
}

/* 单字节收发，给"发指令读返回值"这种一问一答用 */
static uint8_t sspi_transfer_byte(bsp_spi_t *self, uint8_t tx)
{
    return sspi_xfer((soft_spi_t *)self, tx);
}

static const bsp_spi_ops_t s_soft_ops = { sspi_transfer, sspi_transfer_byte };

/**
 * @brief  创建软件 SPI
 * @param  sck/mosi/miso 三个引脚
 * @param  speed_hz      目标速率
 * @retval 句柄；参数非法或池满返回 NULL
 * @note   SCK/MOSI 配推挽输出，MISO 配浮空输入。
 *         片选不在这里——那是器件自己的事。
 */
bsp_spi_t *drv_spi_soft_create(bsp_gpio_t *sck, bsp_gpio_t *mosi,
                               bsp_gpio_t *miso, uint32_t speed_hz)
{
    soft_spi_t *s;

    if ((sck == NULL) || (mosi == NULL) || (miso == NULL) ||
        (speed_hz == 0u) || (s_soft_used >= DRV_SPI_POOL_SIZE)) {
        return NULL;
    }

    s = &s_soft[s_soft_used];
    s_soft_used++;

    s->sck = sck;
    s->mosi = mosi;
    s->miso = miso;
    s->time = drv_time_get();
    s->half_us = (500000u / speed_hz) + 1u;
    s->api.ops = &s_soft_ops;
    s->api.hw = s;

    (void)bsp_gpio_set_mode(sck, BSP_GPIO_MODE_OUT_PP);
    (void)bsp_gpio_set_mode(mosi, BSP_GPIO_MODE_OUT_PP);
    (void)bsp_gpio_set_mode(miso, BSP_GPIO_MODE_IN_FLOAT);
    bsp_gpio_write(sck, BSP_GPIO_LEVEL_LOW);
    bsp_gpio_write(mosi, BSP_GPIO_LEVEL_HIGH);
    return &s->api;
}

/* ---------------- 硬件 SPI ---------------- */
typedef struct {
    bsp_spi_t   api;
    SPI_TypeDef *reg;
} hw_spi_t;

static hw_spi_t s_hw[DRV_SPI_POOL_SIZE];
static uint8_t  s_hw_used = 0u;

#define DRV_SPI_HW_TIMEOUT  20000u

/* 硬件 SPI 连续收发：每字节都等 TXE（发送空）和 RXNE（接收非空），带超时 */
static bsp_status_t hspi_transfer(bsp_spi_t *self, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    hw_spi_t *h = (hw_spi_t *)self;
    uint16_t i;

    for (i = 0u; i < len; i++) {
        uint32_t guard = DRV_SPI_HW_TIMEOUT;
        while (SPI_I2S_GetFlagStatus(h->reg, SPI_I2S_FLAG_TXE) == RESET) {
            if (--guard == 0u) {
                return BSP_ERR_TIMEOUT;
            }
        }
        SPI_I2S_SendData(h->reg, (tx != NULL) ? tx[i] : 0xFFu);

        guard = DRV_SPI_HW_TIMEOUT;
        while (SPI_I2S_GetFlagStatus(h->reg, SPI_I2S_FLAG_RXNE) == RESET) {
            if (--guard == 0u) {
                return BSP_ERR_TIMEOUT;
            }
        }
        {
            uint16_t in = SPI_I2S_ReceiveData(h->reg);
            if (rx != NULL) {
                rx[i] = (uint8_t)in;
            }
        }
    }
    return BSP_OK;
}

/* 硬件 SPI 单字节收发 */
static uint8_t hspi_transfer_byte(bsp_spi_t *self, uint8_t tx)
{
    uint8_t rx = 0xFFu;
    (void)hspi_transfer(self, &tx, &rx, 1u);
    return rx;
}

static const bsp_spi_ops_t s_hw_ops = { hspi_transfer, hspi_transfer_byte };

/**
 * @brief  创建硬件 SPI（SPI1 / SPI2）
 * @param  instance 1 或 2
 * @retval 句柄；参数非法或池满返回 NULL
 * @note   8 分频 = 72/8 = 9MHz。SPI 模式 0，和绝大多数 Flash / 传感器兼容。
 */
bsp_spi_t *drv_spi_hw_create(uint8_t instance)
{
    hw_spi_t       *h;
    SPI_TypeDef    *reg;
    GPIO_InitTypeDef init;

    if (((instance != 1u) && (instance != 2u)) || (s_hw_used >= DRV_SPI_POOL_SIZE)) {
        return NULL;
    }

    if (instance == 1u) {
        reg = SPI1;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA, ENABLE);
        init.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;        /* SCK=PA5, MOSI=PA7 */
        init.GPIO_Mode = GPIO_Mode_AF_PP;
        init.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &init);
        init.GPIO_Pin = GPIO_Pin_6;                     /* MISO=PA6 */
        init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(GPIOA, &init);
    } else {
        reg = SPI2;
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        init.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;      /* SCK=PB13, MOSI=PB15 */
        init.GPIO_Mode = GPIO_Mode_AF_PP;
        init.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOB, &init);
        init.GPIO_Pin = GPIO_Pin_14;                    /* MISO=PB14 */
        init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(GPIOB, &init);
    }

    {
        SPI_InitTypeDef cfg;
        cfg.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
        cfg.SPI_Mode = SPI_Mode_Master;
        cfg.SPI_DataSize = SPI_DataSize_8b;
        cfg.SPI_CPOL = SPI_CPOL_Low;                    /* 模式 0 */
        cfg.SPI_CPHA = SPI_CPHA_1Edge;
        cfg.SPI_NSS = SPI_NSS_Soft;
        cfg.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;  /* 72/8 = 9MHz */
        cfg.SPI_FirstBit = SPI_FirstBit_MSB;
        cfg.SPI_CRCPolynomial = 7u;
        SPI_Init(reg, &cfg);
    }
    SPI_Cmd(reg, ENABLE);

    h = &s_hw[s_hw_used];
    s_hw_used++;
    h->reg = reg;
    h->api.ops = &s_hw_ops;
    h->api.hw = h;
    return &h->api;
}
