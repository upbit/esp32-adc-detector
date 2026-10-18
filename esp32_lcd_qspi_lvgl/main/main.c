#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_touch_chsc6417.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lcd_tk018.h"
#include "lv_demos.h"
#include "ui.h"
#include "myui.h"
#include "LCD.h"

static const char *TAG = "main";

#define TOUCH_ENABLE  1     //触摸开关
#define test_lvgl     1     //lvgl案例开关
#define TOUCH_CHIP    1     //触摸芯片选择: 1=CHSC6417, 0=FT5x06

#define TEST_LCD_HOST               SPI2_HOST
#define TEST_LCD_H_RES              (368)
#define TEST_LCD_V_RES              (448)
#define TEST_LCD_BIT_PER_PIXEL      (16)

#define TEST_PIN_NUM_LCD_CS         (GPIO_NUM_16)
#define TEST_PIN_NUM_LCD_PCLK       (GPIO_NUM_6)
#define TEST_PIN_NUM_LCD_DATA0      (GPIO_NUM_7)
#define TEST_PIN_NUM_LCD_DATA1      (GPIO_NUM_15)
#define TEST_PIN_NUM_LCD_DATA2      (GPIO_NUM_17)
#define TEST_PIN_NUM_LCD_DATA3      (GPIO_NUM_18)
#define TEST_PIN_NUM_LCD_RST        (GPIO_NUM_NC)
#define TEST_PIN_NUM_BK_LIGHT       (GPIO_NUM_NC)    // 背光引脚
#define TEST_LCD_BK_LIGHT_ON_LEVEL  (1)
#define TEST_LCD_BK_LIGHT_OFF_LEVEL !TEST_LCD_BK_LIGHT_ON_LEVEL


/* Touch settings */
#define EXAMPLE_TOUCH_I2C_NUM       (1)
#define EXAMPLE_TOUCH_I2C_CLK_HZ    (400000)

/* LCD touch pins */
#define EXAMPLE_TOUCH_I2C_SCL       (GPIO_NUM_5)
#define EXAMPLE_TOUCH_I2C_SDA       (GPIO_NUM_4)
#define EXAMPLE_TOUCH_GPIO_INT      (GPIO_NUM_NC)


/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

void lcd_set_brightness(uint8_t percent)
{
    if (lcd_io == NULL) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint8_t val = (uint8_t)((percent * 255) / 100);
    esp_err_t err = esp_lcd_panel_io_tx_param(lcd_io, 0xFE, (uint8_t []){0x00}, 1);
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(lcd_io, 0x51, &val, 1);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD brightness cmd failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LCD brightness set to %d%% (0x%02x)", percent, val);
    }
}

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

/* LCD Touch IO and panel */
static esp_lcd_panel_io_handle_t tp_io_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

#define TEST_DELAY_TIME_MS          (3000)
 
static SemaphoreHandle_t refresh_finish = NULL;

/**
 * @brief 全屏填充指定颜色
 *        参照 test_draw_bitmap 使用 SPI_SWAP_DATA_TX 处理字节序
 *        分段传输, 每次发送的行数能整除垂直分辨率
 * @param panel_handle LCD面板句柄
 * @param rgb565_color RGB565颜色值 (如 0x001F=蓝, 0xF800=红, 0x07E0=绿, 0xFFFF=白)
 */
