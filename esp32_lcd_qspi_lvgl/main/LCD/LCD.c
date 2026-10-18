#include "LCD.h"
#include "ASCII.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"

static const char *TAG = "LCD";
#include "GB1616.h"	//16*16������ģ



void LCD_delay(unsigned short time)  //��ʱ����
{
	vTaskDelay(pdMS_TO_TICKS(time));
}

void LCD_WriteByteSPI(unsigned char byte) //SPIģ�⺯����дһ��8bit������
{
	  unsigned char buf;
    unsigned char i;
    for(i=0;i<8;i++) 
    {
			
        buf=(byte>>(7-i))&0x1;
        SPI_SDA(buf);
				SPI_DCLK(0);
        SPI_DCLK(1);
    }	
}
static void LCD_WriteNibble(unsigned char nib)
{
	gpio_set_level(LCD_PIN_D0, (nib >> 0) & 0x1);
	gpio_set_level(LCD_PIN_D1, (nib >> 1) & 0x1);
	gpio_set_level(LCD_PIN_D2, (nib >> 2) & 0x1);
	gpio_set_level(LCD_PIN_D3, (nib >> 3) & 0x1);
	SPI_DCLK(0);
	SPI_DCLK(1);
}

void LCD_GPIO_Init(void)
{
	uint64_t mask = (1ULL << LCD_PIN_CS) | (1ULL << LCD_PIN_SCK) | (1ULL << LCD_PIN_D0) |
	               (1ULL << LCD_PIN_D1) | (1ULL << LCD_PIN_D2) | (1ULL << LCD_PIN_D3);
	gpio_config_t cfg = {
		.pin_bit_mask = mask,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&cfg);
	SPI_CS(1);
	SPI_DCLK(0);
}

esp_err_t lcd_tk499_init_cmds(esp_lcd_panel_io_handle_t io)
{
	typedef struct {
		int cmd;
		const uint8_t data[2];
		size_t data_bytes;
		unsigned int delay_ms;
	} lcd_cmd_t;
	static const lcd_cmd_t cmds[] = {
		{0xFE, {0x00},    1,   0},   /* 页选择寄存器组0 */
		{0x11, {0x00},    0, 100},   /* Sleep Out */
		{0xFE, {0x00},    1,   0},
		{0xC4, {0x80},    1,   0},
		{0x3A, {0x55},    1,   0},   /* RGB565 */
		{0x53, {0x20},    1,   0},   /* 显示控制 */
		{0x63, {0xFF},    1,   0},
		{0x51, {0xa0},    1,   0},   /* 亮度 */
		{0x58, {0x07},    1,   0},
		{0x29, {0x00},    0,  10},   /* Display On */
	};
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmds[i].cmd,
			cmds[i].data_bytes ? cmds[i].data : NULL, cmds[i].data_bytes), TAG, "tx_param failed");
		if (cmds[i].delay_ms) {
			vTaskDelay(pdMS_TO_TICKS(cmds[i].delay_ms));
		}
	}
	return ESP_OK;
}

void WriteColor(unsigned int color)
{
	LCD_WriteNibble((color >> 12) & 0x0F);
	LCD_WriteNibble((color >> 8) & 0x0F);
	LCD_WriteNibble((color >> 4) & 0x0F);
	LCD_WriteNibble(color & 0x0F);
}
void Write_1bye(unsigned int color)
{
	LCD_WriteNibble((color >> 4) & 0x0F);
	LCD_WriteNibble(color & 0x0F);
}
void WriteComm(unsigned char Index) //д����
{

	
SPI_CS(1);

SPI_CS(0);
	
	LCD_WriteByteSPI(0X02);
	
	
	LCD_WriteByteSPI(0X00);
	LCD_WriteByteSPI(Index);
	LCD_WriteByteSPI(0X00);

}
void WriteComm4bit(unsigned char Index) //д����
{
SPI_CS(1);

SPI_CS(0);
	
	LCD_WriteByteSPI(0X32);//    02/32/12
	
	
	LCD_WriteByteSPI(0X00);
	LCD_WriteByteSPI(Index);
	LCD_WriteByteSPI(0X00);

}
void WriteData(unsigned short dat) //д����
{

	
  LCD_WriteByteSPI(dat);

}

