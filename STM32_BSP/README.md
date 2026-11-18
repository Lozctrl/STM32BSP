# 🧰 STM32_BSP — 我的嵌入式零件盒

> 一个纯代码库：只有零件（.c/.h），没有工程文件、没有编译器产物、没有厂商库。
> 用的时候把整个文件夹放进工程目录，在 `CMakeLists.txt` 里挑需要的零件加进去就行（见第 4 节）。

## 🎯 1. 这是什么

跨项目复用的 STM32 板级零件库。每个文件夹 = 一个零件，里面只有这个零件的 `.c` 和 `.h`。
换单片机平台（ESP32 / CH32 / GD32）时，**只重写 `Drivers/`**，上面的零件原样能用。

## 🚨 2. 铁律（写代码前先看这段）

1. **接口分离**：`Components/` 里严禁出现 `HAL_I2C_Transmit`、`GPIO_WritePin`、`Delay_ms` 这类底层调用。
   底层操作一律用**函数指针**在 `.h` 里声明，初始化时由外面注入。
2. **结构体封装**：不用全局变量。所有状态（引脚、句柄、状态机）装在 `xxx_t` 里，用指针传参。
3. **非阻塞**：不许 `Delay_ms` 死等。消抖、超时、状态机全靠毫秒时间戳。
4. **防冲突**：宏和枚举必须带模块前缀（`KEY_DEBOUNCE_MS`、`OLED_ADDR_DEFAULT`），不许叫 `TIMEOUT`、`MAX`。
5. **文档同步**：每加/改一个零件，同一次提交里把下面 §5 的清单更新掉。

## 📂 3. 目录结构

```text
STM32_BSP/
│
├── Components/                 零件区：每种硬件一个文件夹
│   ├── key/                    key.c  key.h
│   ├── oled/                   oled.c  oled.h  oled_font.c  oled_font.h
│   ├── motor/                  motor.c  motor.h          (TB6612)
│   ├── servo/                  servo.c  servo.h
│   ├── encoder/                encoder.c  encoder.h
│   ├── mpu6050/                mpu6050.c  mpu6050.h  mpu6050_reg.h
│   ├── led/  buzzer/           led.c  buzzer.c ...
│   ├── uart_link/              uart_link.c  uart_link.h   (HC-05 / 蓝牙)
│   ├── w25q64/                 w25q64.c  w25q64.h         (SPI Flash)
│   ├── adc_sensor/             光敏 / 电池电压
│   ├── line_sensor/            循迹阵列
│   └── pulse_counter/          对射红外 / 霍尔计数
│
├── Drivers/                    工具区：跟芯片绑死的东西，换芯片就换这一层
│   ├── interface/              13 份"插座"定义（bsp_gpio.h / bsp_i2c.h ...）两套共用
│   ├── stm32f103_spl/          ← 后端 A：标准库版（江协教程 / 老工程用这个）
│   │   ├── gpio/  i2c/  spi/  pwm/  uart/  adc/  encoder/
│   │   ├── time/  exti/  flash/  rtc/  wdt/  critical/
│   │   └── board/              板级初始化 + 全局配置（drv_board.c  drv_config.h）
│   └── stm32f103_hal/          ← 后端 B：HAL 版（CubeMX 工程用这个）
│       └── （与上面 14 个文件夹一一对应，API 完全相同）
│
├── Middlewares/                算法区：纯数学，不碰任何硬件
│   ├── pid/  filter/  ramp/    控制与滤波
│   ├── ringbuf/  frame/  crc/  通信基础件
│   ├── sched/                  软定时调度
│   └── numfmt/                 数字转字符串（不用 printf）
│
└── README.md
```

**没有的东西**（有意为之）：`Core/`（厂商库）、`build/`（编译产物）、`Tools/`（脚本）、
`Applications/`（你的项目业务，按约定不入库）。