static void test_draw_color_screen(esp_lcd_panel_handle_t panel_handle, uint16_t rgb565_color)
{
    refresh_finish = xSemaphoreCreateBinary();

    uint16_t row_line = TEST_LCD_V_RES / TEST_LCD_BIT_PER_PIXEL;  // 300/16=18行/段, 与test_draw_bitmap一致
    uint8_t byte_per_pixel = TEST_LCD_BIT_PER_PIXEL / 8;
    uint8_t *color = (uint8_t *)heap_caps_calloc(1, row_line * TEST_LCD_H_RES * byte_per_pixel, MALLOC_CAP_DMA);

    /* 使用SPI_SWAP_DATA_TX预交换字节序, 与test_draw_bitmap完全一致 */
    uint16_t color_swapped = SPI_SWAP_DATA_TX(rgb565_color, TEST_LCD_BIT_PER_PIXEL);
    for (int i = 0; i < row_line * TEST_LCD_H_RES; i++) {
        for (int k = 0; k < byte_per_pixel; k++) {
            color[i * byte_per_pixel + k] = (color_swapped >> (k * 8)) & 0xff;
        }
    }

    /* 分段发送, 覆盖全屏 */
    for (int j = 0; j < TEST_LCD_BIT_PER_PIXEL; j++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, j * row_line, TEST_LCD_H_RES, (j + 1) * row_line, color);
        xSemaphoreTake(refresh_finish, portMAX_DELAY);
    }

    free(color);
    vSemaphoreDelete(refresh_finish);
}

IRAM_ATTR static bool test_notify_refresh_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;

    xSemaphoreGiveFromISR(refresh_finish, &need_yield);
    return (need_yield == pdTRUE);
}

static void test_draw_bitmap(esp_lcd_panel_handle_t panel_handle)
{
    refresh_finish = xSemaphoreCreateBinary();
    //TEST_ASSERT_NOT_NULL(refresh_finish);

    uint16_t row_line = TEST_LCD_V_RES / TEST_LCD_BIT_PER_PIXEL;
    uint8_t byte_per_pixel = TEST_LCD_BIT_PER_PIXEL / 8;
    uint8_t *color = (uint8_t *)heap_caps_calloc(1, row_line * TEST_LCD_H_RES * byte_per_pixel, MALLOC_CAP_DMA);
    //TEST_ASSERT_NOT_NULL(color);

    for (int j = 0; j < TEST_LCD_BIT_PER_PIXEL; j++) {
        for (int i = 0; i < row_line * TEST_LCD_H_RES; i++) {
            for (int k = 0; k < byte_per_pixel; k++) {
                color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), TEST_LCD_BIT_PER_PIXEL) >> (k * 8)) & 0xff;
            }
        }
        esp_lcd_panel_draw_bitmap(panel_handle, 0, j * row_line, TEST_LCD_H_RES, (j + 1) * row_line, color);
        xSemaphoreTake(refresh_finish, portMAX_DELAY);
    }
    free(color);
    vSemaphoreDelete(refresh_finish);
    //vTaskDelay(pdMS_TO_TICKS(3000));
}


static void test_draw_bitmap2(esp_lcd_panel_handle_t panel_handle)
{
    refresh_finish = xSemaphoreCreateBinary();

    uint16_t row_line = TEST_LCD_V_RES / TEST_LCD_BIT_PER_PIXEL;
    uint8_t byte_per_pixel = TEST_LCD_BIT_PER_PIXEL / 8;
    uint8_t *color = (uint8_t *)heap_caps_calloc(1, row_line * TEST_LCD_H_RES * byte_per_pixel, MALLOC_CAP_DMA);

    for (int j = 0; j < TEST_LCD_BIT_PER_PIXEL; j++) {
        for (int i = 0; i < row_line * TEST_LCD_H_RES; i++) {
            uint16_t color_val = BIT(j);
            
            // 根据颜色格式处理
            if (TEST_LCD_BIT_PER_PIXEL == 16) {
                // 16位565格式：交换R和B分量
                uint16_t r = (color_val >> 11) & 0x1F;    // 取5位R
                uint16_t g = (color_val >> 5) & 0x3F;     // 取6位G
                uint16_t b = color_val & 0x1F;            // 取5位B
                
                // 交换R和B
                color_val = (b << 11) | (g << 5) | r;
                
                // 存储（小端格式）
                color[i * 2] = color_val & 0xFF;
                color[i * 2 + 1] = (color_val >> 8) & 0xFF;
            }
            else if (TEST_LCD_BIT_PER_PIXEL == 24 || TEST_LCD_BIT_PER_PIXEL == 32) {
                // 24/32位格式：RGB转BGR
                uint8_t r = (color_val >> 16) & 0xFF;
                uint8_t g = (color_val >> 8) & 0xFF;
                uint8_t b = color_val & 0xFF;
                
                color[i * byte_per_pixel] = b;      // B
                color[i * byte_per_pixel + 1] = g;  // G
                color[i * byte_per_pixel + 2] = r;  // R
                
                // 32位可能有Alpha通道
                if (byte_per_pixel == 4) {
                    color[i * 4 + 3] = 0xFF;  // Alpha固定为不透明
                }
            }
        }
        esp_lcd_panel_draw_bitmap(panel_handle, 0, j * row_line, TEST_LCD_H_RES, (j + 1) * row_line, color);
        xSemaphoreTake(refresh_finish, portMAX_DELAY);
    }
    free(color);
    vSemaphoreDelete(refresh_finish);
    //vTaskDelay(pdMS_TO_TICKS(3000));
}