void LCD_Initial(void) //LCD��ʼ������
{
	lcd_RESET(0);
	LCD_delay(200);//�ȴ�LCDӲ����λ���
	lcd_RESET(1);
	LCD_delay(200);//�ȴ�LCDӲ����λ���

	WriteComm(0xFE);
	WriteData(0x00);

	WriteComm(0x11);
	SPI_CS(1);
	LCD_delay(100);
	WriteComm(0xFE);
	WriteData(0x00);
	
	WriteComm(0xC4);
	WriteData(0x80);

	WriteComm(0x3A);
	WriteData(0x55);

	WriteComm(0x53);
	WriteData(0x20);

	WriteComm(0x63);
	WriteData(0xFF);
	

	WriteComm(0x51);
	WriteData(0xa0);
	
	WriteComm(0x58);
	WriteData(0x07);

WriteComm(0x29);
SPI_CS(1);
LCD_delay(10);

}

/**********************************************
����������������

��ڲ�����XStart x��������
          Xend   x������յ�
					YStart y��������
          Yend   y������յ�

��������������ǣ���һ�����ο򣬷����������������������
***********************************************/
void BlockWrite(unsigned int Xstart,unsigned int Xend,unsigned int Ystart,unsigned int Yend) 
{
	 WriteComm(0x2A);
   WriteData(Xstart>>8);
   WriteData(Xstart);
   WriteData(Xend>>8);
   WriteData(Xend);
   SPI_CS(1);
   WriteComm(0x2B);
   WriteData(Ystart>>8);
   WriteData(Ystart);
   WriteData(Yend>>8);
	 WriteData(Yend);
	SPI_CS(1);
	
	WriteComm4bit(0x2c);
	

}
/**********************************************
��������Lcd������亯��

��ڲ�����xStart x�������ʼ��
          ySrart y�������ֹ��
          xLong Ҫѡ�����ε�x���򳤶�
          yLong  Ҫѡ�����ε�y���򳤶�
����ֵ����
***********************************************/
void Lcd_ColorBox(u16 xStart,u16 yStart,u16 xLong,u16 yLong,u32 Color)
{
	u32 i,j;
	BlockWrite(xStart,xStart+xLong-1,yStart,yStart+yLong-1);

	j = xLong*yLong;
	for (i=0; i<j; i++)
	{
		WriteColor(Color);	  //��ʾ��������ɫ. 
	}
	SPI_CS(1);
}

