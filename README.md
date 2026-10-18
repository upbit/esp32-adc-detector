# esp32-adc-detector

当前默认工程是 **ESP32-S3 N8R8 + TK0196M106 AMOLED 刷新测试**。新 UI 开发前，先用此工程验证屏幕、颜色、QSPI 传输与实际完成 FPS。旧扫描/RLCD 源码保留，但不参与构建。

当前使用 QSPI 驱动 TK0196M106 AMOLED（CO5300），供电 3.333V，RST、EN 均约 3.33V，线长约 8cm。固件和 PSRAM 运行正常，DMA 有完成统计，但屏幕仅出现“黑屏→白块伴黑线→黑屏”，无法显示彩色或 UI。

## 硬件与接线

已通过 esptool 识别 ESP32-S3 rev v0.2、8 MB Quad Flash、8 MB Embedded PSRAM、40 MHz 晶振。屏幕为 368×448、CO5300，使用 **QSPI、RGB565、DMA 双缓冲**（40 MHz 已验证，SPI mode 3，卖家初始化序列；线束必须按下方「接线注意事项」绑紧）。采用 [Espressif CO5300 2.2.0 驱动](https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.2.0/readme)。

开发板排针依据 [ESP32-S3_R8N8_pinlayout.png](docs/ESP32-S3_R8N8_pinlayout.png) 核对。**以下为新接线，替换上一版 GPIO6/7/15/16/17/18 分配。** QSPI 使用 SPI2 原生 IO_MUX 引脚 GPIO9–14，六根信号线集中在左侧连续排针。

| AMOLED 丝印 | ESP32-S3 连接 | 说明 |
|---|---|---|
| D3 | GPIO9 | QSPI DATA3 |
| D2 | GPIO14 | QSPI DATA2 |
| D1 | GPIO13 | QSPI DATA1 |
| D0 | GPIO11 | QSPI DATA0 |
| SCK | GPIO12 | QSPI 时钟 |
| CS | GPIO10 | 片选 |
| GND | 左侧 GPIO6 与 GPIO7 之间的 GND | 共地；**必须用靠近 QSPI 信号的这个 GND**，见下方说明 |
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
GND     ← 不要用：靠近 USB，回流路径远离 QSPI 线束
```

屏幕 GND 接**左侧 GPIO6 与 GPIO7 之间的 GND**（远离 USB、紧挨 QSPI 信号线）。

QSPI 信号线尽量短，建议先控制在约 10 cm 以内；连接端只按丝印辨认，勿凭线材颜色判断。背面插座方向见 [AMOLED-pinout.jpg](docs/AMOLED/AMOLED-pinout.jpg)。RST/TE/EN 测试点暂不接，使用板上上电复位和 CO5300 软件复位。引脚和屏幕参数统一定义于 [src/board.h](src/board.h)。断电调整接线。**2026-10-18 已点亮。** 之前出现白块/黑线后黑屏，根因是地回路：屏幕 GND 原先接在 USB 旁边的底部 GND，四根数据线同时翻转时产生地弹，导致 QSPI 像素写入出错，而单根线翻转、单线 SPI 都正常。屏幕 GND 改接到 GPIO7 旁边的 GND 后恢复正常。SCK/D0–D3 驱动能力为 0（`LCD_DRIVE_CAP`）。排查过程见 [bringup-plan.md](docs/AMOLED/bringup-plan.md)。

### 接线注意事项（QSPI 信号完整性）

屏幕的 6 针 QSPI 插座（D3/D2/D1/D0/SCK/CS）**没有 GND 针**，数据线的回流只能走 4 针插座里的那一根 GND。MX1.25 转杜邦线约 8 cm 且不能缩短，所以：

1. **GND 选远离 USB、紧挨 GPIO7–14 的那个**（左侧 GPIO6 与 GPIO7 之间）。不要用 USB 旁边的底部 GND，最初的黑屏就是这个原因。
2. **两束线全程并拢绑紧**：6 针 QSPI 线和 4 针电源/触摸线（GND/3V3/SCL/SDA）每隔 1–2 cm 用胶带或扎带固定，两端分叉处尽量贴近插座，不要张开成大的 Y 形。
3. **3V3 和 GND 成对贴紧**，一起靠着 QSPI 线束。
4. **加一根屏蔽地线**：另接一根杜邦线，一端插 ESP32 远离 USB 的 GND，另一端悬空，绑在 QSPI 线束中间。它只起静电屏蔽、减少串扰的作用，不承载回流电流。如果能把另一端也接到屏幕侧的 GND，效果更好。
5. 不要把多余线长盘成圈；线束远离 USB 线和板载天线；不需要把线拧成双绞。
6. 后续可选：在 ESP32 端给 SCK/D0–D3 各串一个 22–33 Ω 电阻（可用面包板转接），进一步抑制地弹和振铃。

改接线前先断电。接线改动后，先看启动检测画面里的渐变是否平滑，再看 FULL 模式。


### 下一步：触摸接线

屏幕使用 CHSC6417 电容触摸。完成显示验证后，断电，将屏幕四线插座的 **SCL 接 GPIO8、SDA 接 GPIO7**；3V3/GND 沿用现有连接。正面朝自己、USB 朝下时，GPIO7/8 就在左侧 GPIO9 的上方，SDA 在上、SCL 在下。

当前固件只运行显示/FPS 测试，接好这两根线后仍需后续开发触摸驱动和 LVGL 输入接入，才会响应触摸。RST/TE/EN 测试点此阶段仍暂不接。

## 测试画面

启动时先显示一屏检测画面，持续 3 秒：上 2/5 为 **红 / 绿 / 蓝 / 白 / 黑** 竖条，下 3/5 为红、绿、蓝、灰四条从黑到亮的横向渐变。渐变平滑、没有噪点或断层，就说明 QSPI 时序正常。然后进入下面的 FPS 页面。

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

启动日志应报告 PSRAM 为 8 MB 并通过 ESP-IDF 开机内存测试；随后出现 `QSPI 40 MHz, drive cap 0, 368x448 RGB565` 及每秒 `benchmark` 指标。观察至少一轮 PARTIAL → FULL → PARTIAL，核对六种颜色、灰阶、四周边界以及移动竖条；记录 FULL 的稳定 FPS 和最大帧耗时。

详细架构、资料依据和验证状态见 [bringup-plan.md](docs/AMOLED/bringup-plan.md)。本次未接入触摸、无线扫描或产品 UI。
