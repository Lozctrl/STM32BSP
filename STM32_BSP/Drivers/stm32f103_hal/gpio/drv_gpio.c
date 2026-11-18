/**
 * @file    drv_gpio.c
 * @brief   STM32F103 GPIO 实现（HAL 后端）。
 * @layer   Drivers / stm32f103_hal
 *
 * 和 SPL 版的差别只有"调哪个库"：
 *   SPL: RCC_APB2PeriphClockCmd + GPIO_Init + GPIO_WriteBit
 *   HAL: __HAL_RCC_GPIOx_CLK_ENABLE + HAL_GPIO_Init + HAL_GPIO_WritePin
 * 对上层来说，drv_gpio_claim() 的用法一模一样。
 */
#include "stm32f1xx_hal.h"
#include "drv_gpio.h"
#include "drv_config.h"

typedef struct {
    bsp_gpio_t    api;      /* 必须放首位：靠首成员地址实现"向上转型" */
    drv_gpio_hw_t hw;
} hal_gpio_t;

static hal_gpio_t s_pool[DRV_GPIO_POOL_SIZE];
static uint8_t    s_used = 0u;

/* 端口字符 -> 寄存器基址。换芯片时只改这个 switch */
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

/* HAL 不会替你开端口时钟，必须自己开。开多次是安全的 */
static void drv_gpio_clk_enable(uint8_t port_index)
{
    switch (port_index) {
    case 0u: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case 1u: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case 2u: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case 3u: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
#ifdef GPIOE
    case 4u: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
#endif
    default: break;
    }
}

/**
 * @brief  配置引脚模式
 * @param  self 句柄
 * @param  mode 抽象模式
 * @retval BSP_OK / BSP_ERR_PARAM
 * @note   HAL 把"输入 + 上下拉"拆成了两个字段（Mode 和 Pull），
 *         所以这里的映射比 SPL 版多一层。
 */
static bsp_status_t drv_gpio_set_mode(bsp_gpio_t *self, bsp_gpio_mode_t mode)
{
    hal_gpio_t      *g = (hal_gpio_t *)self;
    GPIO_InitTypeDef init;

    drv_gpio_clk_enable(g->hw.port_index);

    init.Pin   = g->hw.pin_mask;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    init.Pull  = GPIO_NOPULL;

    switch (mode) {
    case BSP_GPIO_MODE_IN_FLOAT:
        init.Mode = GPIO_MODE_INPUT;
        init.Pull = GPIO_NOPULL;
        break;
    case BSP_GPIO_MODE_IN_PULLUP:
        init.Mode = GPIO_MODE_INPUT;
        init.Pull = GPIO_PULLUP;
        break;
    case BSP_GPIO_MODE_IN_PULLDOWN:
        init.Mode = GPIO_MODE_INPUT;
        init.Pull = GPIO_PULLDOWN;
        break;
    case BSP_GPIO_MODE_OUT_OD:
        init.Mode = GPIO_MODE_OUTPUT_OD;
        break;
    case BSP_GPIO_MODE_OUT_PP:
    default:
        init.Mode = GPIO_MODE_OUTPUT_PP;
        break;
    }

    HAL_GPIO_Init((GPIO_TypeDef *)g->hw.port, &init);
    return BSP_OK;
}

/* 写电平：bsp 的枚举值 0/1 和 GPIO_PIN_RESET/SET 的数值一致，可以直接转 */
static void drv_gpio_write(bsp_gpio_t *self, bsp_gpio_level_t level)
{
    hal_gpio_t *g = (hal_gpio_t *)self;
    HAL_GPIO_WritePin((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask, (GPIO_PinState)level);
}

static bsp_gpio_level_t drv_gpio_read(bsp_gpio_t *self)
{
    hal_gpio_t *g = (hal_gpio_t *)self;
    return (HAL_GPIO_ReadPin((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask) != GPIO_PIN_RESET)
           ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW;
}

static void drv_gpio_toggle(bsp_gpio_t *self)
{
    hal_gpio_t *g = (hal_gpio_t *)self;
    HAL_GPIO_TogglePin((GPIO_TypeDef *)g->hw.port, g->hw.pin_mask);
}

static const bsp_gpio_ops_t drv_gpio_ops = {
    drv_gpio_set_mode,
    drv_gpio_write,
    drv_gpio_read,
    drv_gpio_toggle
};

/**
 * @brief  认领一个引脚
 * @param  port 'A'~'G'
 * @param  pin  0~15
 * @retval 句柄；池满或参数非法返回 NULL
 */
bsp_gpio_t *drv_gpio_claim(char port, uint8_t pin)
{
    hal_gpio_t   *g;
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

uint8_t drv_gpio_used(void)
{
    return s_used;
}
