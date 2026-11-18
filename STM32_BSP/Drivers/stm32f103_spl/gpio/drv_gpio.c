/**
 * @file    drv_gpio.c
 * @brief   STM32F103 GPIO 实现（唯一允许出现 GPIOA/RCC/GPIO_Init 的地方之一）。
 * @layer   Drivers / stm32f103
 */
#include "stm32f10x.h"
#include "drv_gpio.h"
#include "drv_config.h"

typedef struct {
    bsp_gpio_t    api;      /* 必须放在首位：靠首成员地址实现"向上转型" */
    drv_gpio_hw_t hw;
} stm32_gpio_t;

static stm32_gpio_t s_pool[DRV_GPIO_POOL_SIZE];
static uint8_t      s_used = 0u;

/* ---------------- 内部映射表 ---------------- */

/* 'A' -> GPIOA、'B' -> GPIOB … 这是唯一出现"具体端口"的地方。
   换芯片时改这一个函数，上层所有 'A'/'B' 的写法都不用动 */
static GPIO_TypeDef *drv_gpio_port_of(char port)
{
    switch (port) {
    case 'A': return GPIOA;
    case 'B': return GPIOB;
    case 'C': return GPIOC;
    case 'D': return GPIOD;
#ifdef GPIOE
    case 'E': return GPIOE;
#endif
    default:  return NULL;
    }
}

/* 端口 -> 时钟使能位。F103 的每个 GPIO 端口要单独开时钟，忘了开就"写不动" */
static uint32_t drv_gpio_rcc_of(uint8_t port_index)
{
    switch (port_index) {
    case 0u: return RCC_APB2Periph_GPIOA;
    case 1u: return RCC_APB2Periph_GPIOB;
    case 2u: return RCC_APB2Periph_GPIOC;
    case 3u: return RCC_APB2Periph_GPIOD;
#ifdef GPIOE
    case 4u: return RCC_APB2Periph_GPIOE;
#endif
    default: return 0u;
    }
}

/* ---------------- ops 实现 ---------------- */

/**
 * @brief  配置引脚模式（把抽象的 mode 翻译成 SPL 的 GPIO_Mode）
 * @param  self 句柄
 * @param  mode 抽象模式
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   每次调用都会重新开一次端口时钟。开时钟是幂等的，多开无副作用，
 *         这样就不用在初始化流程里到处记"哪个端口开过没"。
 */
static bsp_status_t drv_gpio_set_mode(bsp_gpio_t *self, bsp_gpio_mode_t mode)
{
    stm32_gpio_t   *g = (stm32_gpio_t *)self;
    GPIO_InitTypeDef init;
    uint32_t        rcc;

    rcc = drv_gpio_rcc_of(g->hw.port_index);
    if (rcc == 0u) {
        return BSP_ERR_PARAM;
    }
    RCC_APB2PeriphClockCmd(rcc, ENABLE);     /* 幂等，多次调用无副作用 */

    init.GPIO_Pin   = g->hw.pin_mask;
    init.GPIO_Speed = GPIO_Speed_50MHz;

    switch (mode) {
    case BSP_GPIO_MODE_IN_FLOAT:    init.GPIO_Mode = GPIO_Mode_IN_FLOATING; break;
    case BSP_GPIO_MODE_IN_PULLUP:   init.GPIO_Mode = GPIO_Mode_IPU;         break;
    case BSP_GPIO_MODE_IN_PULLDOWN: init.GPIO_Mode = GPIO_Mode_IPD;         break;
    case BSP_GPIO_MODE_OUT_OD:      init.GPIO_Mode = GPIO_Mode_Out_OD;      break;
    case BSP_GPIO_MODE_OUT_PP:
    default:                        init.GPIO_Mode = GPIO_Mode_Out_PP;      break;
    }

    GPIO_Init((GPIO_TypeDef *)g->hw.port, &init);
    return BSP_OK;
}

/* 写电平。用 GPIO_WriteBit 而不是直接改 ODR，可读性更好，编译器也会优化成位带操作 */
static void drv_gpio_write(bsp_gpio_t *self, bsp_gpio_level_t level)
{
    stm32_gpio_t *g = (stm32_gpio_t *)self;
    GPIO_WriteBit((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask, (BitAction)level);
}

/* 读输入寄存器。注意：输出模式下读的是引脚真实电平，不是输出寄存器的值 */
static bsp_gpio_level_t drv_gpio_read(bsp_gpio_t *self)
{
    stm32_gpio_t *g = (stm32_gpio_t *)self;
    return (GPIO_ReadInputDataBit((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask) != 0u)
           ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW;
}

/* 翻转：先读输出寄存器再取反写回。不是原子操作，多上下文同时翻要加锁 */
static void drv_gpio_toggle(bsp_gpio_t *self)
{
    stm32_gpio_t *g = (stm32_gpio_t *)self;
    uint8_t cur = (GPIO_ReadOutputDataBit((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask) != 0u) ? 1u : 0u;
    GPIO_WriteBit((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask, (BitAction)(cur ^ 1u));
}

static const bsp_gpio_ops_t drv_gpio_ops = {
    drv_gpio_set_mode,
    drv_gpio_write,
    drv_gpio_read,
    drv_gpio_toggle
};

/* ---------------- 工厂 ---------------- */

/**
 * @brief  从静态池里"认领"一个引脚，返回可用的句柄
 * @param  port 'A'~'G'
 * @param  pin  0~15
 * @retval 引脚句柄；池满或参数非法返回 NULL
 * @note   为什么用"认领"而不是直接操作？
 *         因为上层拿到的是一个带 ops 的句柄，根本不知道背后是 GPIOA 还是别的。
 *         池子满说明板级配置写错了（引脚要得太多），启动时会直接卡住。
 */
bsp_gpio_t *drv_gpio_claim(char port, uint8_t pin)
{
    stm32_gpio_t *g;
    GPIO_TypeDef *p;

    if ((pin > 15u) || (s_used >= DRV_GPIO_POOL_SIZE)) {
        return NULL;
    }
    p = drv_gpio_port_of(port);
    if (p == NULL) {
        return NULL;
    }

    g = &s_pool[s_used];
    s_used++;

    g->hw.port       = (void *)p;
    g->hw.pin_mask   = (uint16_t)(1u << pin);
    g->hw.port_index = (uint8_t)(port - 'A');
    g->api.ops       = &drv_gpio_ops;
    g->api.hw        = &g->hw;

    return &g->api;
}

/**
 * @brief  已经认领了多少个引脚（自检/统计用）
 * @retval 数量
 */
uint8_t drv_gpio_used(void)
{
    return s_used;
}
