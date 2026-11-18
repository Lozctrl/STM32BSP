/**
 * @file    led.c
 * @brief   LED 组件实现。
 * @layer   Components
 */
#include "led.h"

/**
 * @brief  初始化 LED
 * @param  led         对象
 * @param  pin         这个 LED 接在哪个引脚
 * @param  active_low  1=低电平点亮（大多数开发板是这样），0=高电平点亮
 * @note   把 pin 设成推挽输出，并先关掉灯（避免上电闪一下）。
 */
void led_init(led_t *led, bsp_gpio_t *pin, uint8_t active_low)
{
    if ((led == NULL) || (pin == NULL)) {
        return;
    }
    led->pin = pin;
    led->active_low = (active_low != 0u) ? 1u : 0u;
    led->on = 0u;
    led->blink_en = 0u;
    led->blink_period = 0u;
    led->t_last = 0u;
    (void)bsp_gpio_set_mode(pin, BSP_GPIO_MODE_OUT_PP);
    led_set(led, 0u);
}

/**
 * @brief  开灯 / 关灯
 * @param  led 对象
 * @param  on  1=亮，0=灭
 * @note   内部会按 active_low 换算成电平，所以调用方只管"亮不亮"，
 *         不用关心硬件是接高还是接低。
 */
void led_set(led_t *led, uint8_t on)
{
    bsp_gpio_level_t level;

    if ((led == NULL) || (led->pin == NULL)) {
        return;
    }
    led->on = (on != 0u) ? 1u : 0u;
    level = (led->on != led->active_low) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW;
    bsp_gpio_write(led->pin, level);
}

/**
 * @brief  翻转灯的状态（亮的变灭，灭的变亮）
 * @param  led 对象
 */
void led_toggle(led_t *led)
{
    if (led != NULL) {
        led_set(led, (uint8_t)(led->on ^ 1u));
    }
}

/**
 * @brief  开始闪灯（非阻塞，靠 led_poll 驱动）
 * @param  led            对象
 * @param  half_period_ms 半周期：传 100 就是 100ms 亮、100ms 灭
 * @note   这个函数只是置个标志，立刻返回。真正的翻转在 led_poll 里做。
 */
void led_blink_start(led_t *led, bsp_time_ms_t half_period_ms)
{
    if (led == NULL) {
        return;
    }
    led->blink_en = 1u;
    led->blink_period = (half_period_ms == 0u) ? 1u : half_period_ms;
    led->t_last = 0u;
}

/**
 * @brief  停止闪灯，并指定最后停在亮还是灭
 * @param  led       对象
 * @param  final_on  1=最后保持亮，0=保持灭
 */
void led_blink_stop(led_t *led, uint8_t final_on)
{
    if (led == NULL) {
        return;
    }
    led->blink_en = 0u;
    led_set(led, final_on);
}

/**
 * @brief  闪灯状态机，必须周期性调用（比如 100ms 一次）
 * @param  led 对象
 * @param  now 当前毫秒（bsp_time_ms 的返回值）
 * @note   只有"到点了"才翻转一次，没有 while 等待，所以不会卡住主循环。
 */
void led_poll(led_t *led, bsp_time_ms_t now)
{
    if ((led == NULL) || (led->blink_en == 0u)) {
        return;
    }
    if (bsp_time_elapsed(now, led->t_last, led->blink_period)) {
        led->t_last = now;
        led_toggle(led);
    }
}