## 🔧 4. 怎么装进你的工程
### 4.1 用 CMake（推荐：VS Code / 命令行）

本库不带工程文件，只给源文件。把用到的模块拼成列表，比手抄一长串 include 路径可靠：

```cmake
# 1) BSP 在哪（和工程目录并排）
get_filename_component(BSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../STM32_BSP" ABSOLUTE)

# 2) 选后端：stm32f103_hal 或 stm32f103_spl（只能二选一）
set(BSP_BACKEND "stm32f103_hal")
set(BSP_MODULES adc board critical encoder exti flash gpio i2c pwm rtc spi time uart wdt)

set(BSP_INCLUDE_DIRS "${BSP_DIR}/Drivers/interface")
set(BSP_SOURCES "")
foreach(mod IN LISTS BSP_MODULES)
  list(APPEND BSP_INCLUDE_DIRS "${BSP_DIR}/Drivers/${BSP_BACKEND}/${mod}")
  list(APPEND BSP_SOURCES      "${BSP_DIR}/Drivers/${BSP_BACKEND}/${mod}/drv_${mod}.c")
endforeach()

# 3) 零件按需取用
set(BSP_COMPONENT_SOURCES
  "${BSP_DIR}/Components/oled/oled.c"
  "${BSP_DIR}/Components/oled/oled_font.c"
  "${BSP_DIR}/Middlewares/numfmt/numfmt.c"
  "${BSP_DIR}/Middlewares/ringbuf/ringbuf.c")

target_include_directories(你的目标名 PRIVATE
  ${BSP_INCLUDE_DIRS}
  "${BSP_DIR}/Components/oled"
  "${BSP_DIR}/Middlewares/numfmt"
  "${BSP_DIR}/Middlewares/ringbuf")

target_sources(你的目标名 PRIVATE ${BSP_SOURCES} ${BSP_COMPONENT_SOURCES})
```

改完 `CMakeLists.txt` 要**重新 configure**（`cmake --preset debug`）再 build，
否则 Ninja 不知道有新增文件，会报一堆 undefined reference。

VS Code 侧不用手写 `c_cpp_properties.json`：把 `CMAKE_EXPORT_COMPILE_COMMANDS` 设为 `ON`，
补全和跳转全部由 `build/<preset>/compile_commands.json` 提供（CMake Tools 会自动读）。

### 4.2 手工维护工程（备用，不推荐）

只有在必须手工管理文件列表时才走这条路：

* **include 路径**：`Drivers/interface` + 你选的那套后端下**所有**模块目录（不要两套同时加）。
* **源文件**：用到哪个模块才加哪个 `drv_xxx.c`，漏一个就是 undefined reference。

```powershell
$bsp = '.\STM32_BSP'; $backend = 'stm32f103_hal'    # 或 'stm32f103_spl'
@("$bsp\Drivers\interface") +
  (Get-ChildItem "$bsp\Drivers\$backend" -Directory | Select-Object -ExpandProperty FullName) |
  Select-Object -Unique
```

> ⚠ **不要再用 Keil 那一套**（`Add Existing Files`、`Options for Target → Include Paths`）。
> 本库不带 `.uvprojx`，统一用 CMake + Ninja + `arm-none-eabi-gcc`，一份构建描述就够，
> 免得同一个工程在 IDE 和命令行里编出两个不一样的结果。

### 4.3 你的工程要有芯片库

本库不自带厂商库，按后端选：

* `Drivers/stm32f103_spl/` → 工程里要有 **stm32f10x.h（CMSIS + SPL）**
* `Drivers/stm32f103_hal/` → 工程里要有 **stm32f1xx_hal.h（CubeMX 生成的 HAL）**

### 4.4 选后端：SPL 还是 HAL？

**只能选一个后端**，不要把 `stm32f103_spl/` 和 `stm32f103_hal/` 同时加进工程——
两边都实现了 `drv_gpio_claim()` 这些同名函数，会链接冲突。