/******************************************
��������Lcdͼ�����
���ܣ���Lcdָ��λ�����ͼ��
��ڲ�����
					(x,y): ͼƬ���Ͻ���ʼ����
					(pic_H,pic_V): ͼƬ�Ŀ���
					 pic  ָ��洢ͼƬ�����ָ��
******************************************/
void LCD_Fill_Pic(u16 x, u16 y,u16 pic_H, u16 pic_V, const unsigned char * pic)
{	
	//========= ��ѯ��ʽ=========//
  unsigned long i;
	BlockWrite(x,x+pic_H-1,y,y+pic_V-1);
	SPI_CS(0);
	for (i = 0; i < pic_H*pic_V*2; i++)
	{
		LCD_WriteNibble(pic[i] >> 4);
		LCD_WriteNibble(pic[i] & 0x0F);
	SPI_CS(1);	
}
}
//=============== ��x��y �����ϴ�һ����ɫΪColor�ĵ� ===============
void DrawPixel(u16 x, u16 y, int Color)
{
	BlockWrite(x,x,y,y); 
  WriteColor(Color);	
	SPI_CS(1);						  
}
void LCD_draw_VLine(u16 x,u16 y0,u16 y1,u16 Color)
{
	u16 i;
	BlockWrite(x,x,y0,y1);
	for(i=0;i<y1-y0+1;i++)
		{
			WriteColor(Color); 
		}
	SPI_CS(1);
}
void LCD_draw_HLine(u16 x0,u16 x1,u16 y,u16 Color)
{
	u16 i;
	BlockWrite(x0,x1,y,y);
	for(i=0;i<x1-x0+1;i++)
		{
			WriteColor(Color);  
		}
	SPI_CS(1);
}
//��ص�����x,y�ǵ����ʾλ�ã�power_per�ǵ����ʾ�İٷֱȣ���ΧΪ0~4��
void draw_Bat(u16 x,u16 y,u8 power_per)
{
	u8 i,j;
	LCD_draw_VLine(x,y,y+5,BLACK);
	LCD_draw_VLine(x,y+5+10,y+5+10+5,BLACK);
	
	LCD_draw_HLine(x,x+28,y,BLACK);
	LCD_draw_HLine(x,x+28,y+20,BLACK);
	
	LCD_draw_VLine(x+28,y,y+5,BLACK);
	LCD_draw_VLine(x+28,y+5+10,y+5+10+5,BLACK);
	
	LCD_draw_HLine(x+28,x+28+3,y+5,BLACK);
	LCD_draw_HLine(x+28,x+28+3,y+5+10,BLACK);
	
	LCD_draw_VLine(x+28+3,y+5,y+5+10,BLACK);
	
	for(j=0;j<power_per;j++)
	{
		BlockWrite(x+3,x+3+4,y+3,y+3+14);
		for(i=0;i<5*15;i++)
			{ 
				WriteColor(BLACK); 				
			}
		SPI_CS(1);
		x=x+6;
	}
}
/**********8*16���� ASCII�� ��ʾ*************
(x,y): ��ʾ��ĸ����ʼ����
num:   Ҫ��ʾ���ַ�:" "--->"~"
fColor ǰ��ɫ
bColor ����ɫ
flag:  �б���ɫ(1)�ޱ���ɫ(0)
*********************************************/
void SPILCD_ShowChar(unsigned short x,unsigned short y,unsigned char num, unsigned int fColor, unsigned int bColor,unsigned char flag) 
{       
	unsigned char temp;
	unsigned int pos,i,j;  

	num=num-' ';//�õ�ƫ�ƺ��ֵ
	i=num*16; 	
	for(pos=0;pos<16;pos++)
		{
			temp=nAsciiDot[i+pos];	//��ͨ����ASCII����
			for(j=0;j<8;j++)
		   {                 
		        if(temp&0x80)
							DrawPixel(x+j,y,fColor);
						else {
							if(flag) DrawPixel(x+j,y,bColor); //�������ɫ��־flagΪ1
							temp<<=1; 
						}
							
		    }
			 y++;
		}		 
}  
/**********дһ��16x16�ĺ���*****************
(x,y): ��ʾ���ֵ���ʼ����
c[2]:  Ҫ��ʾ�ĺ���
fColor ǰ��ɫ
bColor ����ɫ
flag:  �б���ɫ(1)�ޱ���ɫ(0)
*********************************************/
void PutGB1616(unsigned short x, unsigned short  y, unsigned char c[2], unsigned int fColor,unsigned int bColor,unsigned char flag)
{
	unsigned int i,j,k;
	unsigned short m;
	for (k=0;k<64;k++) { //64��ʾ�Խ����ֿ��еĸ�����ѭ����ѯ����
	  if ((codeGB_16[k].Index[0]==c[0])&&(codeGB_16[k].Index[1]==c[1]))
			{ 
    	for(i=0;i<32;i++) 
			{
				m=codeGB_16[k].Msk[i];
				for(j=0;j<8;j++) 
				{		
					if((m&0x80)==0x80) {
						DrawPixel(x+j,y,fColor);
						}
					else {
						if(flag) DrawPixel(x+j,y,bColor);
						}
					m=m<<1;
				} 
				if(i%2){y++;x=x-8;}
				else x=x+8;
		  }
		}  
	  }	
	}

/**********��ʾһ����*****************
(x,y): �ַ�������ʼ����
*s:    Ҫ��ʾ���ַ���ָ��
fColor ǰ��ɫ
bColor ����ɫ
flag:  �б���ɫ(1)�ޱ���ɫ(0)
*********************************************/
void LCD_PutString(unsigned short x, unsigned short y, char *s, unsigned int fColor, unsigned int bColor,unsigned char flag) 
	{
		unsigned char l=0;
		while(*s) 
			{
				if( (unsigned char)*s < 0x80) 
						{
							SPILCD_ShowChar(x+l*8,y,*s,fColor,bColor,flag);
							s++;l++;
						}
				else
						{
							PutGB1616(x+l*8,y,(unsigned char*)s,fColor,bColor,flag);
							s+=2;l+=2;
						}
			}
	}