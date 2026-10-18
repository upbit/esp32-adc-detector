#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"

#define LCD_PIN_CS     (GPIO_NUM_16)
#define LCD_PIN_SCK    (GPIO_NUM_6)
#define LCD_PIN_SDA    (GPIO_NUM_15)
#define LCD_PIN_D0     (GPIO_NUM_15)
#define LCD_PIN_D1     (GPIO_NUM_7)
#define LCD_PIN_D2     (GPIO_NUM_17)
#define LCD_PIN_D3     (GPIO_NUM_18)
#define LCD_PIN_RST    (GPIO_NUM_NC)

#define SPI_CS(a)      gpio_set_level(LCD_PIN_CS, (a))
#define SPI_DCLK(a)    gpio_set_level(LCD_PIN_SCK, (a))
#define SPI_SDA(a)     gpio_set_level(LCD_PIN_SDA, (a))
#define lcd_RESET(a)  do { if (LCD_PIN_RST >= 0) gpio_set_level(LCD_PIN_RST, (a)); } while (0)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define WHITE						0xFFFF
#define BLACK						0x0000	  
#define BLUE						0x001F  
#define BRED						0XF81F
#define GRED						0XFFE0
#define GBLUE						0X07FF
#define RED							0xF800
#define MAGENTA						0xF81F
#define GREEN						0x07E0
#define CYAN						0x7FFF
#define YELLOW						0xFFE0
#define BROWN						0XBC40 //��ɫ
#define BRRED						0XFC07 //�غ�ɫ
#define GRAY						0X8430 //��ɫ
void LCD_delay(unsigned short time); //��ʱ����
void LCD_Initial(void); //LCD��ʼ������
void LCD_GPIO_Init(void);
esp_err_t lcd_tk499_init_cmds(esp_lcd_panel_io_handle_t io);
void LCD_WriteData(unsigned short dat);
void BlockWrite(unsigned int Xstart,unsigned int Xend,unsigned int Ystart,unsigned int Yend);
void DrawPixel(u16 x, u16 y, int Color);
void PutGB1616(unsigned short x, unsigned short y, unsigned char c[2], unsigned int fColor, unsigned int bColor, unsigned char flag);
void Lcd_ColorBox(u16 xStart,u16 yStart,u16 xLong,u16 yLong,u32 Color);
void LCD_Fill_Pic(u16 x, u16 y,u16 pic_H, u16 pic_V, const unsigned char * pic);
void SPILCD_ShowChar(unsigned short x,unsigned short y,unsigned char num, unsigned int fColor, unsigned int bColor,unsigned char flag) ;
void LCD_PutString(unsigned short x, unsigned short y, char *s, unsigned int fColor, unsigned int bColor,unsigned char flag);

#endif
