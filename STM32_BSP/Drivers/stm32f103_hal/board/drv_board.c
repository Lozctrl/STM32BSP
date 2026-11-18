/**
 * @file    drv_board.c
 * @brief   板级早期初始化（HAL 后端）：时钟树 → HAL_Init → DWT 微秒延时。
 * @layer   Drivers / stm32f103_hal
 *
 * 顺序不能乱：
 *   1) 先配时钟（72MHz），因为 HAL_Init() 里的 SysTick 重装值是按当时的 HCLK 算的；
 *   2) 再 HAL_Init()，它会配 SysTick 1ms 并把 NVIC 分组设成 Group 4；
 *   3) 最后开 DWT，用于 I2C/SPI 的微秒级时序。
 *
 * 如果你的工程是用 CubeMX 生成的，main() 里已经做过 1) 和 2)，
 * 把 drv_config.h 里的 DRV_CFG_CONFIG_CLOCK / DRV_CFG_INIT_HAL 改成 0 即可；
 * 即使忘了调 HAL_Init()，本文件也会用 DWT 探测并自动补一次，避免 tick 恒为 0。
 */
#include "stm32f1xx_hal.h"
#include "drv_board.h"
#include "drv_config.h"
#include "drv_time.h"

#if (DRV_CFG_CONFIG_CLOCK != 0u)

/**
 * @brief  把系统时钟配到 72MHz（HSE 8MHz × 9），没有晶振就退到 HSI 64MHz
 * @note   为什么不用 CubeMX 的 SystemClock_Config？因为本库要能脱开 CubeMX 独立跑。
 *         有晶振用 HSE 更准（串口波特率才准），没晶振用 HSI 也能跑但误差大。
 */
static void drv_board_clock_init(void)
{
    RCC_OscInitTypeDef osc;
    RCC_ClkInitTypeDef clk;
    uint32_t pll_source;
    uint32_t pll_mul;

    /* 第一步：只试 HSE，先不配 PLL */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        pll_source = RCC_PLLSOURCE_HSE;
        pll_mul = RCC_PLL_MUL9;         /* 8MHz × 9 = 72MHz */
    } else {
        /* 没有外部晶振：用内部 RC，HSI/2 × 16 = 64MHz（F103 的上限） */
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState = RCC_PLL_NONE;
        (void)HAL_RCC_OscConfig(&osc);
        pll_source = RCC_PLLSOURCE_HSI_DIV2;
        pll_mul = RCC_PLL_MUL16;
    }

    /* 第二步：开 PLL */
    osc.OscillatorType = RCC_OSCILLATORTYPE_NONE;   /* 不动振荡器，只改 PLL */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = pll_source;
    osc.PLL.PLLMUL = pll_mul;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return;                          /* 配不上就维持现在的时钟，至少还能跑 */
    }

    /* 第三步：切 SYSCLK 到 PLL，并分频 APB1 = 36MHz（F103 的 APB1 上限） */
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    /* 72MHz 需要 2 个等待周期，配 1 会直接跑飞 */
    (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

#endif /* DRV_CFG_CONFIG_CLOCK */

void drv_board_init(void)
{
#if (DRV_CFG_CONFIG_CLOCK != 0u)
    drv_board_clock_init();
#endif

#if (DRV_CFG_INIT_HAL != 0u)
    /* HAL_Init 做三件事：复位外设、配 SysTick 为 1ms、NVIC 分组设为 Group 4 */
    (void)HAL_Init();
#endif

    /* DWT 微秒延时；下面的 tick 探测也要用它 */
    drv_time_init();

#if (DRV_CFG_INIT_HAL == 0u)
    /*
     * 约定是"main() 已经调过 HAL_Init()"。可工程模板常常漏掉这一句，
     * 而 bsp_time_ms() 直接取 HAL_GetTick()——tick 不走，按键消抖和所有
     * 超时都会失效（HAL 的超时循环还会变成死等）。
     * 这里用 DWT 量 2ms，tick 没动就补一次 HAL_Init()。
     */
    {
        uint32_t t0 = HAL_GetTick();

        bsp_time_delay_us(drv_time_get(), 2000u);
        if (HAL_GetTick() == t0) {
            (void)HAL_Init();
        }
    }
#endif
}
