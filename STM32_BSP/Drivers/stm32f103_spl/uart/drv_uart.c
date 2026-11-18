/**
 * @file    drv_uart.c
 * @brief   USART1/2/3 实现：中断只做"搬字节"，协议解析留给组件层。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_uart.h"
#include "drv_config.h"
#include "drv_time.h"
#include "ringbuf.h"

typedef struct {
    bsp_uart_t    api;
    USART_TypeDef *reg;
    uint8_t        instance;
    bsp_time_t    *time;
    ringbuf_t      rx;
    uint8_t        rx_storage[DRV_UART_RX_BUF_SIZE];
    volatile uint32_t overflow;
} stm32_uart_t;

static stm32_uart_t  s_pool[DRV_UART_POOL_SIZE];
static uint8_t       s_used = 0u;
static stm32_uart_t *s_slots[DRV_UART_POOL_SIZE];

/* ---------------- ops ---------------- */

/**
 * @brief  发送数据（阻塞，带超时）
 * @param  self       串口
 * @param  buf        数据
 * @param  len        字节数
 * @param  timeout_ms 超时
 * @retval BSP_OK / BSP_ERR_TIMEOUT
 * @note   发完最后还要等 TC（发送完成），否则函数返回时最后一个字节还在移位寄存器里，
 *         紧接着关外设或进低功耗就会把它丢掉。
 */
static bsp_status_t drv_uart_write(bsp_uart_t *self, const uint8_t *buf,
                                   uint16_t len, bsp_time_ms_t timeout_ms)
{
    stm32_uart_t *u = (stm32_uart_t *)self;
    bsp_time_ms_t t0;
    uint16_t i;

    if ((buf == NULL) || (len == 0u)) {
        return BSP_ERR_PARAM;
    }

    t0 = bsp_time_ms(u->time);
    for (i = 0u; i < len; i++) {
        while (USART_GetFlagStatus(u->reg, USART_FLAG_TXE) == RESET) {
            if ((timeout_ms != BSP_NEVER) &&
                bsp_time_elapsed(bsp_time_ms(u->time), t0, timeout_ms)) {
                return BSP_ERR_TIMEOUT;
            }
        }
        USART_SendData(u->reg, buf[i]);
    }

    while (USART_GetFlagStatus(u->reg, USART_FLAG_TC) == RESET) {
        if ((timeout_ms != BSP_NEVER) &&
            bsp_time_elapsed(bsp_time_ms(u->time), t0, timeout_ms)) {
            return BSP_ERR_TIMEOUT;
        }
    }
    return BSP_OK;
}

/* 从环形缓冲取数据，非阻塞。取不到就返回 0，绝不在这等 */
static uint16_t drv_uart_read(bsp_uart_t *self, uint8_t *buf, uint16_t cap)
{
    return rb_read(&((stm32_uart_t *)self)->rx, buf, cap);
}

/* 查还有多少字节没读 */
static uint16_t drv_uart_rx_available(bsp_uart_t *self)
{
    return rb_count(&((stm32_uart_t *)self)->rx);
}

/* 清空接收缓冲（协议重同步时用） */
static void drv_uart_rx_flush(bsp_uart_t *self)
{
    rb_clear(&((stm32_uart_t *)self)->rx);
}

static const bsp_uart_ops_t drv_uart_ops = {
    drv_uart_write,
    drv_uart_read,
    drv_uart_rx_available,
    drv_uart_rx_flush
};

/* ---------------- 中断分发 ---------------- */

/**
 * @brief  串口中断处理：只做"把字节塞进环形缓冲"这一件事
 * @param  u 串口对象
 * @note   缓冲满了就丢新字节（overflow++），而不是覆盖旧数据——
 *         丢最新的一个字节，前面的帧还有机会完整解析出来。
 *         另外还要处理溢出错误 ORE，否则错误标志会一直挂着，中断反复进。
 */
static void drv_uart_irq(stm32_uart_t *u)
{
    if (USART_GetITStatus(u->reg, USART_IT_RXNE) == SET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(u->reg);
        if (rb_write(&u->rx, &byte, 1u) == 0u) {
            u->overflow++;      /* 缓冲满：丢弃新字节而不是覆盖旧数据 */
        }
        USART_ClearITPendingBit(u->reg, USART_IT_RXNE);
    }
    if (USART_GetITStatus(u->reg, USART_IT_ORE) == SET) {
        (void)USART_ReceiveData(u->reg);
        USART_ClearITPendingBit(u->reg, USART_IT_ORE);
    }
}

