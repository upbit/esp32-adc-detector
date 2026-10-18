# esp32-adc-detector

当前默认工程是 **ESP32-S3 N8R8 + TK0196M106 AMOLED 刷新测试**。新 UI 开发前，先用此工程验证屏幕、颜色、QSPI 传输与实际完成 FPS。旧扫描/RLCD 源码保留，但不参与构建。

当前使用 QSPI 驱动 TK0196M106 AMOLED（CO5300），供电 3.333V，RST、EN 均约 3.33V，线长约 8cm。固件和 PSRAM 运行正常，DMA 有完成统计，但屏幕仅出现“黑屏→白块伴黑线→黑屏”，无法显示彩色或 UI。

## 硬件与接线

已通过 esptool 识别 ESP32-S3 rev v0.2、8 MB Quad Flash、8 MB Embedded PSRAM、40 MHz 晶振。屏幕为 368×448、CO5300，使用 **QSPI、RGB565、DMA 双缓冲**（当前黑屏诊断暂降为 10 MHz，目标配置为 40 MHz）。采用 [Espressif CO5300 2.2.0 驱动](https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.2.0/readme)。

开发板排针依据 [ESP32-S3_R8N8_pinlayout.png](docs/ESP32-S3_R8N8_pinlayout.png) 核对。**以下为新接线，替换上一版 GPIO6/7/15/16/17/18 分配。** QSPI 使用 SPI2 原生 IO_MUX 引脚 GPIO9–14，六根信号线集中在左侧连续排针。

| AMOLED 丝印 | ESP32-S3 连接 | 说明 |
|---|---|---|
| D3 | GPIO9 | QSPI DATA3 |
| D2 | GPIO14 | QSPI DATA2 |
| D1 | GPIO13 | QSPI DATA1 |
| D0 | GPIO11 | QSPI DATA0 |
| SCK | GPIO12 | QSPI 时钟 |
| CS | GPIO10 | 片选 |
| GND | GND | 共地 |
| 3V3 | 3V3 | 成品屏电源 |
| SCL | GPIO8 | 下一步触摸接线；当前固件未启用 |
| SDA | GPIO7 | 下一步触摸接线；当前固件未启用 |

正面朝向自己、USB 朝下时，左侧相关排针从上到下为：

```text
GPIO7   ← SDA（下一步接入触摸）
GPIO8   ← SCL（下一步接入触摸）
GPIO9   ← D3
GPIO10  ← CS
GPIO11  ← D0
GPIO12  ← SCK
GPIO13  ← D1
GPIO14  ← D2
RST     ← 不接
3V3     ← 屏幕 3V3（三个 3V3 针脚任选一个）
3V3
3V3
GND     ← 屏幕 GND
```

QSPI 信号线尽量短，建议先控制在约 10 cm 以内；连接端只按丝印辨认，勿凭线材颜色判断。背面插座方向见 [AMOLED-pinout.jpg](docs/AMOLED/AMOLED-pinout.jpg)。RST/TE/EN 测试点暂不接，使用板上上电复位和 CO5300 软件复位。引脚和屏幕参数统一定义于 [src/board.h](src/board.h)。断电调整接线。用户已确认显示及 GPIO7/8 触摸接线，测试固件已于 2026-10-18 烧录；用户反馈初版雪花屏、复位后黑屏；10 MHz 纯色诊断也未通过（白色块/黑线后黑屏），当前处于硬件和初始化排查阶段。

### 下一步：触摸接线

屏幕使用 CHSC6417 电容触摸。完成显示验证后，断电，将屏幕四线插座的 **SCL 接 GPIO8、SDA 接 GPIO7**；3V3/GND 沿用现有连接。正面朝自己、USB 朝下时，GPIO7/8 就在左侧 GPIO9 的上方，SDA 在上、SCL 在下。

当前固件只运行显示/FPS 测试，接好这两根线后仍需后续开发触摸驱动和 LVGL 输入接入，才会响应触摸。RST/TE/EN 测试点此阶段仍暂不接。

## 测试画面

当前诊断版本启动时先依次显示 **红 → 绿 → 蓝 → 白 → 黑**，每色 2 秒，再进入下面的 FPS 页面。QSPI 暂用 10 MHz；原有 40 MHz 测量只保留作历史记录，未通过可见显示验证。

画面每 **15 秒**切换一次：

- **PARTIAL**：只更新运动色块、竖条及每秒更新的文字，观察实际 UI 流畅度。
- **FULL**：同样的运动画面，每次刷新主动让整屏失效，测试全屏持续写入。

固定 RGB/黄/青/品红色条、16 级灰阶和边框用于检查色序、字节序、裁剪；运动竖条用于观察撕裂。无需触摸或按键。初始亮度约 40%。

屏幕与串口每秒给出相同指标，切换模式后的第一个采样窗口显示 `Settling...`，不用于比较：

| 指标 | 含义 |
|---|---|
| FPS / completed | 完成最后一个 DMA 分块的 LVGL 刷新帧数/秒；不是动画定时器次数 |
| MB/s / payload | 已完成 DMA 的像素字节/秒，十进制 MB，不含命令开销 |
| full eq / full_equiv | 像素吞吐量 ÷ 329728，折算为 368×448 RGB565 全屏帧数/秒 |
| Frame avg / max | 一次刷新首块提交到末块 DMA 完成的平均/最大时间，包含中间绘制与等待，不含首块提交前的绘制 |

动画与刷新目标周期均为 16 ms。40 MHz QSPI 的理想负载上限为 20 MB/s，折合约 60.7 全屏 FPS；实际受命令、绘制和分块开销影响。**PARTIAL 的 FPS 和 FULL 的 FPS 应结合 full eq 比较。未连接 TE，不做垂直同步；这些统计不能证明面板扫描频率或无撕裂。** 即使屏幕未连接，SPI 写入也可能成功；只有看到真实画面才能确认点屏。

## 构建与运行

本机已使用 PlatformIO Espressif32 55.3.312、ESP-IDF 5.5.5、LVGL 9.4.0。8 MB Flash、Octal PSRAM/80 MHz、USB Serial/JTAG 控制台、1 kHz RTOS tick 均已配置。

```sh
pio run -e esp32-s3-n8r8
pio run -e esp32-s3-n8r8 -t upload --upload-port /dev/cu.usbmodem1401
pio device monitor -p /dev/cu.usbmodem1401 -b 115200
```

本机若 `pio` 不在 PATH，将其替换为 `/Users/gao/.platformio/penv/bin/pio`。串口路径会随 USB 连接变化，烧录前重新确认。固件输出位于 `.pio/build/esp32-s3-n8r8/firmware.bin`。

启动日志应报告 PSRAM 为 8 MB 并通过 ESP-IDF 开机内存测试；随后出现 `QSPI 10 MHz, 368x448 RGB565`（当前诊断配置） 及每秒 `benchmark` 指标。观察至少一轮 PARTIAL → FULL → PARTIAL，核对六种颜色、灰阶、四周边界以及移动竖条；记录 FULL 的稳定 FPS 和最大帧耗时。

详细架构、资料依据和验证状态见 [bringup-plan.md](docs/AMOLED/bringup-plan.md)。本次未接入触摸、无线扫描或产品 UI。