| | `stm32f103_spl/` | `stm32f103_hal/` |
|---|---|---|
| 配哪个工程 | 江协模板 / 任何 SPL 工程 | CubeMX 生成的工程 |
| 芯片头文件 | `stm32f10x.h` | `stm32f1xx_hal.h` |
| 编译宏 | `STM32F10X_MD`, `USE_STDPERIPH_DRIVER` | `STM32F103xB`, `USE_HAL_DRIVER` |
| 毫秒节拍 | 自己的 SysTick（`drv_time.c`） | 借 `HAL_GetTick()`（没初始化会自动补，见下） |
| 谁配时钟 | 启动文件的 `SystemInit()` | `drv_board_init()`（可关）；微秒延时与 PWM 按**实际 HCLK** 运行时换算 |

**HAL 后端的三个开关**（在 `Drivers/stm32f103_hal/board/drv_config.h`）：

```c
#define DRV_CFG_INIT_HAL       1u   /* 0 = 你的 main() 已经调过 HAL_Init()      */
#define DRV_CFG_CONFIG_CLOCK   1u   /* 0 = CubeMX 已经配过 72MHz 时钟           */
#define DRV_CFG_EXTI_OWNS_IRQ  1u   /* 0 = CubeMX 已在 stm32f1xx_it.c 生成 EXTIx_IRQHandler */
```

用 CubeMX 生成的工程时，把这三个都改成 `0`，本库就"只当零件用"，不抢初始化。

即使 `DRV_CFG_INIT_HAL=0` 但工程忘了调 `HAL_Init()`，`drv_board_init()` 也会用 DWT 量 2ms
探测 `HAL_GetTick()`：发现 tick 不走就自动补一次 `HAL_Init()`——否则按键消抖和所有超时都会失效
（HAL 的超时循环还会变成死等）。

## 📦 5. 现有零件清单

> 格式：`- [x] 模块（文件夹）: 干什么用。对外 API: 函数列表。`
> 新增零件后必须在这里加一行，否则视为没做完。

### Components（零件区）

