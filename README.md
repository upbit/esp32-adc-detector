# esp32-adc-detector (ESP32-C5)

基于 ESP32-C5 的设备识别项目，当前阶段为最小硬件测试。识别方案和传感器接入将在后续讨论，目前尚未实现射频扫描或 ADC 检测。

## 硬件

- ESP32-C5，已连接芯片 revision v1.2，16 MB Flash。
- GPIO27：板载 WS2812B RGB LED，使用 RMT 驱动。
- GPIO28：BOOT 按键，按下为低电平。
- USB Serial/JTAG：烧录和日志；PSRAM 容量未核实，暂不启用。

![ESP32-C5 Pinout](docs/esp32-c5-pinout.png)

## LED 测试

- 上电后以白色呼吸，约 2 秒一个周期。
- BOOT 按一次随机切换颜色，呼吸效果继续。
- 按键消抖 50 ms，长按只切换一次。重启恢复白色。
- 亮度上限为 48/255，不启用 Wi-Fi、BLE 或 PSRAM。

已完成编译、烧录和启动日志检查，呼吸灯及 BOOT 换色效果已实机确认。

## 构建与烧录

PlatformIO + ESP-IDF 5.5.5，默认环境 `esp32-c5`。通过固定的 pioarduino release 安装标准工具链，环境详情见 [docs/c5-environment.md](docs/c5-environment.md)。

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run
pio run -t upload --upload-port /dev/cu.usbmodem101
pio device monitor --port /dev/cu.usbmodem101
```

串口以 `pio device list` 为准。不要在复位或上电时按住 BOOT，否则设备会进入下载模式。
