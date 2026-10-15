# esp32-adc-detector

基于 ESP32-C5 的单屏信源星盘原型，沿用 PlatformIO + ESP-IDF 5.5.5。MPU6050 提供相对偏航，ST7796 SPI 屏显示固定天线与反向旋转的历史弧带；真实模式锁定一个 Wi-Fi AP 的 Beacon RSSI，不关联网络。本版不包含 BLE、信源列表或距离估算。

![LCD UI Design](docs/UI_Design.png)

实现要求见 [星盘 UI V1](docs/星盘UI_V1_AI实现要求.md)，配置与完整调试流程见 [原型说明](docs/starchart-prototype.md)。

[星盘 UI 还原需求与实施计划](docs/starchart-ui-reconstruction-plan.md)：已实现大星盘布局（R=114、圆心 (160,156)）、16 px 脏格差异刷新、双区帧内重叠。P5 已删除压力测试开关、BOOT 菜单、FPS 覆盖层及重复实现，**默认直接运行星盘 UI_SIM**；姿态、信号、串口和显示任务全部保留。转动时约 120 FPS 是待实测验收目标，不是已达到的性能。

生产路径只有一套矩形 DMA 接口：CPU 序 RGB565 提交前一次 O2 交换、两个长期描述符、队列深度 2、每矩形排空关闭。40 KiB 内部 DMA 内存固定分为两个最大 32 行区，当前显示按 16 行分块，遵守查询的 12276 B 事务上限；不分配整屏缓存、不修改全局 tick、不跨帧预生成。迁移与验证见[计划第15节](docs/starchart-ui-reconstruction-plan.md#15-p5-执行记录)。

星盘按实际变化发送矩形，无变化轮不发送像素并让出至少一个 tick。串口 `dirty` 报告实际更新率、字节/窗口、draw/swap/cmd/dma_span/wait、总工作、空闲轮次、堆栈与帧间隔粗直方图。`dma_span` 包含重叠绘制，不是纯线上 DMA 时间；首次全屏与普通差异轮分别统计。

压力测试的历史 117.9 FPS / 8.428 ms 是关闭 UI/IMU/Wi-Fi 的 320×128 色带结果，不代表真实星盘。原始策略、O2 对照、横纹与闪烁反馈保留在已归档的 [LCD 刷新测试](docs/lcd-refresh-test.md)，不再提供策略切换。P2 旧整屏路径全圈约 1.8 FPS、绘制约 0.5 秒/帧的证据同样保留；当前差异刷新需独立实测。

以下为设备端共用 RGB565 渲染器输出的模拟预览，不是实机照片：

![Star Chart Simulation](docs/preview/starchart-sim.png)

[旋转与停止采样动画](docs/preview/starchart-sim.gif) · [等待信号状态](docs/preview/starchart-wait.png)

## 硬件

### ESP32-C5

- 芯片为 revision v1.2，16 MB Flash
- 外接定向天线，用于确定信号方向

![ESP32-C5 Pinout](docs/esp32-c5-pinout.png)

### MP6050

与天线刚性连接，用于确定天线朝向

![MP6050 Pinout](docs/MP6050-pinout.png)

### LCD (TK015F4000)

The TK015F4000 is a 1.54-inch 320×320 resolution SPI IPS display module driven by the ST7796 controller chip, which can be interfaced with an ESP32 microcontroller and the LVGL (Light and Versatile Graphics Library) framework.

![TFT Board Pinout](docs/TFT-pinout-320x320.png)

## Pinout

屏幕接线全部集中在 C5 左侧，MPU6050 的信号线集中在右侧。

**3.3 V 电源** 引脚共用：这块开发板右侧没有引出的 3V3，所以 MPU6050 的供电需要从左侧分出一根线。

| 屏幕转接板 | C5 引脚 | 功能 |
|---|---|---|
| MOSI | **左侧 GPIO7** | 显示数据输入 |
| DC | **左侧 GPIO9** | 命令/数据选择 |
| SCK | **左侧 GPIO6** | SPI 时钟 |
| CS | **左侧 GPIO10** | 屏幕片选 |
| GND | **左侧 GND** | 地 |
| 3V3 | **左侧 3V3** | 模块供电 |
| BL | **左侧 GPIO0**（用户已连接） | 本轮不调整背光，固件仍不驱动；不能用 GPIO 直接供背光电流 |
| 无标记脚 | 暂不接 | NA |

GPIO7、9、6、10 都在左侧相邻区域，接线比较集中。MOSI 使用 GPIO7 而非 GPIO8：GPIO7/6/10 正好是 C5 的 SPI2 IOMUX 引脚，三者齐全时整条总线走 IOMUX 而非 GPIO 矩阵，少一层偏斜（启动日志会打印实际走的是哪条路径）。DC=GPIO9 是普通输出，不受此限。

建议避开 GPIO15：**带 SPI PSRAM 的 C5 模组可能占用这个脚**。

**MPU6050 采用下面这组右侧接线：**

| MPU6050 | C5 接法 | 说明 |
|---|---|---|
| SDA | **右侧 GPIO23** | I²C 数据 |
| SCL | **右侧 GPIO24** | I²C 时钟 |
| GND | **右侧顶部 GND** | 地 |
| VCC | 共用左侧 3V3 电源分支 | 前提是模块支持 3.3 V 输入 |
| AD0 | 接 MPU6050 自身的 GND | 固定地址为 `0x68` |
| INT | 可选接右侧 GPIO5 | 初版轮询可不接 |
| XDA、XCL | 不接 | 当前不使用辅助 I²C |

GPIO23、24 在右侧相邻位置，适合把 MPU6050 放在开发板右边。

最终软件配置：

```text
屏幕：MOSI=7，SCK=6，CS=10，DC=9，BL=GPIO0（已接，软件不驱动）
MPU6050：SDA=23，SCL=24，INT可选=5
```

屏幕未引出已确认的 RESET 脚，初始化时可先尝试 ST7796 的软件复位；无标记脚继续留空，不能擅自接成 RESET。

保持 `CHART_LCD_BL=-1`，不驱动已连接 GPIO0 的 BL；`CHART_LCD_MIRROR_X=1` 校正用户报告的左右镜像。初始化/命令 40 MHz，星盘像素 80 MHz。80 MHz 超出 ST7796 规格（写周期下限 66 ns，约 15 MHz）5 倍以上，面板会晚一个位周期开始采样，像素流整体左移 1 位——实测表现为背景变亮、琥珀变粉。`CHART_LCD_SHIFT_BITS=1` 在每个窗口的像素流前补一个哑位抵消它（按窗口而非按块，块间接续进位），实机已确认颜色与 40 MHz 基线一致。但该偏移量可能随温度、供电或面板批次漂移，`CHART_LCD_SHIFT_DIAGNOSTIC=1` 可随时叠加位标尺与色卡复核；80 MHz 的长期可靠性仍未验收。上电诊断默认关闭，显式 `CHART_LCD_BOOT_DIAGNOSTIC=1` 才先以 40 MHz 显示颜色/边界/方向图 5 秒，再进入星盘，诊断也使用生产矩形接口。

排查像素信号完整性时，可在集中配置将 `CHART_LCD_FAST_SPI_HZ` 降为 40000000（此时补偿自动停用，因为 40 MHz 无需补偿），对照同一星盘路径；`chart_lcd_fast_pixels(false)` 也保留 40 MHz 能力，只能在窗口关闭后使用。SPI 发送成功不等于光学验收。详细日志口径见[原型说明](docs/starchart-prototype.md)。

## 构建与烧录

PlatformIO + ESP-IDF 5.5.5，默认环境 `esp32-c5`。通过固定的 pioarduino release 安装标准工具链

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run
pio run -t upload --upload-port /dev/cu.usbmodem101
pio device monitor --port /dev/cu.usbmodem101
```

串口以 `pio device list` 为准。

默认 `UI_SIM`，屏幕上明确显示「模拟」。所有 GPIO、显示窗口、IMU 轴、模式和目标 AP 集中在 `include/starchart_config.h`。切换真实模式前填写 `CHART_TARGET_SSID`；可选 BSSID 防止同名 AP 混淆。

```text
help          查看串口命令
mode ui       自动姿态 + 合成 RSSI
mode imu      MPU6050 + 合成 RSSI，静置 2.5 秒校准
mode live     MPU6050 + 固定 BSSID 的 Beacon RSSI
angle 30      模拟姿态设为 +30°，停止自动旋转
signal off    停止合成采样，盘仍跟随姿态
zero          重新置零并清空本轮
cal           重新初始化 MPU6050、校准并清空本轮
stats         查看角度、覆盖、包数及时间对齐统计
```

主机验证与预览生成：

```sh
sh tools/test_chart.sh
python3 -m pip install Pillow
python3 tools/build_chart_preview.py
```

P5 主机 ASan/UBSan、默认 C5、显式上电诊断及非空 SSID 编译通过；P1/P2 已获实机视觉反馈。最新烧录与串口数据见[执行记录](docs/starchart-ui-reconstruction-plan.md#15-p5-执行记录)，不能用历史压力测试成绩替代当前实测。真实 IMU 轴符号、RSSI、80 MHz 跨温度长期稳定性及十分钟并发仍需验收。