- [x] **key**（`Components/key/`）: 按键消抖，出短按/长按/连发事件，全程不卡主循环。API: `key_init()`, `key_scan()`, `key_take_events()`, `key_is_pressed()`。
- [x] **led**（`Components/led/`）: 指示灯，常亮/翻转/闪灯。API: `led_init()`, `led_set()`, `led_toggle()`, `led_blink_start()`, `led_blink_stop()`, `led_poll()`。
- [x] **buzzer**（`Components/buzzer/`）: 有源蜂鸣器，能"嘀几声"，不阻塞。API: `buzzer_init()`, `buzzer_set()`, `buzzer_beep()`, `buzzer_poll()`。
- [x] **oled**（`Components/oled/`）: SSD1306 128x64，显存里画好再分页发，不拖慢控制环。API: `oled_init()`, `oled_show_char()`, `oled_show_string()`, `oled_show_string_padded()`, `oled_show_u32()`, `oled_show_i32()`, `oled_show_f32()`, `oled_progress_bar()`, `oled_draw_pixel()`, `oled_draw_hline()`, `oled_draw_rect()`, `oled_fill()`, `oled_clear()`, `oled_mark_all_dirty()`, `oled_flush()`, `oled_flush_step()`。
- [x] **motor**（`Components/motor/`）: TB6612 一路直流电机，正转/反转/滑行/刹车。API: `motor_init()`, `motor_set_speed()`, `motor_coast()`, `motor_brake()`, `motor_get_speed()`。
- [x] **servo**（`Components/servo/`）: 舵机，给角度就转（内部换算成脉宽）。API: `servo_init()`, `servo_set_angle()`, `servo_set_pulse()`, `servo_get_angle()`。
- [x] **encoder**（`Components/encoder/`）: 编码器测速，出 计数/秒、转速或线速度。API: `encoder_init()`, `encoder_set_wheel()`, `encoder_set_unit()`, `encoder_set_lpf()`, `encoder_update()`, `encoder_speed()`, `encoder_total()`, `encoder_reset()`。
- [x] **mpu6050**（`Components/mpu6050/`）: 六轴陀螺仪，一次读 14 字节，出 g 和 °/s，还能算倾角。API: `mpu6050_init()`, `mpu6050_whoami()`, `mpu6050_read()`, `mpu6050_accel_angles()`。
- [x] **uart_link**（`Components/uart_link/`）: 串口收发 + 打包解包（带转义和 CRC8），蓝牙模块直接接。API: `uart_link_init()`, `uart_link_poll()`, `uart_link_send()`, `uart_link_send_str()`。
- [x] **w25q64**（`Components/w25q64/`）: 8MB SPI Flash，读/写/擦扇区，忙等有超时。API: `w25q64_init()`, `w25q64_jedec_id()`, `w25q64_read()`, `w25q64_write()`, `w25q64_page_program()`, `w25q64_erase_sector()`, `w25q64_erase_chip()`, `w25q64_is_busy()`, `w25q64_wait_ready()`。
- [x] **adc_sensor**（`Components/adc_sensor/`）: 模拟量采集（光敏、电池电压），带平均和回差判断。API: `adc_sensor_init()`, `adc_sensor_update()`, `adc_sensor_value()`, `adc_sensor_above()`。
- [x] **line_sensor**（`Components/line_sensor/`）: 多路循迹，直接给 -1000~+1000 的偏移量和"丢线"标志。API: `line_sensor_init()`, `line_sensor_update()`, `line_sensor_position()`。
- [x] **pulse_counter**（`Components/pulse_counter/`）: 边沿计数（对射红外、霍尔），中断里只加一，主循环取数。API: `pulse_counter_init()`, `pulse_counter_take()`, `pulse_counter_total()`。

### Middlewares（算法区）

- [x] **pid**（`Middlewares/pid/`）: PID 控制，抗积分饱和、微分先行、输出限幅。API: `pid_init()`, `pid_set_limits()`, `pid_set_flags()`, `pid_update()`, `pid_reset()`, `pid_output()`。
- [x] **filter**（`Middlewares/filter/`）: 滑动平均、一阶低通、互补滤波（陀螺+加速度融合）。API: `flt_ma_init()`, `flt_ma_reset()`, `flt_ma_push()`, `flt_lpf_init_hz()`, `flt_lpf_init_alpha()`, `flt_lpf_reset()`, `flt_lpf_update()`, `flt_comp_init()`, `flt_comp_reset()`, `flt_comp_update()`。
- [x] **ringbuf**（`Middlewares/ringbuf/`）: 环形缓冲（收数据用），生产者消费者各管一头，不用加锁。API: `rb_init()`, `rb_write()`, `rb_read()`, `rb_peek()`, `rb_count()`, `rb_free()`, `rb_clear()`。
- [x] **frame**（`Middlewares/frame/`）: 串口打包/解包，帧头+长度+内容+CRC8+帧尾。API: `frame_encode()`, `frame_dec_init()`, `frame_dec_push()`, `frame_dec_feed()`。
- [x] **crc**（`Middlewares/crc/`）: 校验码计算（CRC8 / CRC16）。API: `crc8_atm()`, `crc16_ccitt()`, `crc16_modbus()`。
- [x] **ramp**（`Middlewares/ramp/`）: 斜率限制，让速度慢慢加上去，保护电机和齿轮。API: `ramp_init()`, `ramp_reset()`, `ramp_update()`。
- [x] **sched**（`Middlewares/sched/`）: 软定时器，按毫秒周期跑任务，卡一次也不会连爆。API: `sched_init()`, `sched_add()`, `sched_enable()`, `sched_run()`。
- [x] **numfmt**（`Middlewares/numfmt/`）: 数字变字符串（不引 printf），能定宽补空格。API: `numfmt_u32()`, `numfmt_i32()`, `numfmt_f32()`, `numfmt_hex()`, `numfmt_pad()`。

