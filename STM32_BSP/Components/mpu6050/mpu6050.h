/**
 * @file    mpu6050.h
 * @brief   六轴 IMU 组件（原始数据 → g / dps 物理量 + 加速度倾角）。
 * @layer   Components
 *
 * 依赖倒置：只持有 bsp_i2c_t*。软件 I2C（PB10/PB11）与硬件 I2C（PB6/PB7）
 * 的切换在 board.c 里一行完成，本文件无需改动。
 */
#ifndef MPU6050_H
#define MPU6050_H

#include "bsp.h"

#define MPU6050_ADDR_DEFAULT    0x68u   /* 7bit：AD0 接地 */

typedef struct {
    float ax, ay, az;   /**< 单位 g */
    float gx, gy, gz;   /**< 单位 °/s */
    float temp_c;       /**< 芯片温度 */
} imu_data_t;

typedef struct {
    bsp_i2c_t *bus;
    uint8_t    addr7;
    float      accel_scale;     /**< g/LSB */
    float      gyro_scale;      /**< (°/s)/LSB */
    uint8_t    present;         /**< WHO_AM_I 校验通过 */
    uint32_t   read_err_cnt;
    imu_data_t data;
} mpu6050_t;

bsp_status_t mpu6050_init(mpu6050_t *m, bsp_i2c_t *bus, uint8_t addr7);
uint8_t      mpu6050_whoami(mpu6050_t *m);
bsp_status_t mpu6050_read(mpu6050_t *m);
/** @brief 由加速度解算俯仰/横滚角（度），静止时可信 */
void         mpu6050_accel_angles(const mpu6050_t *m, float *pitch_deg, float *roll_deg);

#endif /* MPU6050_H */
