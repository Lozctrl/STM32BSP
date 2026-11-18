/**
 * @file    drv_uart.c
 * @brief   USART1/2/3 实现（HAL 后端）。
 * @layer   Drivers / stm32f103_hal
 *
 * HAL 的串口接收是"回调"模型，和 SPL 里自己写 IRQHandler 不一样：
 *   HAL_UART_Receive_IT(1 字节) → 每收 1 字节进一次 HAL_UART_RxCpltCallback
 *   我们在回调里把字节塞进环形缓冲，然后**重新武装**接收。
 * 对上层来说完全没差别：bsp_uart_read() 依旧非阻塞、依旧从环形缓冲取。
 */
#include "stm32f1xx_hal.h"
#include "drv_uart.h"
#include "drv_config.h"
#include "ringbuf.h"

typedef struct {
    bsp_uart_t         api;
    UART_HandleTypeDef huart;
    uint8_t            instance;
    ringbuf_t          rx;
    uint8_t            rx_storage[DRV_UART_RX_BUF_SIZE];
    uint8_t            rx_byte;         /* HAL 逐字节接收的落点 */
    volatile uint32_t  overflow;
} hal_uart_t;

static hal_uart_t  s_pool[DRV_UART_POOL_SIZE];
static hal_uart_t *s_slots[DRV_UART_POOL_SIZE];
static uint8_t     s_used = 0u;

/* HAL 回调只给句柄，这里反查回我们的对象 */
static hal_uart_t *drv_uart_from_handle(UART_HandleTypeDef *h)
{
    uint8_t i;

    for (i = 0u; i < s_used; i++) {
        if (&s_pool[i].huart == h) {
            return &s_pool[i];
        }
    }
    return NULL;
}

/* ---------------- ops ---------------- */

static bsp_status_t drv_uart_write(bsp_uart_t *self, const uint8_t *buf,
                                   uint16_t len, bsp_time_ms_t timeout_ms)
{
    hal_uart_t *u = (hal_uart_t *)self;
    HAL_StatusTypeDef st;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }

    st = HAL_UART_Transmit(&u->huart, (uint8_t *)buf, len, timeout_ms);
    return (st == HAL_OK) ? BSP_OK : BSP_ERR_TIMEOUT;
}

static uint16_t drv_uart_read(bsp_uart_t *self, uint8_t *buf, uint16_t cap)
{
    return rb_read(&((hal_uart_t *)self)->rx, buf, cap);
}

static uint16_t drv_uart_rx_available(bsp_uart_t *self)
{
    return rb_count(&((hal_uart_t *)self)->rx);
}

static void drv_uart_rx_flush(bsp_uart_t *self)
{
    rb_clear(&((hal_uart_t *)self)->rx);
}

static const bsp_uart_ops_t drv_uart_ops = {
    drv_uart_write,
    drv_uart_read,
    drv_uart_rx_available,
    drv_uart_rx_flush
};

/* ---------------- HAL 回调（弱函数，这里覆盖） ---------------- */

/**
 * @brief  收到 1 个字节
 * @note   ① 塞进环形缓冲；② 立刻重新武装接收。
 *         忘记 ② 的话，收完第一个字节就再也收不到数据了——这是 HAL 串口最经典的坑。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    hal_uart_t *u = drv_uart_from_handle(huart);

    if (u == NULL) {
        return;
    }
    if (rb_write(&u->rx, &u->rx_byte, 1u) == 0u) {
        u->overflow++;      /* 缓冲满：丢新字节，保住已有数据 */
    }
    (void)HAL_UART_Receive_IT(&u->huart, &u->rx_byte, 1u);
}

/**
 * @brief  串口出错（溢出、噪声、帧错）
 * @note   F1 上一旦发生溢出错误，接收会被 HAL 停掉，必须重新武装。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    hal_uart_t *u = drv_uart_from_handle(huart);

    if (u == NULL) {
        return;
    }
    __HAL_UART_CLEAR_OREFLAG(&u->huart);
    (void)HAL_UART_Receive_IT(&u->huart, &u->rx_byte, 1u);
}

/* ---------------- 中断入口 ---------------- */