static esp_err_t lcd_init(void)
{

    ESP_LOGI(TAG, "Initialize SPI bus");

    const spi_bus_config_t buscfg = SPD2010_PANEL_BUS_SPI_CONFIG(TEST_PIN_NUM_LCD_PCLK,
                                                                 TEST_PIN_NUM_LCD_DATA0,
                                                                 TEST_LCD_H_RES * TEST_LCD_V_RES * TEST_LCD_BIT_PER_PIXEL / 8);
    
    // const spi_bus_config_t buscfg = SPD2010_PANEL_BUS_QSPI_CONFIG(TEST_PIN_NUM_LCD_PCLK,
    //                                                               TEST_PIN_NUM_LCD_DATA0,
    //                                                               TEST_PIN_NUM_LCD_DATA1,
    //                                                               TEST_PIN_NUM_LCD_DATA2,
    //                                                               TEST_PIN_NUM_LCD_DATA3,
    //                                                               TEST_LCD_H_RES * TEST_LCD_V_RES * TEST_LCD_BIT_PER_PIXEL / 8);
    
    spi_bus_initialize(TEST_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    ESP_LOGI(TAG, "Install panel IO");

    const esp_lcd_panel_io_spi_config_t io_config = SPD2010_PANEL_IO_SPI_CONFIG(
        TEST_PIN_NUM_LCD_CS, 
        -1, 
        test_notify_refresh_ready, 
        NULL);

    // const esp_lcd_panel_io_spi_config_t io_config = SPD2010_PANEL_IO_QSPI_CONFIG(TEST_PIN_NUM_LCD_CS, NULL, NULL);

    esp_lcd_new_panel_io_spi(TEST_LCD_HOST, &io_config, &lcd_io);

    ESP_LOGI(TAG, "Install SPD2010 panel driver");

    const spd2010_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 0,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TEST_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = TEST_LCD_BIT_PER_PIXEL,
        .vendor_config = (void *) &vendor_config,
    };
    esp_lcd_new_panel_tk018(lcd_io, &panel_config, &lcd_panel);
    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);

    esp_lcd_panel_disp_on_off(lcd_panel, true);//显示开启

    return ESP_OK;
}

static esp_err_t lcd_deinit(void)
{
    esp_lcd_panel_del(lcd_panel);
    esp_lcd_panel_io_del(lcd_io);
    return ESP_OK;
}

