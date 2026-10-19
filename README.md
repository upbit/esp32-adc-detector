# esp32-adc-detector

当前默认工程是 **ESP32-S3 N8R8 + TK0196M106 AMOLED 雷达刷新测试**，支持 BOOT 和触摸滑动切换五个测试档位。旧扫描/RLCD 源码保留，但不参与构建。

当前使用 40 MHz QSPI 驱动 TK0196M106 AMOLED（CO5300），显示已通过用户目视验证。用户已确认 CHSC6417 触摸和滑动正常；最新 `ERR` 提示保持 500 ms 的版本已编译，尚未烧录验证。

## 硬件与接线

已通过 esptool 识别 ESP32-S3 rev v0.2、8 MB Quad Flash、8 MB Embedded PSRAM、40 MHz 晶振。屏幕为 368×448、CO5300，使用 **QSPI、RGB565、DMA**（40 MHz 已验证，SPI mode 3，卖家初始化序列；线束必须按下方「接线注意事项」绑紧）。当前由 `display.c` 直接提交 SPI 事务；LVGL 文字使用双缓冲，雷达使用四个条带缓冲。

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
| SCL | GPIO8 | CHSC6417 触摸 I²C 时钟 |
| SDA | GPIO7 | CHSC6417 触摸 I²C 数据 |

正面朝向自己、USB 朝下时，左侧相关排针从上到下为：

```text
GPIO7   ← SDA（触摸）
GPIO8   ← SCL（触摸）
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

改接线前先断电。接线改动后，先看启动检测画面里的渐变是否平滑，再看整圈档位。


### 触摸接线与操作

屏幕使用 CHSC6417 电容触摸。完成显示验证后，断电，将屏幕四线插座的 **SCL 接 GPIO8、SDA 接 GPIO7**；3V3/GND 沿用现有连接。正面朝自己、USB 朝下时，GPIO7/8 就在左侧 GPIO9 的上方，SDA 在上、SCL 在下。

固件通过 100 kHz I²C 读取 CHSC6417（地址 `0x2E`），约每 10 ms 采样一次。RST/TE/EN 测试点仍不接。启动日志应出现 `CHSC6417 at 0x2E`；若出现 `Touch disabled`，显示和 BOOT 仍可使用，先核对触摸接线和供电。

实测控制器可能首次探测不应答、随后同一地址应答，因此启动时对地址未应答最多探测 3 次，间隔 20 ms，避免一次瞬时 NACK 就永久禁用触摸；持续失败时打印总线扫描和 SCL/SDA 电平诊断。

左下角与右下角 FPS 对称的位置显示触摸状态：绿色 `ON` 表示读到触点，`UP` 表示松开，红色 `ERR` 表示 I²C 读取失败，`OFF` 表示触摸初始化失败／未启用。按住时先检查是否变为 `ON`；此提示不依赖滑动识别，坐标异常或手势被取消时仍可显示按下。

为避免错误提示一闪而过，最近一次错误后的 500 ms 内，松开状态继续显示红色 `ERR`；连续错误延长保持时间。有效按下仍立即显示绿色 `ON`。这只是提示保持，读取错误仍记录在串口日志中。

**下滑后松手切换下一页，上滑后松手切换上一页**。五页为 10% → 25% → 35% → 50% → 整圈，首尾循环；BOOT 短按仍切换下一页。每次滑动最多翻一页，纵向净位移至少 48 像素且至少为横向的 1.5 倍；点击、短滑和横滑不翻页。读取失败会取消本次手势，松手后才接受新手势。

## 测试画面

启动时先显示一屏检测画面，持续 3 秒：上 2/5 为 **红 / 绿 / 蓝 / 白 / 黑** 竖条，下 3/5 为红、绿、蓝、灰四条从黑到亮的横向渐变。渐变平滑、没有噪点或断层，就说明 QSPI 时序正常。然后进入下面的 FPS 页面。

检测画面后进入雷达页，指针每 2 秒转一圈；各页通过改变余晖面积测试不同刷新负载，整圈页每帧写入整个雷达圆。页面由触摸或 BOOT 切换，不自动翻页。顶部显示当前档位、理论总线上限和统计，右下角显示实测 FPS。

屏幕与串口每秒给出相同指标，切换模式后的第一个采样窗口显示 `Settling...`，不用于比较：

| 指标 | 含义 |
|---|---|
| FPS | 完成最后一个 DMA 条带的雷达帧数/秒 |
| MB/s / payload | 已完成 DMA 的像素字节/秒，十进制 MB，不含命令开销 |
| bus | 像素吞吐量相对 40 MHz 四线理想负载 20 MB/s 的占比 |
| Frame avg / max | 一帧首条提交到末条 DMA 完成的平均/最大时间，不含首条提交前的绘制 |
| draw | core 1 平均每帧绘制耗时 |

40 MHz QSPI 的理想负载上限为 20 MB/s，折合约 60.7 全屏 FPS；雷达局部刷新可获得更高的写入 FPS，实际受命令、绘制和分块开销影响。**未连接 TE，不做垂直同步；这些统计不能证明面板扫描频率或无撕裂。** 触摸轮询与 DMA 提交共用主任务，其性能影响需要实机对比。

## 构建与运行

本机已使用 PlatformIO Espressif32 55.3.312、ESP-IDF 5.5.5、LVGL 9.4.0。8 MB Flash、Octal PSRAM/80 MHz、USB Serial/JTAG 控制台、1 kHz RTOS tick 均已配置。

```sh
pio run -e esp32-s3-n8r8
pio run -e esp32-s3-n8r8 -t upload --upload-port /dev/cu.usbmodem1401
pio device monitor -p /dev/cu.usbmodem1401 -b 115200
```

本机若 `pio` 不在 PATH，将其替换为 `/Users/gao/.platformio/penv/bin/pio`。串口路径会随 USB 连接变化，烧录前重新确认。固件输出位于 `.pio/build/esp32-s3-n8r8/firmware.bin`。

启动日志应报告 PSRAM 为 8 MB 并通过开机内存测试；随后出现 `QSPI 40 MHz direct SPI`、触摸初始化结果及每秒 `benchmark` 指标。触摸验证时，分别上下滑动并松手，检查每次只切一页、首尾循环、点击和横滑不切页、BOOT 仍可切档；串口应出现 `Swipe down/up` 及对应的 `tier`。同时记录各档位稳定 FPS，比较触摸轮询前后的性能。

详细架构、资料依据和验证状态见 [bringup-plan.md](docs/AMOLED/bringup-plan.md)，其中触摸翻页设计已由用户确认。本次未接入无线扫描或产品 UI。
