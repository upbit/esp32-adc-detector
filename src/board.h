#pragma once

// ESP32-S3_R8N8_pinlayout.png: SPI2 native IO_MUX pins, left header GPIO9-14.
#define LCD_WIDTH 368
#define LCD_HEIGHT 448
#define LCD_QSPI_HZ (40 * 1000 * 1000) // 80 MHz / 2; verified with bundled cable. CO5300 write limit 50 MHz.
#define LCD_SPI_HOST SPI2_HOST
#define LCD_SINGLE_WIRE 0 // Diagnostic: SCK/CS/D0 only, like the vendor example; 0 = QSPI.
#define LCD_DRIVE_CAP GPIO_DRIVE_CAP_0 // Weakest (~5 mA) on SCK/D0-D3; ESP-IDF default is 2.
#define LCD_PIN_SCK 12
#define LCD_PIN_CS 10
#define LCD_PIN_D0 11
#define LCD_PIN_D1 13
#define LCD_PIN_D2 14
#define LCD_PIN_D3 9
#define LCD_PIN_RST (-1) // Module's RC power-on reset; use software reset.
#define TOUCH_PIN_SCL 8 // Reserved; touch is not initialized in this test.
#define TOUCH_PIN_SDA 7
#define LCD_BUFFER_LINES 48
