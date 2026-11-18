/**
 * @file    mpu6050.c
 * @brief   MPU6050 组件实现（14 字节突发读，比逐寄存器读快 12 倍）。
 * @layer   Components
 */
#include "mpu6050.h"
#include "mpu6050_reg.h"
#include <math.h>

#define MPU6050_RAD_TO_DEG      57.2957795f

/* 把两个字节拼成有符号 16 位。MPU6050 是大端（高字节在前），
   而 STM32 是小端，所以不能直接强转，必须手动拼 */
static int16_t mpu6050_be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief  读 WHO_AM_I 寄存器（芯片身份证，固定 0x68）
 * @param  m 对象
 * @retval 0x68 表示芯片正常；读失败返回 0xFF
 * @note   上电自检用它判断"IMU 到底焊上没有/线接对没有"。
 */
uint8_t mpu6050_whoami(mpu6050_t *m)
{
    uint8_t id = 0xFFu;

    if ((m == NULL) || (m->bus == NULL)) {
        return 0xFFu;
    }
    (void)bsp_i2c_mem_read(m->bus, m->addr7, MPU6050_REG_WHO_AM_I, &id, 1u);
    return id;
}

/**
 * @brief  初始化 MPU6050：解除睡眠、设采样率、设量程
 * @param  m     对象
 * @param  bus   I2C 总线（注入）
 * @param  addr7 7 位地址（AD0 接地是 0x68，接高是 0x69）
 * @retval BSP_OK / BSP_ERR_NODEV
 * @note   量程选的是 ±16g 和 ±2000°/s：
 *         量程越大分辨率越低，但机器人急停、碰撞时不容易超量程。
 */
bsp_status_t mpu6050_init(mpu6050_t *m, bsp_i2c_t *bus, uint8_t addr7)
{
    static const uint8_t cfg[][2] = {
        { MPU6050_REG_PWR_MGMT_1,   0x01u },    /* 解除睡眠，选择 X 轴陀螺时钟 */
        { MPU6050_REG_PWR_MGMT_2,   0x00u },    /* 六轴全开 */
        { MPU6050_REG_SMPLRT_DIV,   0x09u },    /* 1kHz / 10 = 100Hz */
        { MPU6050_REG_CONFIG,       0x06u },    /* DLPF 5Hz */
        { MPU6050_REG_GYRO_CONFIG,  0x18u },    /* ±2000 dps，16.4 LSB/dps */
        { MPU6050_REG_ACCEL_CONFIG, 0x18u }     /* ±16 g，2048 LSB/g */
    };
    uint8_t i;

    if ((m == NULL) || (bus == NULL)) {
        return BSP_ERR_PARAM;
    }

    m->bus = bus;
    m->addr7 = (uint8_t)(addr7 & 0x7Fu);
    m->accel_scale = 1.0f / 2048.0f;
    m->gyro_scale = 1.0f / 16.4f;
    m->read_err_cnt = 0u;
    m->present = 0u;
    m->data.ax = 0.0f; m->data.ay = 0.0f; m->data.az = 0.0f;
    m->data.gx = 0.0f; m->data.gy = 0.0f; m->data.gz = 0.0f;
    m->data.temp_c = 0.0f;

    for (i = 0u; i < (uint8_t)(sizeof(cfg) / sizeof(cfg[0])); i++) {
        if (bsp_i2c_mem_write(m->bus, m->addr7, cfg[i][0], &cfg[i][1], 1u) != BSP_OK) {
            return BSP_ERR_NODEV;
        }
    }

    if (mpu6050_whoami(m) != MPU6050_WHO_AM_I_VALUE) {
        return BSP_ERR_NODEV;
    }

    m->present = 1u;
    return BSP_OK;
}

/**
 * @brief  读一次六轴数据 + 温度
 * @param  m 对象
 * @retval BSP_OK / 总线错误（失败时读失败计数 +1）
 * @note   一次读 14 个字节（加速度 6 + 温度 2 + 陀螺 6）。
 *         为什么不去逐个读寄存器？因为每个寄存器一次 I2C 事务要 ~100us，
 *         14 次就是 1.4ms；一次性突发读只要 ~200us。
 */
bsp_status_t mpu6050_read(mpu6050_t *m)
{
    uint8_t raw[14];
    bsp_status_t st;

    if ((m == NULL) || (m->present == 0u)) {
        return BSP_ERR_PARAM;
    }

    st = bsp_i2c_mem_read(m->bus, m->addr7, MPU6050_REG_ACCEL_XOUT_H, raw, 14u);
    if (st != BSP_OK) {
        m->read_err_cnt++;
        return st;
    }

    m->data.ax = (float)mpu6050_be16(&raw[0])  * m->accel_scale;
    m->data.ay = (float)mpu6050_be16(&raw[2])  * m->accel_scale;
    m->data.az = (float)mpu6050_be16(&raw[4])  * m->accel_scale;
    m->data.temp_c = (float)mpu6050_be16(&raw[6]) / 340.0f + 36.53f;
    m->data.gx = (float)mpu6050_be16(&raw[8])  * m->gyro_scale;
    m->data.gy = (float)mpu6050_be16(&raw[10]) * m->gyro_scale;
    m->data.gz = (float)mpu6050_be16(&raw[12]) * m->gyro_scale;

    return BSP_OK;
}

/**
 * @brief  只用加速度计算俯仰角和横滚角
 * @param  m         对象
 * @param  pitch_deg 输出：俯仰角（度）
 * @param  roll_deg  输出：横滚角（度）
 * @note   静止时准，运动时会被加速度污染。要稳定姿态请用
 *         filter 模块里的互补滤波（flt_comp_update）把陀螺也融进来。
 */
void mpu6050_accel_angles(const mpu6050_t *m, float *pitch_deg, float *roll_deg)
{
    float ax;
    float ay;
    float az;

    if ((m == NULL) || (pitch_deg == NULL) || (roll_deg == NULL)) {
        return;
    }

    ax = m->data.ax;
    ay = m->data.ay;
    az = m->data.az;

    *pitch_deg = (float)atan2(-ax, (float)sqrt((double)(ay * ay + az * az))) * MPU6050_RAD_TO_DEG;
    *roll_deg  = (float)atan2(ay, az) * MPU6050_RAD_TO_DEG;
}