static esp_err_t i2c_bus_init(void)
{
    const i2c_master_bus_config_t i2c_bus_conf = {
        .i2c_port = EXAMPLE_TOUCH_I2C_NUM,
        .sda_io_num = EXAMPLE_TOUCH_I2C_SDA,
        .scl_io_num = EXAMPLE_TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_new_master_bus(&i2c_bus_conf, &i2c_bus_handle);
    ESP_LOGI(TAG, "I2C bus init: port=%d, SCL=GPIO%d, SDA=GPIO%d, freq=%d Hz",
             EXAMPLE_TOUCH_I2C_NUM, EXAMPLE_TOUCH_I2C_SCL, EXAMPLE_TOUCH_I2C_SDA, EXAMPLE_TOUCH_I2C_CLK_HZ);
    return ESP_OK;
}

static void i2c_gpio_diagnose(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EXAMPLE_TOUCH_I2C_SCL) | (1ULL << EXAMPLE_TOUCH_I2C_SDA),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(10));
    int scl_level = gpio_get_level(EXAMPLE_TOUCH_I2C_SCL);
    int sda_level = gpio_get_level(EXAMPLE_TOUCH_I2C_SDA);
    if (scl_level == 0 || sda_level == 0) {
        ESP_LOGE(TAG, "I2C bus stuck low! SCL(GPIO%d)=%d, SDA(GPIO%d)=%d",
                 EXAMPLE_TOUCH_I2C_SCL, scl_level, EXAMPLE_TOUCH_I2C_SDA, sda_level);
        ESP_LOGE(TAG, "Check: wiring/pin swap, touch VCC 3.3V, pull-up resistors, touch RST pin");
        return;
    }
    ESP_LOGI(TAG, "I2C bus idle high (SCL=%d, SDA=%d)", scl_level, sda_level);

    /* GPIO驱动能力自测: 开漏拉低/释放, 验证引脚真的连到总线 */
    int drive_test_ok = 1;
    const int test_pins[2] = {EXAMPLE_TOUCH_I2C_SCL, EXAMPLE_TOUCH_I2C_SDA};
    const char *test_names[2] = {"SCL", "SDA"};
    for (int p = 0; p < 2; p++) {
        gpio_config_t od_conf = {
            .pin_bit_mask = (1ULL << test_pins[p]),
            .mode = GPIO_MODE_INPUT_OUTPUT_OD,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&od_conf);
        gpio_set_level(test_pins[p], 0);
        esp_rom_delay_us(1000);
        int low_read = gpio_get_level(test_pins[p]);
        gpio_set_level(test_pins[p], 1);
        esp_rom_delay_us(1000);
        int high_read = gpio_get_level(test_pins[p]);
        ESP_LOGI(TAG, "GPIO drive test %s(GPIO%d): set 0 -> read %d, set 1 -> read %d",
                 test_names[p], test_pins[p], low_read, high_read);
        if (low_read != 0 || high_read != 1) {
            drive_test_ok = 0;
            if (low_read != 0) {
                ESP_LOGE(TAG, "  %s set 0 but read %d: pin cannot pull bus low (wrong pin? not connected?)",
                         test_names[p], low_read);
            }
            if (high_read != 1) {
                ESP_LOGE(TAG, "  %s set 1 but read %d: bus held low by slave or short to GND",
                         test_names[p], high_read);
            }
        }
    }
    if (!drive_test_ok) {
        ESP_LOGE(TAG, "GPIO drive test FAILED: I2C pins cannot control bus, check wiring!");
    }
}

static void i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C bus scanning...");
    uint8_t found = 0;
    int timeouts = 0, nacks = 0, others = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(i2c_bus_handle, addr, pdMS_TO_TICKS(10));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  found device at 0x%02X", addr);
            found++;
        } else if (ret == ESP_ERR_TIMEOUT) {
            timeouts++;
        } else if (ret == ESP_FAIL) {
            nacks++;
        } else {
            others++;
            if (others <= 3) {
                ESP_LOGW(TAG, "  addr 0x%02X unexpected error: %s (0x%x)",
                         addr, esp_err_to_name(ret), ret);
            }
        }
    }
    ESP_LOGI(TAG, "I2C scan summary: found=%u, timeout=%d, nack=%d, other=%d", found, timeouts, nacks, others);
    if (found == 0) {
        if (timeouts > 100) {
            ESP_LOGE(TAG, "Mostly TIMEOUT: SCL/SDA signal path broken (pins swapped? no external pull-up? wrong GPIO?)");
        } else {
            ESP_LOGW(TAG, "Mostly NACK: bus OK but no device (check touch VCC 3.3V / wrong address / not powered)");
        }
    }
}

