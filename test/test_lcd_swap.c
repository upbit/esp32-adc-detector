#define CHART_LCD_SWAP_HOST_TEST
#include "../src/chart_hardware.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t optimized[10240+2],original[10240+2];

static void check(size_t count,unsigned seed)
{
    for (size_t i=0;i<10240+2;++i) original[i]=(uint16_t)(seed+i-1);
    memcpy(optimized,original,sizeof(original));
    lcd_swap_optimized(optimized+1,count);
    assert(optimized[0]==original[0]);
    for (size_t i=1;i<=count;++i)
        assert(optimized[i]==(uint16_t)((original[i]&255)<<8 | original[i]>>8));
    for (size_t i=count+1;i<10240+2;++i) assert(optimized[i]==original[i]);
    lcd_swap_optimized(optimized+1,count);
    assert(memcmp(optimized,original,sizeof(original))==0);
}

int main(void)
{
    const size_t sizes[]={0,1,2,3,255,256,257,320,320*16,320*32};
    for (size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) check(sizes[i],0xf800);
    for (unsigned seed=0;seed<65536;seed+=10240) check(10240,seed);
    puts("PASS: production LCD swap matches all RGB565 values, odd lengths, 16/32-row regions and guard pixels");
    return 0;
}
