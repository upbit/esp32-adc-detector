#pragma once

// ESP32-S3_R8N8_pinlayout.png: SPI2 native IO_MUX pins, left header GPIO9-14.
#define LCD_WIDTH 368
#define LCD_HEIGHT 448
#define LCD_QSPI_HZ (10 * 1000 * 1000) // Diagnostic speed; restore 40 MHz after visible validation.
#define LCD_SPI_HOST SPI2_HOST
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
