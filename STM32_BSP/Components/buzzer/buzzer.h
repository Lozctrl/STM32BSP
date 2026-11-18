/**
 * @file    buzzer.h
 * @brief   有源蜂鸣器组件：非阻塞鸣叫/提示音模式。
 * @layer   Components
 */
#ifndef BUZZER_H
#define BUZZER_H

#include "bsp.h"

#define BUZZER_BEEP_MS      80u
#define BUZZER_GAP_MS       120u

typedef struct {
    bsp_gpio_t   *pin;
    uint8_t       active_low;
    uint8_t       on;
    uint8_t       remain;           /**< 剩余鸣叫次数 */
    uint8_t       phase;            /**< 0=响 1=停 */
    bsp_time_ms_t t_next;
    uint16_t      on_ms;
    uint16_t      off_ms;
} buzzer_t;

void buzzer_init(buzzer_t *b, bsp_gpio_t *pin, uint8_t active_low);
void buzzer_set(buzzer_t *b, uint8_t on);
void buzzer_beep(buzzer_t *b, uint8_t times, uint16_t on_ms, uint16_t off_ms);
void buzzer_poll(buzzer_t *b, bsp_time_ms_t now);

#endif /* BUZZER_H */
