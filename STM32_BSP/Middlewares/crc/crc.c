/**
 * @file    crc.c
 * @brief   校验算法实现。
 * @layer   Middlewares
 */
#include "crc.h"

/**
 * @brief  算 CRC-8（多项式 0x07，初值 0）
 * @param  data 数据
 * @param  len  长度
 * @retval 8 位校验值
 * @note   逐位算，不用查表：慢一点，但省 256 字节 ROM，串口帧这种小数据完全够用。
 */
uint8_t crc8_atm(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00u;
    uint16_t i;
    uint8_t b;

    if (data == NULL) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (b = 0u; b < 8u; b++) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((uint8_t)(crc << 1) ^ CRC8_POLY_ATM);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief  算 CRC-16/CCITT-FALSE（多项式 0x1021，初值 0xFFFF）
 * @param  data 数据
 * @param  len  长度
 * @retval 16 位校验值
 * @note   常用于升级包、大块数据校验。
 */
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = CRC16_INIT;
    uint16_t i;
    uint8_t b;

    if (data == NULL) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0u; b < 8u; b++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_CCITT_POLY);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief  算 CRC-16/MODBUS（多项式 0xA001 反向，初值 0xFFFF）
 * @param  data 数据
 * @param  len  长度
 * @retval 16 位校验值
 * @note   接 Modbus 设备（变频器、电表、部分传感器）时用这个。
 */
uint16_t crc16_modbus(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t b;

    if (data == NULL) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (b = 0u; b < 8u; b++) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc >> 1) ^ 0xA001u);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}
