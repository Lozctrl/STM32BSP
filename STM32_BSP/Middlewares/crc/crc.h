/**
 * @file    crc.h
 * @brief   校验算法（纯算法，逐位实现，Rom 占用极小）。
 * @layer   Middlewares
 */
#ifndef CRC_H
#define CRC_H

#include "bsp_types.h"

#define CRC8_POLY_ATM       0x07u
#define CRC16_CCITT_POLY    0x1021u
#define CRC16_INIT          0xFFFFu

uint8_t  crc8_atm(const uint8_t *data, uint16_t len);
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);
uint16_t crc16_modbus(const uint8_t *data, uint16_t len);

#endif /* CRC_H */
