/**
 * @file    key.h
 * @brief   非阻塞按键组件：状态机消抖 + 短按/长按/重复事件。
 * @layer   Components
 *
 * 对比原教程 Key_GetNum()：里面有 Delay_ms(20) 和 while(等待松开)，
 * 一次按键会把主循环卡死 20ms 以上。本实现只读一次电平，全靠时间戳。
 */
#ifndef KEY_H
#define KEY_H

#include "bsp.h"

#define KEY_DEBOUNCE_MS     20u     /* 消抖窗口 */
#define KEY_LONG_PRESS_MS   800u    /* 长按判定 */
#define KEY_REPEAT_MS       200u    /* 长按后重复触发周期 */

typedef enum {
    KEY_STATE_IDLE = 0,
    KEY_STATE_DEBOUNCE_PRESS,
    KEY_STATE_PRESSED,
    KEY_STATE_DEBOUNCE_RELEASE
} key_state_t;

typedef enum {
    KEY_EVT_NONE    = 0x00u,
    KEY_EVT_PRESS   = 0x01u,    /* 按下沿（已消抖） */
    KEY_EVT_RELEASE = 0x02u,    /* 抬起沿 */
    KEY_EVT_LONG    = 0x04u,    /* 达到长按阈值，一次 */
    KEY_EVT_REPEAT  = 0x08u     /* 长按保持期间的重复触发 */
} key_event_t;

typedef struct {
    bsp_gpio_t   *pin;          /**< 注入的引脚接口 */
    uint8_t       active_low;   /**< 1=低电平有效（按键接地） */
    key_state_t   state;
    uint8_t       raw_last;
    uint8_t       pressed;      /**< 消抖后的稳定状态 */
    bsp_time_ms_t t_edge;       /**< 最近一次原始电平变化时刻 */
    bsp_time_ms_t t_press;      /**< 稳定按下时刻 */
    bsp_time_ms_t t_repeat;     /**< 上次重复触发时刻 */
    uint8_t       long_fired;
    uint8_t       events;       /**< 待取事件位 */
} key_t;

void    key_init(key_t *k, bsp_gpio_t *pin, uint8_t active_low);
/** @brief 周期调用（建议 5~10ms），内部无任何阻塞 */
void    key_scan(key_t *k, bsp_time_ms_t now);
/** @brief 取走并清除累积事件 */
uint8_t key_take_events(key_t *k);
uint8_t key_is_pressed(const key_t *k);

#endif /* KEY_H */
