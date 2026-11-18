/**
 * @file    led.h
 * @brief   LED 组件：支持常亮/熄灭/翻转，以及非阻塞闪烁。
 * @layer   Components
 */
#ifndef LED_H
#define LED_H

#include "bsp.h"

typedef struct {
    bsp_gpio_t   *pin;
    uint8_t       active_low;
    uint8_t       on;
    uint8_t       blink_en;
    bsp_time_ms_t blink_period;
    bsp_time_ms_t t_last;
} led_t;

void led_init(led_t *led, bsp_gpio_t *pin, uint8_t active_low);
void led_set(led_t *led, uint8_t on);
void led_toggle(led_t *led);
void led_blink_start(led_t *led, bsp_time_ms_t half_period_ms);
void led_blink_stop(led_t *led, uint8_t final_on);
/** @brief 周期调用，驱动闪烁状态机 */
void led_poll(led_t *led, bsp_time_ms_t now);

#endif /* LED_H */
