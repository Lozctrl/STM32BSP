/**
 * @file    drv_spi.c
 * @brief   SPI 主设备实现：软件（模式 0）+ 硬件 SPI1/SPI2。
 * @layer   Drivers / stm32f103_hal
 */
#include "stm32f1xx_hal.h"
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


/* ---------------- 硬件 SPI（HAL 后端） ---------------- */
typedef struct {
    bsp_spi_t        api;
    SPI_HandleTypeDef hspi;
} hal_spi_t;

static hal_spi_t s_hw[DRV_SPI_POOL_SIZE];
static uint8_t   s_hw_used = 0u;

#define DRV_SPI_HAL_TIMEOUT_MS  20u

/* 单字节收发：HAL 没有"发一个字节"的接口，用 1 字节的 TransmitReceive 凑 */
static uint8_t hspi_byte(hal_spi_t *h, uint8_t tx)
{
    uint8_t in = 0xFFu;

    (void)HAL_SPI_TransmitReceive(&h->hspi, &tx, &in, 1u, DRV_SPI_HAL_TIMEOUT_MS);
    return in;
}

/**
 * @brief  连续收发
 * @note   HAL 的 TransmitReceive 要求收发缓冲都不为 NULL，
 *         所以"只发"或"只收"时退化成逐字节调用，逻辑不变、只是慢一点。
 */
static bsp_status_t hspi_transfer(bsp_spi_t *self, const uint8_t *tx,
                                  uint8_t *rx, uint16_t len)
{
    hal_spi_t *h = (hal_spi_t *)self;
    uint16_t   i;

    if ((tx != NULL) && (rx != NULL)) {
        return (HAL_SPI_TransmitReceive(&h->hspi, (uint8_t *)tx, rx, len,
                                        DRV_SPI_HAL_TIMEOUT_MS) == HAL_OK)
               ? BSP_OK : BSP_ERR_TIMEOUT;
    }

    for (i = 0u; i < len; i++) {
        uint8_t in = hspi_byte(h, (tx != NULL) ? tx[i] : 0xFFu);
        if (rx != NULL) {
            rx[i] = in;
        }
    }
    return BSP_OK;
}

static uint8_t hspi_transfer_byte(bsp_spi_t *self, uint8_t tx)
{
    return hspi_byte((hal_spi_t *)self, tx);
}

static const bsp_spi_ops_t s_hw_ops = { hspi_transfer, hspi_transfer_byte };

/**
 * @brief  创建硬件 SPI（SPI1 / SPI2）
 * @param  instance 1 或 2
 * @retval 句柄；参数非法 / 池满 / HAL 初始化失败返回 NULL
 * @note   8 分频 = 72/8 = 9MHz，模式 0，和绝大多数 Flash、传感器兼容。
 */
bsp_spi_t *drv_spi_hw_create(uint8_t instance)
{
    hal_spi_t       *h;
    GPIO_InitTypeDef gpio;

    if (((instance != 1u) && (instance != 2u)) || (s_hw_used >= DRV_SPI_POOL_SIZE)) {
        return NULL;
    }

    h = &s_hw[s_hw_used];

    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    if (instance == 1u) {
        h->hspi.Instance = SPI1;
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_SPI1_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;         /* SCK=PA5, MOSI=PA7 */
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Mode = GPIO_MODE_INPUT;                /* MISO=PA6 浮空输入 */
        gpio.Pin  = GPIO_PIN_6;
        HAL_GPIO_Init(GPIOA, &gpio);
    } else {
        h->hspi.Instance = SPI2;
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_SPI2_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;       /* SCK=PB13, MOSI=PB15 */
        HAL_GPIO_Init(GPIOB, &gpio);
        gpio.Mode = GPIO_MODE_INPUT;                /* MISO=PB14 */
        gpio.Pin  = GPIO_PIN_14;
        HAL_GPIO_Init(GPIOB, &gpio);
    }

    h->hspi.Init.Mode              = SPI_MODE_MASTER;
    h->hspi.Init.Direction         = SPI_DIRECTION_2LINES;
    h->hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    h->hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;    /* 模式 0 */
    h->hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;
    h->hspi.Init.NSS               = SPI_NSS_SOFT;        /* 片选由器件自己管 */
    h->hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    h->hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    h->hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
    h->hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    h->hspi.Init.CRCPolynomial     = 7u;

    if (HAL_SPI_Init(&h->hspi) != HAL_OK) {
        return NULL;
    }

    h->api.ops = &s_hw_ops;
    h->api.hw  = h;
    s_hw_used++;

    return &h->api;
}
