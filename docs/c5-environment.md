# ESP32-C5 编译环境

## 硬件

- 已连接芯片：ESP32-C5 revision v1.2，Flash 16 MB。
- GPIO27：WS2812B RGB LED，GRB 数据顺序；GPIO28：BOOT，按下接地。
- USB Serial/JTAG：当前端口 `/dev/cu.usbmodem101`。
- PSRAM 容量未核实，暂不启用。

LED 和 BOOT 接线采用 [ESP32-C5-DevKitC-1 官方定义](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html)，本地参考见 [C5 Pinout](esp32-c5-pinout.png)。

## 工具链

- PlatformIO Core 6.2.0 或更高版本。
- pioarduino release `55.03.312-1`，通过 `platformio.ini` 中的固定 GitHub release URL 安装。
- ESP-IDF 5.5.5、RISC-V GCC 14.2.0、esptool 5.4.0。
- 使用标准 `~/.platformio` 安装路径，不设置 `core_dir`。
- `espressif/led_strip` 3.x：官方可寻址 LED 驱动，使用 RMT 后端。

## 配置

- 默认且唯一的环境为 `esp32-c5`。
- Flash 16 MB，DIO / 40 MHz，`partitions16.csv` 单应用分区。
- `sdkconfig.defaults.esp32c5` 提供默认配置，`sdkconfig.esp32-c5` 为本地生成文件。
- USB Serial/JTAG 控制台，115200 波特率，监视器不拉动 DTR/RTS。
- bootloader 位于 `0x2000`，分区表位于 `0x8000`，应用位于 `0x10000`。

构建、烧录和测试操作见 [README](../README.md)。