/* 三个中断入口都转发给同一个处理函数，具体是哪个串口由槽位决定 */
void USART1_IRQHandler(void)
{
    if (s_slots[0] != NULL) {
        drv_uart_irq(s_slots[0]);
    }
}

void USART2_IRQHandler(void)
{
    if (s_slots[1] != NULL) {
        drv_uart_irq(s_slots[1]);
    }
}

void USART3_IRQHandler(void)
{
    if (s_slots[2] != NULL) {
        drv_uart_irq(s_slots[2]);
    }
}

/* ---------------- 工厂 ---------------- */

/**
 * @brief  创建串口
 * @param  instance 1~3（USART1/2/3）
 * @param  baud     波特率
 * @retval 句柄；参数非法返回 NULL；重复创建返回同一个实例
 * @note   引脚固定（USART1=PA9/PA10，USART2=PA2/PA3，USART3=PB10/PB11）。
 *         中断优先级设为 2：比 EXTI 低，比 SysTick 高。
 */
bsp_uart_t *drv_uart_create(uint8_t instance, uint32_t baud)
{
    stm32_uart_t   *u;
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef cfg;
    IRQn_Type       irqn;

    if ((instance < 1u) || (instance > DRV_UART_POOL_SIZE)) {
        return NULL;
    }
    if (s_slots[instance - 1u] != NULL) {
        return &s_slots[instance - 1u]->api;    /* 幂等：重复创建返回同一实例 */
    }

    u = NULL;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    switch (instance) {
    case 1u:
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
        gpio.GPIO_Mode = GPIO_Mode_AF_PP;   gpio.GPIO_Pin = GPIO_Pin_9;    /* TX */
        GPIO_Init(GPIOA, &gpio);
        gpio.GPIO_Mode = GPIO_Mode_IPU;     gpio.GPIO_Pin = GPIO_Pin_10;   /* RX */
        GPIO_Init(GPIOA, &gpio);
        irqn = USART1_IRQn;
        break;
    case 2u:
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
        gpio.GPIO_Mode = GPIO_Mode_AF_PP;   gpio.GPIO_Pin = GPIO_Pin_2;
        GPIO_Init(GPIOA, &gpio);
        gpio.GPIO_Mode = GPIO_Mode_IPU;     gpio.GPIO_Pin = GPIO_Pin_3;
        GPIO_Init(GPIOA, &gpio);
        irqn = USART2_IRQn;
        break;
    default:
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        gpio.GPIO_Mode = GPIO_Mode_AF_PP;   gpio.GPIO_Pin = GPIO_Pin_10;
        GPIO_Init(GPIOB, &gpio);
        gpio.GPIO_Mode = GPIO_Mode_IPU;     gpio.GPIO_Pin = GPIO_Pin_11;
        GPIO_Init(GPIOB, &gpio);
        irqn = USART3_IRQn;
        break;
    }

    if (s_used >= DRV_UART_POOL_SIZE) {
        return NULL;
    }
    u = &s_pool[s_used];
    s_used++;

    u->reg = (instance == 1u) ? USART1 : ((instance == 2u) ? USART2 : USART3);
    u->instance = instance;
    u->time = drv_time_get();
    u->overflow = 0u;
    u->api.ops = &drv_uart_ops;
    u->api.hw = u;
    (void)rb_init(&u->rx, u->rx_storage, DRV_UART_RX_BUF_SIZE);

    cfg.USART_BaudRate = baud;
    cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    cfg.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    cfg.USART_Parity = USART_Parity_No;
    cfg.USART_StopBits = USART_StopBits_1;
    cfg.USART_WordLength = USART_WordLength_8b;
    USART_Init(u->reg, &cfg);

    USART_ITConfig(u->reg, USART_IT_RXNE, ENABLE);
    USART_Cmd(u->reg, ENABLE);

    NVIC_SetPriority(irqn, 2u);
    NVIC_EnableIRQ(irqn);

    s_slots[instance - 1u] = u;
    return &u->api;
}

/**
 * @brief  读溢出次数（接收缓冲满导致丢字节的次数）
 * @param  instance 串口号
 * @retval 次数
 * @note   调试串口丢数据时的第一手线索：这个数一直涨，说明主循环读得太慢。
 */
uint32_t drv_uart_overflow_count(uint8_t instance)
{
    if ((instance < 1u) || (instance > DRV_UART_POOL_SIZE) || (s_slots[instance - 1u] == NULL)) {
        return 0u;
    }
    return s_slots[instance - 1u]->overflow;
}
