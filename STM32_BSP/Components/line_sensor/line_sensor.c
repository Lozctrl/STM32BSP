/**
 * @file    line_sensor.c
 * @brief   循迹阵列组件实现。
 * @layer   Components
 */
#include "line_sensor.h"

/**
 * @brief  初始化循迹阵列
 * @param  ls         对象
 * @param  pins       引脚数组（从左到右）
 * @param  count      几路（最多 LINE_SENSOR_MAX）
 * @param  active_low 1=压到黑线时输出低（常见）
 * @retval BSP_OK / BSP_ERR_PARAM
 */
bsp_status_t line_sensor_init(line_sensor_t *ls, bsp_gpio_t **pins, uint8_t count,
                              uint8_t active_low)
{
    uint8_t i;

    if ((ls == NULL) || (pins == NULL) || (count == 0u) || (count > LINE_SENSOR_MAX)) {
        return BSP_ERR_PARAM;
    }

    for (i = 0u; i < LINE_SENSOR_MAX; i++) {
        ls->pins[i] = (i < count) ? pins[i] : NULL;
    }
    ls->count = count;
    ls->active_low = (active_low != 0u) ? 1u : 0u;
    ls->mask = 0u;
    ls->active_count = 0u;
    ls->position = 0;
    ls->lost = 1u;

    for (i = 0u; i < count; i++) {
        (void)bsp_gpio_set_mode(ls->pins[i], BSP_GPIO_MODE_IN_PULLUP);
    }
    return BSP_OK;
}

/**
 * @brief  读一次所有探头，算出加权位置
 * @param  ls 对象
 * @note   算法：每个探头按物理位置给一个权重（左负右正），
 *         把"检测到线"的探头位置求平均，就是线相对车头的偏移量。
 *         多路同时压线时会自动落在中间，比"只看中间那一路"平滑得多。
 *
 *         全都没检测到 = 丢线，此时保持上一次的方向（而不是归零），
 *         让车"顺着刚才的趋势"找回线。
 */
void line_sensor_update(line_sensor_t *ls)
{
    uint8_t i;
    uint8_t mask = 0u;
    int32_t weight_sum = 0;
    int32_t pos_sum = 0;
    uint8_t count = 0u;

    if ((ls == NULL) || (ls->count == 0u)) {
        return;
    }

    for (i = 0u; i < ls->count; i++) {
        uint8_t raw = (bsp_gpio_read(ls->pins[i]) == BSP_GPIO_LEVEL_LOW) ? 1u : 0u;
        if (ls->active_low == 0u) {
            raw = (uint8_t)(raw ^ 1u);
        }
        if (raw != 0u) {
            int32_t pos;
            mask = (uint8_t)(mask | (uint8_t)(1u << i));
            /* 探头等间距，位置映射到 [-1000, +1000] */
            pos = ((2 * (int32_t)i) - (int32_t)(ls->count - 1u)) * LINE_SENSOR_POS_SCALE
                / (int32_t)(ls->count - 1u + (uint8_t)((ls->count == 1u) ? 1u : 0u));
            pos_sum += pos;
            weight_sum += 1;
            count++;
        }
    }

    ls->mask = mask;
    ls->active_count = count;
    ls->lost = (count == 0u) ? 1u : 0u;

    if ((weight_sum > 0) && (ls->count > 1u)) {
        ls->position = (int16_t)(pos_sum / weight_sum);
    } else {
        /* 全白（丢线）或只有一路：保持上一帧方向，交给上层 PID 的丢线保护处理 */
        if (ls->lost != 0u) {
            if (ls->position > 0) {
                ls->position = LINE_SENSOR_POS_SCALE;
            } else if (ls->position < 0) {
                ls->position = -LINE_SENSOR_POS_SCALE;
            } else {
                ls->position = 0;
            }
        } else {
            ls->position = 0;
        }
    }
}

/**
 * @brief  读最近一次算出的位置误差
 * @param  ls 对象
 * @retval -1000（线在左）~ 0（正中）~ +1000（线在右）
 * @note   这个值直接喂给循迹 PID 当测量值，所以单位是"千分比"而不是像素。
 */
int16_t line_sensor_position(const line_sensor_t *ls)
{
    return (ls != NULL) ? ls->position : 0;
}
