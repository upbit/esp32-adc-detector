/* Included twice in chart_hardware.c so only compile level differs. */
LCD_WINDOW_ATTR esp_err_t LCD_WINDOW_FN(begin)(int y,int rows)
{
    if (lcd_window.open || lcd_window.failed) return ESP_ERR_INVALID_STATE;
    if (y<0 || rows<=0 || y+rows>320) return ESP_ERR_INVALID_ARG;
    TRY(spi_device_acquire_bus(lcd,portMAX_DELAY));
    memset(&lcd_window,0,sizeof(lcd_window));
    lcd_window.open=true;
    lcd_window.remaining=(size_t)320*rows*2;
    int x1=CHART_LCD_X_OFFSET,x2=x1+319,y1=CHART_LCD_Y_OFFSET+y,y2=y1+rows-1;
    uint8_t xs[]={x1>>8,x1&255,x2>>8,x2&255},ys[]={y1>>8,y1&255,y2>>8,y2&255};
    int64_t started=esp_timer_get_time();
    esp_err_t err=lcd_command(0x2a,xs,sizeof(xs));
    if (err==ESP_OK) err=lcd_command(0x2b,ys,sizeof(ys));
    if (err==ESP_OK) err=lcd_command(0x2c,NULL,0);
    if (err==ESP_OK) err=spi_device_get_actual_freq(lcd,&lcd_window.timing.command_khz);
    lcd_window.timing.command_us=esp_timer_get_time()-started;
    gpio_set_level(CHART_LCD_DC,1);
    if (err!=ESP_OK) {
        lcd_window.failed=true;
        lcd_window.open=false;
        spi_device_release_bus(lcd);
    }
    return err;
}

LCD_WINDOW_ATTR esp_err_t LCD_WINDOW_FN(write)(uint16_t *pixels,int rows)
{
    if (!lcd_window.open || lcd_window.failed || lcd_window.last || lcd_window.asynchronous)
        return ESP_ERR_INVALID_STATE;
    size_t length=rows>0 ? (size_t)320*rows*2 : 0;
    if (!pixels || !length || length>lcd_window.remaining) return ESP_ERR_INVALID_ARG;
    const uint8_t *bytes=(const uint8_t *)pixels;
    int64_t started=esp_timer_get_time();
    esp_err_t err=ESP_OK;
    while (length && err==ESP_OK) {
        size_t n=length<lcd_dma_max ? length : lcd_dma_max;
        spi_transaction_t t={.length=n*8,.tx_buffer=bytes,.override_freq_hz=lcd_pixel_hz,
            .flags=lcd_window.remaining>n ? SPI_TRANS_CS_KEEP_ACTIVE : 0};
        err=spi_device_transmit(lcd,&t);
        if (err==ESP_OK) { bytes+=n; length-=n; lcd_window.remaining-=n; }
    }
    lcd_window.timing.dma_us+=esp_timer_get_time()-started;
    lcd_window.timing.wait_us=lcd_window.timing.dma_us;
    lcd_window.last=lcd_window.remaining==0;
    lcd_window.failed=err!=ESP_OK;
    return err;
}

LCD_WINDOW_ATTR esp_err_t LCD_WINDOW_FN(submit)(uint16_t *pixels,int rows,bool last)
{
    if (!lcd_window.open || lcd_window.failed || lcd_window.last || lcd_window.pending==2)
        return ESP_ERR_INVALID_STATE;
    size_t length=rows>0 ? (size_t)320*rows*2 : 0;
    if (!pixels || !length || length>lcd_dma_max || length>lcd_window.remaining ||
        last!=(length==lcd_window.remaining)) return ESP_ERR_INVALID_ARG;
    /* Do not mix synchronous writes and an asynchronous window. */
    if (!lcd_window.asynchronous && lcd_window.timing.dma_us) return ESP_ERR_INVALID_STATE;
    for (unsigned i=0;i<lcd_window.pending;++i)
        if (lcd_window.tx[(lcd_window.next+2-lcd_window.pending+i)%2].tx_buffer==pixels)
            return ESP_ERR_INVALID_STATE;
    spi_transaction_t *t=&lcd_window.tx[lcd_window.next];
    *t=(spi_transaction_t){.length=length*8,.tx_buffer=pixels,.override_freq_hz=lcd_pixel_hz,
        .flags=SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL | (last ? 0 : SPI_TRANS_CS_KEEP_ACTIVE)};
    if (!lcd_window.asynchronous) lcd_window.first_submit=esp_timer_get_time();
    esp_err_t err=spi_device_queue_trans(lcd,t,portMAX_DELAY);
    if (err!=ESP_OK) { lcd_window.failed=true; return err; }
    lcd_window.asynchronous=true;
    lcd_window.next=(lcd_window.next+1)%2;
    ++lcd_window.pending;
    lcd_window.remaining-=length;
    lcd_window.last=last;
    return ESP_OK;
}

LCD_WINDOW_ATTR esp_err_t LCD_WINDOW_FN(wait)(uint16_t **completed_pixels)
{
    if (!lcd_window.open || !lcd_window.pending) return ESP_ERR_INVALID_STATE;
    spi_transaction_t *returned=NULL;
    spi_transaction_t *expected=&lcd_window.tx[(lcd_window.next+2-lcd_window.pending)%2];
    int64_t started=esp_timer_get_time();
    esp_err_t err=spi_device_get_trans_result(lcd,&returned,portMAX_DELAY);
    lcd_window.timing.wait_us+=esp_timer_get_time()-started;
    if (returned==expected) {
        --lcd_window.pending;
        if (completed_pixels) *completed_pixels=(uint16_t *)returned->tx_buffer;
    } else err=ESP_ERR_INVALID_STATE;
    if (err!=ESP_OK) lcd_window.failed=true;
    return err;
}

LCD_WINDOW_ATTR esp_err_t LCD_WINDOW_FN(end)(chart_lcd_timing_t *timing)
{
    if (!lcd_window.open) return ESP_ERR_INVALID_STATE;
    esp_err_t err=lcd_window.failed || !lcd_window.last ? ESP_ERR_INVALID_STATE : ESP_OK;
    while (lcd_window.pending) {
        unsigned before=lcd_window.pending;
        esp_err_t result=LCD_WINDOW_FN(wait)(NULL);
        if (result!=ESP_OK) err=result;
        if (before==lcd_window.pending) return result; /* Never release an unreturned buffer. */
    }
    if (lcd_window.asynchronous)
        lcd_window.timing.dma_us=esp_timer_get_time()-lcd_window.first_submit;
    if (err==ESP_OK) err=spi_device_get_actual_freq(lcd,&lcd_window.timing.pixel_khz);
    spi_device_release_bus(lcd);
    lcd_window.open=false;
    lcd_window.failed=err!=ESP_OK;
    if (timing) *timing=lcd_window.timing;
    return err;
}
