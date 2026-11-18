/**
 * @file    line_sensor.h
 * @brief   多路循迹传感器组件（灰度/红外对管阵列）。
 * @layer   Components
 *
 * 输出加权位置误差（-1000 左偏 ... 0 居中 ... +1000 右偏），
 * 是 PID 巡线的"测量值"，与具体探头数量无关。
 */
#ifndef LINE_SENSOR_H
#define LINE_SENSOR_H

#include "bsp.h"

#define LINE_SENSOR_MAX         8u
#define LINE_SENSOR_POS_SCALE   1000

typedef struct {
    bsp_gpio_t *pins[LINE_SENSOR_MAX];
    uint8_t     count;
    uint8_t     active_low;     /**< 1=检测到黑线时输出低 */
    uint8_t     mask;           /**< 本帧各探头原始状态位 */
    uint8_t     active_count;   /**< 命中的探头数量 */
    int16_t     position;       /**< -1000 ~ +1000，正=偏右 */
    uint8_t     lost;           /**< 1=全部丢线（应触发保护） */
} line_sensor_t;

bsp_status_t line_sensor_init(line_sensor_t *ls, bsp_gpio_t **pins, uint8_t count,
                              uint8_t active_low);
void         line_sensor_update(line_sensor_t *ls);
int16_t      line_sensor_position(const line_sensor_t *ls);

#endif /* LINE_SENSOR_H */
