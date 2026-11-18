/**
 * @file    buzzer.c
 * @brief   蜂鸣器组件实现。
 * @layer   Components
 */
#include "buzzer.h"

/**
 * @brief  初始化蜂鸣器
 * @param  b          对象
 * @param  pin        接在哪个引脚
 * @param  active_low 1=低电平响（很多有源蜂鸣器模块是高电平响，传 0）
 */
void buzzer_init(buzzer_t *b, bsp_gpio_t *pin, uint8_t active_low)
{
    if ((b == NULL) || (pin == NULL)) {
        return;
    }
    b->pin = pin;
    b->active_low = (active_low != 0u) ? 1u : 0u;
    b->on = 0u;
    b->remain = 0u;
    b->phase = 1u;
    b->t_next = 0u;
    b->on_ms = BUZZER_BEEP_MS;
    b->off_ms = BUZZER_GAP_MS;
    (void)bsp_gpio_set_mode(pin, BSP_GPIO_MODE_OUT_PP);
    buzzer_set(b, 0u);
}

/**
 * @brief  立刻开/关蜂鸣器（不经过时间控制）
 * @param  b  对象
 * @param  on 1=响，0=停
 */
void buzzer_set(buzzer_t *b, uint8_t on)
{
    bsp_gpio_level_t level;

    if ((b == NULL) || (b->pin == NULL)) {
        return;
    }
    b->on = (on != 0u) ? 1u : 0u;
    level = (b->on != b->active_low) ? BSP_GPIO_LEVEL_HIGH : BSP_GPIO_LEVEL_LOW;
    bsp_gpio_write(b->pin, level);
}

/**
 * @brief  嘀几声（非阻塞）
 * @param  b       对象
 * @param  times   响几声
 * @param  on_ms   每声持续多久
 * @param  off_ms  两声之间停多久
 * @note   这个函数只把参数记下来并立刻响第一声，剩下的交给 buzzer_poll。
 *         所以"响三声"不会卡住程序 300ms。
 */
void buzzer_beep(buzzer_t *b, uint8_t times, uint16_t on_ms, uint16_t off_ms)
{
    if (b == NULL) {
        return;
    }
    b->remain = times;
    b->on_ms = (on_ms == 0u) ? BUZZER_BEEP_MS : on_ms;
    b->off_ms = (off_ms == 0u) ? BUZZER_GAP_MS : off_ms;
    b->phase = 0u;
    b->t_next = 0u;     /* 立即进入第一声 */
    if (times > 0u) {
        buzzer_set(b, 1u);
    }
}

/**
 * @brief  蜂鸣器状态机，必须周期性调用
 * @param  b   对象
 * @param  now 当前毫秒
 */
void buzzer_poll(buzzer_t *b, bsp_time_ms_t now)
{
    if (b == NULL) {
        return;
    }

    if (b->remain == 0u) {
        return;
    }

    if (b->t_next == 0u) {
        b->t_next = now + ((b->phase == 0u) ? b->on_ms : b->off_ms);
        return;
    }

    if ((int32_t)(now - b->t_next) >= 0) {
        if (b->phase == 0u) {
            /* 一声结束 */
            buzzer_set(b, 0u);
            b->remain--;
            b->phase = 1u;
            if (b->remain == 0u) {
                b->t_next = 0u;
                return;
            }
            b->t_next = now + b->off_ms;
        } else {
            buzzer_set(b, 1u);
            b->phase = 0u;
            b->t_next = now + b->on_ms;
        }
    }
}
