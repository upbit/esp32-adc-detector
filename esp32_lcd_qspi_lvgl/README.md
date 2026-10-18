# ESP32 QSPI LCD (TK018F3716) + LVGL 使用说明

## 项目简介

本项目基于 ESP32-S3 芯片，通过 QSPI 接口驱动 TK018F3716（SPD2010/TK0134）1.18 英寸圆形 LCD 屏幕（240x300），并使用 LVGL 9.2.0 图形库实现 UI 显示。UI 界面通过 SquareLine Studio 1.4.1 设计生成，包含天气、温湿度、GIF 动画等展示功能，支持 FT5x06 电容触摸。

## 硬件要求

| 项目 | 规格 |
|------|------|
| 主控芯片 | ESP32-S3 |
| LCD 屏幕型号 | TK018F3716（驱动 IC: SPD2010 / TK0134） |
| 屏幕分辨率 | 240 x 300 |
| 屏幕接口 | QSPI（4线 SPI） |
| 触摸 IC | FT5x06（I2C 接口） |
| 背光控制 | GPIO0 |

## 引脚定义

### LCD QSPI 引脚

| 功能 | GPIO |
|------|------|
| SPI CLK (PCLK) | GPIO6 |
| SPI CS | GPIO16 |
| DATA0 | GPIO15 |
| DATA1 | GPIO7 |
| DATA2 | GPIO11 |
| DATA3 | GPIO12 |
| RST | 未使用 (GPIO_NUM_NC) |
| 背光 (BK_LIGHT) | GPIO0 |

### 触摸 I2C 引脚

| 功能 | GPIO |
|------|------|
| I2C SCL | GPIO5 |
| I2C SDA | GPIO4 |
| 触摸中断 | 未使用 (GPIO_NUM_NC) |

## 软件环境

- **ESP-IDF**: v5.5.0
- **LVGL**: 9.2.0
- **esp_lvgl_port**: ^2.4.4
- **SquareLine Studio**: 1.4.1（用于 UI 设计）

## 项目结构

```
esp32_lcd_qspi_lvgl/
├── CMakeLists.txt                  # 顶层 CMake 构建文件
├── sdkconfig                       # 项目配置
├── main/
│   ├── main.c                      # 主程序入口（LCD/LVGL/触摸初始化）
│   ├── CMakeLists.txt              # main 组件构建文件
│   ├── idf_component.yml           # 组件依赖声明
│   ├── lcd_tk0134/
│   │   ├── lcd_tk0134.c            # TK0134 LCD 驱动实现
│   │   └── lcd_tk0134.h            # TK0134 LCD 驱动头文件
│   ├── myui/
│   │   ├── myui.c                  # 自定义 UI 逻辑（温湿度/天气/GIF）
│   │   └── myui.h                  # 自定义 UI 头文件
│   └── ui/                         # SquareLine Studio 生成的 UI 代码
│       ├── ui.c / ui.h             # UI 初始化与变量定义
│       ├── ui_events.c / ui_events.h  # UI 事件处理
│       ├── ui_helpers.c / ui_helpers.h # UI 辅助函数
│       ├── screens/
│       │   ├── ui_Screen1.c        # 主屏幕（天气/温湿度/GIF）
│       │   └── ui_Screen2.c        # 第二屏幕（滑块控制）
│       ├── images/                 # 图片资源（天气图标等）
│       ├── fonts/                  # 字体资源
│       ├── gif/                    # GIF 动画资源
│       └── components/             # UI 组件
├── components/
│   ├── espressif__esp_lcd_spd2010/ # SPD2010 LCD 驱动组件
│   ├── espressif__esp_lcd_touch/   # 触摸通用组件
│   └── espressif__esp_lcd_touch_ft5x06/  # FT5x06 触摸驱动组件
└── .vscode/                        # VS Code 配置
```

## 编译与烧录

### 1. 环境准备

确保已安装 ESP-IDF v5.5.0 并正确配置环境变量：

```bash
# 设置 IDF_PATH 环境变量
export IDF_PATH=/path/to/esp-idf
# 或 Windows 下
set IDF_PATH=C:\path\to\esp-idf
```

### 2. 编译项目

```bash
idf.py build
```

### 3. 烧录

```bash
idf.py -p COM端口 flash
```

例如：
```bash
idf.py -p COM3 flash
```

### 4. 查看串口输出

```bash
idf.py -p COM端口 monitor
```

### 5. 一键编译+烧录+监控

```bash
idf.py -p COM端口 flash monitor
```

## 功能说明

### 运行模式

在 [main.c](main/main.c) 中通过 `test_lvgl` 宏控制运行模式：

