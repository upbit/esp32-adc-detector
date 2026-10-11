# esp32-adc-detector

基于 ESP32-C5 的信号探测项目，当前阶段为最小硬件测试。识别方案采用 Espressif 提供 Wi‑Fi 混杂模式接收回调；BLE 扫描结果也直接包含设备地址、地址类型和 RSSI。

![UI Design](docs/UI_Design.png)

## 硬件

### ESP32-C5

- 芯片为 revision v1.2，16 MB Flash
- 外接定向天线，用于确定信号方向

![ESP32-C5 Pinout](docs/esp32-c5-pinout.png)

### MP6050

与天线刚性连接，用于确定天线朝向

![MP6050 Pinout](docs/MP6050-pinout.png)

### TFT 液晶屏

- 液晶屏分辨率 320x320

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
| BL | 预留不接 | 不要直接用 GPIO 提供原图标注的约 80 mA 背光电流 |
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
