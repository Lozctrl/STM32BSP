/**
 * @file    key.c
 * @brief   非阻塞按键状态机实现。
 * @layer   Components
 */
#include "key.h"

/**
 * @brief  初始化按键
 * @param  k          对象
 * @param  pin        接在哪个引脚
 * @param  active_low 1=按下接地（带上拉电阻，最常见），0=按下接高
 * @note   消抖靠时间戳，不靠延时。所以这个函数里没有任何 Delay。
 */
void key_init(key_t *k, bsp_gpio_t *pin, uint8_t active_low)
{
    if ((k == NULL) || (pin == NULL)) {
        return;
    }
    k->pin = pin;
    k->active_low = (active_low != 0u) ? 1u : 0u;
    k->state = KEY_STATE_IDLE;
    k->raw_last = 0u;
    k->pressed = 0u;
    k->t_edge = 0u;
    k->t_press = 0u;
    k->t_repeat = 0u;
    k->long_fired = 0u;
    k->events = KEY_EVT_NONE;
}

/**
 * @brief  查询按键当前是不是"稳定按下"状态（已消抖）
 * @param  k 对象
 * @retval 1=按着，0=没按
 */
uint8_t key_is_pressed(const key_t *k)
{
    return (k != NULL) ? k->pressed : 0u;
}

/**
 * @brief  取走累积的事件，取完就清零
 * @param  k 对象
 * @retval 事件位组合：KEY_EVT_PRESS / RELEASE / LONG / REPEAT
 * @note   用法：ev = key_take_events(&k);
 *         if (ev & KEY_EVT_PRESS) {...}
 *         为什么用位组合？因为一个扫描周期里可能同时发生"抬起"和"短按"。
 */
uint8_t key_take_events(key_t *k)
{
    uint8_t ev;

    if (k == NULL) {
        return KEY_EVT_NONE;
    }
    /* 事件只由 key_scan()（主循环上下文）写入，因此读+清无需关中断。
       若将来把 key_scan 挪进 ISR，请改用 bsp_critical 保护这一段。 */
    ev = k->events;
    k->events = KEY_EVT_NONE;
    return ev;
}

/**
 * @brief  按键扫描状态机，必须周期性调用（建议 5~10ms 一次）
 * @param  k   对象
 * @param  now 当前毫秒（bsp_time_ms 的返回值）
 * @note   这是"非阻塞按键"的核心：
 *         只读一次电平、记录变化时刻，然后靠时间差判断是抖动还是真按下。
 *         原来的写法是 Delay_ms(20) + while(等到松手)，那样一次按键能卡住 20ms 以上。
 *
 *         状态流转：IDLE -> (稳定按下 20ms) -> PRESSED -> (稳定松开 20ms) -> IDLE
 *         PRESSED 期间超过 800ms 触发一次 LONG，之后每 200ms 触发 REPEAT。
 */
void key_scan(key_t *k, bsp_time_ms_t now)
{
    uint8_t raw;

    if ((k == NULL) || (k->pin == NULL)) {
        return;
    }

    raw = (bsp_gpio_read(k->pin) == BSP_GPIO_LEVEL_LOW) ? 1u : 0u;
    if (k->active_low == 0u) {
        raw = (uint8_t)(raw ^ 1u);      /* 高有效则翻转 */
    }

    /* 原始电平变化：只是重置消抖定时器，不做任何延时等待 */
    if (raw != k->raw_last) {
        k->raw_last = raw;
        k->t_edge = now;
    }

    switch (k->state) {
    case KEY_STATE_IDLE:
        if ((raw != 0u) && bsp_time_elapsed(now, k->t_edge, KEY_DEBOUNCE_MS)) {
            k->state = KEY_STATE_PRESSED;
            k->pressed = 1u;
            k->t_press = now;
            k->t_repeat = now;
            k->long_fired = 0u;
            k->events = (uint8_t)(k->events | KEY_EVT_PRESS);
        }
        break;

    case KEY_STATE_PRESSED:
        if (raw == 0u) {
            k->state = KEY_STATE_DEBOUNCE_RELEASE;
            k->t_edge = now;
        } else if (k->long_fired == 0u) {
            if (bsp_time_elapsed(now, k->t_press, KEY_LONG_PRESS_MS)) {
                k->long_fired = 1u;
                k->t_repeat = now;
                k->events = (uint8_t)(k->events | KEY_EVT_LONG);
            }
        } else if (bsp_time_elapsed(now, k->t_repeat, KEY_REPEAT_MS)) {
            k->t_repeat = now;
            k->events = (uint8_t)(k->events | KEY_EVT_REPEAT);
        }
        break;

    case KEY_STATE_DEBOUNCE_RELEASE:
        if (raw != 0u) {
            k->state = KEY_STATE_PRESSED;       /* 抖动，回到按下态 */
        } else if (bsp_time_elapsed(now, k->t_edge, KEY_DEBOUNCE_MS)) {
            k->state = KEY_STATE_IDLE;
            k->pressed = 0u;
            k->events = (uint8_t)(k->events | KEY_EVT_RELEASE);
        }
        break;

    case KEY_STATE_DEBOUNCE_PRESS:
    default:
        k->state = KEY_STATE_IDLE;
        break;
    }
}