static esp_err_t app_touch_init(void)
{
    /* GPIO级诊断必须在I2C驱动安装之前执行 */
    i2c_gpio_diagnose();

    /* Initilize I2C */
    if (i2c_bus_init()) {return ESP_FAIL;}
    i2c_scan();

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = TEST_LCD_H_RES,
        .y_max = TEST_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = (void *)(intptr_t)EXAMPLE_TOUCH_I2C_NUM,
    };

#if TOUCH_CHIP

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CHSC6417_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_TOUCH_I2C_CLK_HZ;
    if (esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle)) {
        ESP_LOGE(TAG, "Failed to create CHSC6417 panel io");
        return ESP_FAIL;
    }
    return esp_lcd_touch_new_i2c_chsc6417(tp_io_handle, &tp_cfg, &touch_handle);

#else

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_TOUCH_I2C_CLK_HZ;
    if (esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle)) {
        ESP_LOGE(TAG, "Failed to create FT5x06 panel io");
        return ESP_FAIL;
    }
    return esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &touch_handle);

#endif
    
}

static esp_err_t lvgl_init(void)
{
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         /* LVGL task priority */
        .task_stack = 8192,         /* LVGL task stack size */
        .task_affinity = -1,        /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500,   /* Maximum sleep in LVGL task */
        .timer_period_ms = 5        /* LVGL timer tick period in ms */
    };
    if (lvgl_port_init(&lvgl_cfg)) {return ESP_FAIL;}

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    ESP_LOGI(TAG, "free DMA heap before add disp: %" PRIu32 " bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = TEST_LCD_V_RES * 40,
        .double_buffer = 1,
        .trans_size = 0,
        .hres = TEST_LCD_H_RES,
        .vres = TEST_LCD_V_RES,
        .monochrome = 0,             
        .color_format = LV_COLOR_FORMAT_RGB565,

        .rotation = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .flags = {
            .buff_dma = 1,      //使用dma
            .buff_spiram = 0,   //使用spiram
            .sw_rotate = 0,     //旋转角度
            .swap_bytes = 1,    //交换字节
            .full_refresh = 0,  //部分刷新
            .direct_mode = 0,   //直接模式
        }
    };

    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to add display");
        return ESP_FAIL;
    }

#if TOUCH_ENABLE
    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (lvgl_touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to add touch input");
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

static void app_main_display(void)
{
    /* Task lock */
    lvgl_port_lock(0);

    // lv_display_set_rotation(lvgl_disp, LV_DISPLAY_ROTATION_180);//旋转180度

    /* LVGL demo */
    // lv_demo_widgets();

    wind_ui_init();
    // band_ui_init();
    
    /* Task unlock */
    lvgl_port_unlock();
}





void app_main(void)
{
#if TOUCH_ENABLE
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    if (app_touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed, touch will be disabled");
    }

#endif

    lcd_init();

#if test_lvgl

    /* LVGL initialization */
    if (lvgl_init() != ESP_OK) {
        ESP_LOGE(TAG, "LVGL initialization failed, stop here");
        return;
    }
    app_main_display();

#else

    int n = 0;
    while(1)
    {

        if(n%2)
        {
            test_draw_color_screen(lcd_panel, 0xFFE0); //黄色
            vTaskDelay(pdMS_TO_TICKS(2000));//等待2秒
            test_draw_bitmap(lcd_panel);
            vTaskDelay(pdMS_TO_TICKS(2000));//等待2秒
        }
           
        else
        {
            test_draw_color_screen(lcd_panel, 0xF800); //红色
            vTaskDelay(pdMS_TO_TICKS(2000));//等待2秒
            test_draw_bitmap2(lcd_panel);
            vTaskDelay(pdMS_TO_TICKS(2000));//等待2秒
        }

        n++;
        vTaskDelay(pdMS_TO_TICKS(100));
        
    }
    
#endif

    while(1) vTaskDelay(pdMS_TO_TICKS(2000));//等待2秒
}