### Drivers（工具区）

> `Drivers/` 下有两套后端：`stm32f103_spl/`（SPL）和 `stm32f103_hal/`（HAL）。
> 下面的模块在**两套里同名同 API**，选一套加进工程即可。

- [x] **interface**（`Drivers/interface/`）: 13 张"插座图纸"，两套后端共用，换芯片时只保留它。API: `bsp_gpio_*`, `bsp_i2c_*`, `bsp_spi_*`, `bsp_pwm_*`, `bsp_uart_*`, `bsp_adc_*`, `bsp_encoder_*`, `bsp_flash_*`, `bsp_rtc_*`, `bsp_wdt_*`, `bsp_time_*`, `bsp_critical_*`, `bsp_exti_*`。
- [x] **gpio**（`Drivers/<后端>/gpio/`）: 要一个引脚就用。API: `drv_gpio_claim()`, `drv_gpio_used()`。
- [x] **i2c**（`Drivers/<后端>/i2c/`）: 两种 I2C——GPIO 模拟（含总线卡死恢复）/ 硬件 I2C。API: `drv_i2c_soft_create()`, `drv_i2c_hw_create()`。
- [x] **spi**（`Drivers/<后端>/spi/`）: 两种 SPI——软件 / 硬件。API: `drv_spi_soft_create()`, `drv_spi_hw_create()`。
- [x] **pwm**（`Drivers/<后端>/pwm/`）: 定时器 PWM，给频率和通道就行。API: `drv_pwm_create()`。
- [x] **uart**（`Drivers/<后端>/uart/`）: USART1/2/3，中断只收字节，不占主循环。API: `drv_uart_create()`, `drv_uart_overflow_count()`。
- [x] **adc**（`Drivers/<后端>/adc/`）: 单通道 AD 采样。API: `drv_adc_create()`。
- [x] **encoder**（`Drivers/<后端>/encoder/`）: 定时器编码器模式（TIM3/TIM4）。API: `drv_encoder_create()`。
- [x] **time**（`Drivers/<后端>/time/`）: 毫秒节拍 + 微秒延时（SPL：自己的 SysTick；HAL：`HAL_GetTick` + DWT）。API: `drv_time_init()`, `drv_time_get()`, `drv_time_tick_isr()`。
- [x] **critical**（`Drivers/<后端>/critical/`）: 关中断/开中断，保护共享变量。API: `drv_critical_get()`。
- [x] **exti**（`Drivers/<后端>/exti/`）: 外部中断注册表，任意引脚挂回调。API: `drv_exti_get()`。
- [x] **flash**（`Drivers/<后端>/flash/`）: 内部 Flash 存参数（最后一页）。API: `drv_flash_get()`。
- [x] **rtc**（`Drivers/<后端>/rtc/`）: 实时时钟，掉电走时。API: `drv_rtc_get()`。
- [x] **wdt**（`Drivers/<后端>/wdt/`）: 独立看门狗。API: `drv_wdt_get()`。
- [x] **board**（`Drivers/<后端>/board/`）: 板级初始化 + 全局配置宏。API: `drv_board_init()`。

## ✅ 6. 状态

- **全部代码已加中文注释**：每个函数有 `@brief` / `@param` / `@retval` / `@note`，
  关键行有"为什么这么写"的说明（注释占比约 27%）。逻辑一行没改，只加注释。
- 全部零件：`armcc`（Cortex-M3）逐个编译两套后端（SPL / HAL 1.8.7）均 0 错误 0 警告；宿主机 gcc 编译 0 警告。
- 已验证的行为：PID 闭环（目标 1720 → 实测 1736 counts/s）、串口帧环回、OLED 显存渲染、按键长/短按、看门狗心跳。
