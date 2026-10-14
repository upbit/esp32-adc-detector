#define CHART_LCD_SWAP_HOST_TEST
#include "../src/chart_hardware.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t baseline[20480+2],optimized[20480+2],original[20480+2];

static void check(size_t count,unsigned seed)
{
    for (size_t i=0;i<20480+2;++i) original[i]=(uint16_t)(seed+i-1);
    memcpy(baseline,original,sizeof(original));
    memcpy(optimized,original,sizeof(original));
    lcd_swap_baseline(baseline+1,count);
    lcd_swap_optimized(optimized+1,count);
    assert(memcmp(baseline,optimized,sizeof(baseline))==0);
    assert(baseline[0]==original[0]);
    for (size_t i=1;i<=count;++i)
        assert(baseline[i]==(uint16_t)((original[i]&255)<<8 | original[i]>>8));
    for (size_t i=count+1;i<20480+2;++i) assert(baseline[i]==original[i]);
    lcd_swap_optimized(optimized+1,count);
    assert(memcmp(optimized,original,sizeof(original))==0);
}

int main(void)
{
    const size_t sizes[]={0,1,2,3,255,256,257,320,320*16,320*32,320*64};
    for (size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) check(sizes[i],0xf800);
    for (unsigned seed=0;seed<65536;seed+=20480) check(20480,seed);
    puts("PASS: LCD byte swaps match for all RGB565 values, odd lengths, 16/32/64-row buffers and guard pixels");
    return 0;
}
