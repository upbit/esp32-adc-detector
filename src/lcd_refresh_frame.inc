/* Included twice in main.c; both copies have identical timing and ownership. */
static REFRESH_FRAME_ATTR esp_err_t REFRESH_FRAME_FN(const refresh_strategy_t *strategy,uint16_t *pixels,
                              int64_t elapsed,unsigned frame,int64_t *draw,int64_t *spi,
                              chart_lcd_timing_t *timing)
{
    const int end=CHART_LCD_REFRESH_Y+CHART_LCD_REFRESH_HEIGHT;
    const int chunk=strategy->window ? CHART_TILE_ROWS : CHART_LCD_REFRESH_ROWS;
    bool opened=false;
    esp_err_t err=ESP_OK;
    for (int y=CHART_LCD_REFRESH_Y;y<end && err==ESP_OK;y+=chunk) {
        unsigned part=(y-CHART_LCD_REFRESH_Y)/chunk;
        uint16_t *buffer=pixels+(strategy->pipeline ? (part%2)*CHART_LCD_BUFFER_PIXELS : 0);
        int64_t started=esp_timer_get_time();
        if (strategy->window && part>=(strategy->pipeline ? 2U : 1U)) {
            uint16_t *completed=NULL;
            err=REFRESH_WINDOW_FN(wait)(&completed);
            if (err==ESP_OK && completed!=buffer) err=ESP_ERR_INVALID_STATE;
        }
        *spi+=esp_timer_get_time()-started;
        if (err!=ESP_OK) break;
        started=esp_timer_get_time();
        if (strategy->window) chart_render_refresh_band(y,chunk,buffer,elapsed,frame);
        else chart_render_refresh_band_wire(y,chunk,buffer,elapsed,frame);
        *draw+=esp_timer_get_time()-started;
        started=esp_timer_get_time();
        if (strategy->window) {
            if (!opened) {
                err=REFRESH_WINDOW_FN(begin)(0,CHART_LCD_REFRESH_Y,320,CHART_LCD_REFRESH_HEIGHT);
                opened=err==ESP_OK;
            }
            if (err==ESP_OK) err=REFRESH_WINDOW_FN(submit)(buffer,chunk,y+chunk==end);
        } else {
            chart_lcd_timing_t t={0};
            err=chart_lcd_tile_profiled(y,chunk,buffer,CHART_LCD_WRITE_WIRE,&t);
            refresh_add_timing(timing,&t);
        }
        *spi+=esp_timer_get_time()-started;
    }
    if (opened) {
        int64_t started=esp_timer_get_time();
        chart_lcd_timing_t t={0};
        esp_err_t ended=REFRESH_WINDOW_FN(end)(&t);
        if (err==ESP_OK) err=ended;
        refresh_add_timing(timing,&t);
        *spi+=esp_timer_get_time()-started;
    }
    return err;
}