void USART1_IRQHandler(void)
{
    if (s_slots[0] != NULL) {
        HAL_UART_IRQHandler(&s_slots[0]->huart);
    }
}

void USART2_IRQHandler(void)
{
    if (s_slots[1] != NULL) {
        HAL_UART_IRQHandler(&s_slots[1]->huart);
    }
}

void USART3_IRQHandler(void)
{
    if (s_slots[2] != NULL) {
        HAL_UART_IRQHandler(&s_slots[2]->huart);
    }
}

/* ---------------- 工厂 ---------------- */

/**
 * @brief  创建串口
 * @param  instance 1~3
 * @param  baud     波特率
 * @retval 句柄；参数非法 / 池满 / HAL 初始化失败返回 NULL
 * @note   引脚固定：USART1=PA9/PA10，USART2=PA2/PA3，USART3=PB10/PB11。
 *         中断优先级设为 2（比 EXTI 低、比 SysTick 高）。
 */
bsp_uart_t *drv_uart_create(uint8_t instance, uint32_t baud)
{
    hal_uart_t      *u;
    GPIO_InitTypeDef gpio;
    IRQn_Type        irqn;

    if ((instance < 1u) || (instance > DRV_UART_POOL_SIZE)) {
        return NULL;
    }
    if (s_slots[instance - 1u] != NULL) {
        return &s_slots[instance - 1u]->api;        /* 幂等 */
    }
    if (s_used >= DRV_UART_POOL_SIZE) {
        return NULL;
    }

    u = &s_pool[s_used];

    gpio.Mode  = GPIO_MODE_AF_PP;       /* TX 复用推挽 */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (instance) {
    case 1u:
        u->huart.Instance = USART1;
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART1_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_9;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Mode = GPIO_MODE_INPUT;    /* RX 上拉输入 */
        gpio.Pull = GPIO_PULLUP;
        gpio.Pin  = GPIO_PIN_10;
        HAL_GPIO_Init(GPIOA, &gpio);
        irqn = USART1_IRQn;
        break;
    case 2u:
        u->huart.Instance = USART2;
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART2_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_2;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_PULLUP;
        gpio.Pin  = GPIO_PIN_3;
        HAL_GPIO_Init(GPIOA, &gpio);
        irqn = USART2_IRQn;
        break;
    default:
        u->huart.Instance = USART3;
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_USART3_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_10;
        HAL_GPIO_Init(GPIOB, &gpio);
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_PULLUP;
        gpio.Pin  = GPIO_PIN_11;
        HAL_GPIO_Init(GPIOB, &gpio);
        irqn = USART3_IRQn;
        break;
    }

    u->huart.Init.BaudRate     = baud;
    u->huart.Init.WordLength   = UART_WORDLENGTH_8B;
    u->huart.Init.StopBits     = UART_STOPBITS_1;
    u->huart.Init.Parity       = UART_PARITY_NONE;
    u->huart.Init.Mode         = UART_MODE_TX_RX;
    u->huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    u->huart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&u->huart) != HAL_OK) {
        return NULL;
    }

    u->instance = instance;
    u->overflow = 0u;
    u->api.ops  = &drv_uart_ops;
    u->api.hw   = u;
    (void)rb_init(&u->rx, u->rx_storage, DRV_UART_RX_BUF_SIZE);

    s_slots[instance - 1u] = u;
    s_used++;

    HAL_NVIC_SetPriority(irqn, 2u, 0u);
    HAL_NVIC_EnableIRQ(irqn);

    /* 武装第一字节：之后全靠回调自己续 */
    (void)HAL_UART_Receive_IT(&u->huart, &u->rx_byte, 1u);

    return &u->api;
}

uint32_t drv_uart_overflow_count(uint8_t instance)
{
    if ((instance < 1u) || (instance > DRV_UART_POOL_SIZE) || (s_slots[instance - 1u] == NULL)) {
        return 0u;
    }
    return s_slots[instance - 1u]->overflow;
}
