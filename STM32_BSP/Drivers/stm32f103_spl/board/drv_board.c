/**
 * @file    drv_board.c
 * @brief   板级早期初始化：时基 + 中断优先级分组。
 * @layer   Drivers / stm32f103
 *
 * 时钟树由 SystemInit()（启动文件里已调用）配到 72MHz，这里不重复配置。
 */
#include "stm32f10x.h"
#include "drv_board.h"
#include "drv_time.h"
#include "drv_config.h"

/**
 * @brief  板级早期初始化：中断优先级分组 + 系统节拍 + GPIO 时钟
 * @note   必须在创建任何组件之前调用，因为后面的东西都依赖"有毫秒时间"。
 *         时钟树（72MHz）由启动文件的 SystemInit() 配好，这里不重复配。
 */
void drv_board_init(void)
{
    /* 2 位抢占 + 2 位子优先级：控制类中断可抢占通信中断 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    drv_time_init();

    /* 让所有用到的 GPIO 端口时钟一次性打开，避免运行期再开关时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
}
