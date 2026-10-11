# esp32-adc-detector

基于 ESP32-C5 的单屏信源星盘原型，沿用 PlatformIO + ESP-IDF 5.5.5。MPU6050 提供相对偏航，ST7796 SPI 屏显示固定天线与反向旋转的历史弧带；真实模式锁定一个 Wi-Fi AP 的 Beacon RSSI，不关联网络。本版不包含 BLE、信源列表或距离估算。

![LCD UI Design](docs/UI_Design.png)

实现要求见 [星盘 UI V1](docs/星盘UI_V1_AI实现要求.md)，配置与完整调试流程见 [原型说明](docs/starchart-prototype.md)。

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
| MOSI | **左侧 GPIO8** | 显示数据输入 |
| DC | **左侧 GPIO9** | 命令/数据选择 |
| SCK | **左侧 GPIO6** | SPI 时钟 |
| CS | **左侧 GPIO10** | 屏幕片选 |
| GND | **左侧 GND** | 地 |
| 3V3 | **左侧 3V3** | 模块供电 |
| BL | 不接 | 如果要控制背光，可接左侧 GPIO0 |
| 无标记脚 | 暂不接 | NA |

GPIO8、9、6、10 都在左侧相邻区域，接线比较集中。GPIO8、9 通过 GPIO 矩阵配置为 MOSI、DC 即可，不需要拘泥于开发板图上的默认复用功能。

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
屏幕：MOSI=8，SCK=6，CS=10，DC=9，BL预留=不接
MPU6050：SDA=23，SCL=24，INT可选=5
```

屏幕未引出已确认的 RESET 脚，初始化时可先尝试 ST7796 的软件复位；无标记脚继续留空，不能擅自接成 RESET。

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

当前已通过 ESP32-C5 编译和主机模拟测试，未烧录或完成实机验收。屏幕初始化、窗口偏移、RGB/BGR、背光电路、IMU 轴符号、真实 RSSI、实际 FPS 和十分钟设备稳定性仍需实测。
