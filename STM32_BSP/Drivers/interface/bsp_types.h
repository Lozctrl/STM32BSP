/**
 * @file    bsp_types.h
 * @brief   平台无关公共类型定义。本文件不含任何芯片厂商头文件。
 * @layer   Drivers / Interface  (纯抽象契约层，换芯片时不需要修改)
 */
#ifndef BSP_TYPES_H
#define BSP_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* --------------------------------------------------------------------------
 * 编译期工具
 * -------------------------------------------------------------------------- */
#if defined(__CC_ARM)
    #define BSP_INLINE      static __inline
#elif defined(__GNUC__)
    #define BSP_INLINE      static __inline__
#else
    #define BSP_INLINE      static
#endif

#define BSP_UNUSED(x)       ((void)(x))
#define BSP_ARRAY_LEN(a)    (sizeof(a) / sizeof((a)[0]))
#define BSP_MIN(a, b)       (((a) < (b)) ? (a) : (b))
#define BSP_MAX(a, b)       (((a) > (b)) ? (a) : (b))
#define BSP_CLAMP(x, lo, hi) BSP_MIN(BSP_MAX((x), (lo)), (hi))
#define BSP_BIT(n)          (1UL << (n))
#define BSP_NEVER           (0xFFFFFFFFUL)

/* --------------------------------------------------------------------------
 * 统一返回码。所有接口层函数都返回 bsp_status_t，不允许返回裸 int。
 * -------------------------------------------------------------------------- */
typedef enum {
    BSP_OK          =  0,   /**< 成功 */
    BSP_ERR         = -1,   /**< 未分类错误 */
    BSP_ERR_PARAM   = -2,   /**< 参数非法 */
    BSP_ERR_TIMEOUT = -3,   /**< 超时 */
    BSP_ERR_BUSY    = -4,   /**< 设备忙 */
    BSP_ERR_NODEV   = -5,   /**< 器件不存在 / 无应答 */
    BSP_ERR_AGAIN   = -6,   /**< 数据未就绪，稍后重试（非阻塞语义） */
    BSP_ERR_NOMEM   = -7,   /**< 静态对象池耗尽 */
    BSP_ERR_CRC     = -8,   /**< 校验失败 */
    BSP_ERR_RANGE   = -9    /**< 数值越界 */
} bsp_status_t;

/* --------------------------------------------------------------------------
 * 时间量纲。整个框架只使用这两种时间单位，禁止出现"裸整数毫秒"。
 * -------------------------------------------------------------------------- */
typedef uint32_t bsp_time_ms_t;
typedef uint32_t bsp_time_us_t;

/* --------------------------------------------------------------------------
 * 数字滤波常用常量
 * -------------------------------------------------------------------------- */
#define BSP_U16_MAX     (0xFFFFu)
#define BSP_I16_MAX     (32767)
#define BSP_I16_MIN     (-32768)

#endif /* BSP_TYPES_H */