```c
#define test_lvgl 0     // 0: LCD 纯色测试模式, 1: LVGL UI 模式
```

- **`test_lvgl = 0`**（默认）：LCD 纯色条带测试，交替显示两种颜色条带图案，用于验证 LCD 硬件连接
- **`test_lvgl = 1`**：LVGL UI 模式，加载完整的图形界面

### 触摸功能

在 [main.c](main/main.c) 中通过 `TOUCH_ENABLE` 宏控制触摸：

```c
#define TOUCH_ENABLE 0  // 0: 禁用触摸, 1: 启用触摸
```

触摸默认关闭，如需启用请改为 `1`。

### UI 界面

#### Screen1 - 主屏幕

主屏幕包含以下功能区域：

- **天气显示**：7种天气图标循环切换（晴、多云、阴、阵雨、雨、雪、雾），每秒更新一次
- **温度显示**：温度数值和进度条，每500ms递增5，0~100循环
- **湿度显示**：湿度数值和进度条，每500ms递增2，0~100循环
- **GIF 动画**：两个 GIF 动画容器
- **页面切换按钮**：点击 Button1 切换到 Screen2

#### Screen2 - 控制屏幕

- **滑块控件**：拖动滑块改变数值，实时显示在标签上
- **返回按钮**：点击 Button2 返回 Screen1

### 自定义 UI 逻辑

自定义逻辑在 [myui.c](main/myui/myui.c) 中实现，`my_ui_init()` 函数在 LVGL 锁内调用，负责：

1. `gif_show()` - 加载 GIF 动画到容器
2. `set_temp()` - 定时器驱动温度更新（500ms 周期）
3. `set_humi()` - 定时器驱动湿度更新（500ms 周期）
4. `weather_img_show()` - 预加载天气图标
5. `weather_show()` - 定时器驱动天气切换（1000ms 周期）

## 如何修改 UI

### 使用 SquareLine Studio 修改

1. 打开 SquareLine Studio 1.4.1
2. 导入项目（确保 LVGL 版本设置为 9.1.0）
3. 修改 UI 布局
4. 导出代码到 `main/ui/` 目录
5. 重新编译烧录

### 手动修改

- 修改 UI 布局：编辑 `main/ui/screens/` 下的文件
- 修改业务逻辑：编辑 `main/myui/myui.c`
- 修改 LCD 驱动：编辑 `main/lcd_tk0134/lcd_tk0134.c`
- 修改引脚配置：编辑 `main/main.c` 中的宏定义

## 添加新图片资源

1. 将图片文件放入项目资源目录
2. 使用 LVGL 图片转换工具转换为 C 数组
3. 将生成的 `.c` 文件放入 `main/ui/images/` 目录
4. 在 `main/ui/ui.h` 中声明 `LV_IMG_DECLARE`
5. 在代码中使用 `lv_img_set_src()` 设置图片

## 常见问题

### 1. 编译失败 - 找不到组件

运行以下命令更新组件依赖：
```bash
idf.py reconfigure
```

### 2. 屏幕白屏/无显示

- 检查 QSPI 引脚连接是否正确
- 确认背光引脚 (GPIO0) 是否正常输出
- 检查 LCD 初始化序列是否正确

### 3. 触摸不响应

- 确认 `TOUCH_ENABLE` 已设为 `1`
- 检查 I2C 引脚 (SCL: GPIO5, SDA: GPIO4) 连接
- 确认触摸 IC 地址（默认 0x15）

### 4. LVGL 显示异常

- 确认 `LV_COLOR_DEPTH` 为 16（RGB565）
- 检查 `sdkconfig` 中 PSRAM 和 DMA 相关配置
- 确认屏幕旋转和镜像设置

### 5. 烧录失败

- 检查串口连接和 COM 端口号
- 尝试降低波特率：`idf.py -p COM端口 -b 115200 flash`
- 按住 BOOT 键后按 EN 键进入下载模式

## 关键配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `TEST_LCD_H_RES` | 240 | 屏幕水平分辨率 |
| `TEST_LCD_V_RES` | 300 | 屏幕垂直分辨率 |
| `TEST_LCD_BIT_PER_PIXEL` | 16 | 像素位深（RGB565） |
| `TEST_PIN_NUM_BK_LIGHT` | GPIO0 | 背光引脚 |
| LVGL task_priority | 4 | LVGL 任务优先级 |
| LVGL task_stack | 8196 | LVGL 任务栈大小 |
| LVGL buffer_size | 240*80 | 显示缓冲区大小 |
| LVGL double_buffer | false | 双缓冲开关 |
