/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>
#include "sdkconfig.h"

#if CONFIG_LCD_ENABLE_DEBUG_LOG
// The local log level must be defined before including esp_log.h
// Set the maximum log level for this source file
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_compiler.h"

#define TK0134_CMD_RAMCTRL               0xb0
#define TK0134_DATA_LITTLE_ENDIAN_BIT    (1 << 3)

static const char *TAG = "lcd_panel.tk0134";

static esp_err_t panel_tk0134_del(esp_lcd_panel_t *panel);
static esp_err_t panel_tk0134_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_tk0134_init(esp_lcd_panel_t *panel);
static esp_err_t panel_tk0134_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                          const void *color_data);
static esp_err_t panel_tk0134_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_tk0134_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_tk0134_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_tk0134_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_tk0134_disp_on_off(esp_lcd_panel_t *panel, bool off);
static esp_err_t panel_tk0134_sleep(esp_lcd_panel_t *panel, bool sleep);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;    // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val;    // save current value of LCD_CMD_COLMOD register
    uint8_t ramctl_val_1;
    uint8_t ramctl_val_2;
} tk0134_panel_t;

esp_err_t
esp_lcd_new_panel_tk0134(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                         esp_lcd_panel_handle_t *ret_panel)
{
#if CONFIG_LCD_ENABLE_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    esp_err_t ret = ESP_OK;
    tk0134_panel_t *tk0134 = NULL;
    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    // leak detection of tk0134 because saving tk0134->base address
    ESP_COMPILER_DIAGNOSTIC_PUSH_IGNORE("-Wanalyzer-malloc-leak")
    tk0134 = calloc(1, sizeof(tk0134_panel_t));
    ESP_GOTO_ON_FALSE(tk0134, ESP_ERR_NO_MEM, err, TAG, "no mem for tk0134 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        tk0134->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        tk0134->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported RGB element order");
        break;
    }

    uint8_t fb_bits_per_pixel = 0;
    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        tk0134->colmod_val = 0x55;
        fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        tk0134->colmod_val = 0x66;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    tk0134->ramctl_val_1 = 0x00;
    tk0134->ramctl_val_2 = 0xf0;    // Use big endian by default
    if ((panel_dev_config->data_endian) == LCD_RGB_DATA_ENDIAN_LITTLE) {
        // Use little endian
        tk0134->ramctl_val_2 |= TK0134_DATA_LITTLE_ENDIAN_BIT;
    }

    tk0134->io = io;
    tk0134->fb_bits_per_pixel = fb_bits_per_pixel;
    tk0134->reset_gpio_num = panel_dev_config->reset_gpio_num;
    tk0134->reset_level = panel_dev_config->flags.reset_active_high;
    tk0134->base.del = panel_tk0134_del;
    tk0134->base.reset = panel_tk0134_reset;
    tk0134->base.init = panel_tk0134_init;
    tk0134->base.draw_bitmap = panel_tk0134_draw_bitmap;
    tk0134->base.invert_color = panel_tk0134_invert_color;
    tk0134->base.set_gap = panel_tk0134_set_gap;
    tk0134->base.mirror = panel_tk0134_mirror;
    tk0134->base.swap_xy = panel_tk0134_swap_xy;
    tk0134->base.disp_on_off = panel_tk0134_disp_on_off;
    tk0134->base.disp_sleep = panel_tk0134_sleep;
    *ret_panel = &(tk0134->base);
    ESP_LOGD(TAG, "new tk0134 panel @%p", tk0134);

    return ESP_OK;

err:
    if (tk0134) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(tk0134);
    }
    return ret;
    ESP_COMPILER_DIAGNOSTIC_POP("-Wanalyzer-malloc-leak")
}

static esp_err_t panel_tk0134_del(esp_lcd_panel_t *panel)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);

    if (tk0134->reset_gpio_num >= 0) {
        gpio_reset_pin(tk0134->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del tk0134 panel @%p", tk0134);
    free(tk0134);
    return ESP_OK;
}

static esp_err_t panel_tk0134_reset(esp_lcd_panel_t *panel)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;

    // perform hardware reset
    if (tk0134->reset_gpio_num >= 0) {
        gpio_set_level(tk0134->reset_gpio_num, tk0134->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(tk0134->reset_gpio_num, !tk0134->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else { // perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG,
                            "io tx param failed");
        vTaskDelay(pdMS_TO_TICKS(20)); // spec, wait at least 5m before sending new command
    }

    return ESP_OK;
}

static esp_err_t panel_tk0134_init(esp_lcd_panel_t *panel)
{

    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;

    // // // 获取自定义面板结构体
    // custom_panel_t *custom_panel = __containerof(panel, custom_panel_t, base);
    // esp_lcd_panel_io_handle_t io = custom_panel->io;

    //************* Start Initial Sequence **********//
    // PASSWORD
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xDF, (uint8_t[]) {0x98, 0x51, 0xE9}, 3), 
                      TAG, "Send password failed");

    //---------------- PAGE0 --------------
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xDE, (uint8_t[]) {0x00}, 1), 
                      TAG, "Set PAGE0 failed");
    
    // VGMP,VGSP,VGMN,VGSN
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB7, (uint8_t[]) {0x31, 0x88, 0x31, 0x10}, 4),
                      TAG, "Set VGMP/VGSP failed");

    // Set_R_GAMMA (32 parameters)
    uint8_t gamma_params[32] = {
        0x3F, 0x3C, 0x34, 0x2D, 0x2D, 0x2E, 0x2A, 0x2C, 
        0x2C, 0x2D, 0x2D, 0x2A, 0x2A, 0x2A, 0x28, 0x0E,
        0x3F, 0x3C, 0x34, 0x2D, 0x2D, 0x2E, 0x2A, 0x2C,
        0x2C, 0x2D, 0x2D, 0x2A, 0x2A, 0x2A, 0x28, 0x0E
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC8, gamma_params, 32),
                      TAG, "Set gamma failed");

    // POW_CTRL
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB9, (uint8_t[]) {0x33, 0x28, 0xCC}, 3),
                      TAG, "Power control failed");

    // DCDC_SEL (8 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xBB, 
                      (uint8_t[]) {0x06, 0x7A, 0x30, 0x30, 0x6C, 0x60, 0x50, 0x70}, 8),
                      TAG, "DCDC select failed");

    // VDDD_CTRL
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xBC, (uint8_t[]) {0x38, 0x3C}, 2),
                      TAG, "VDDD control failed");
    
    // SETSTBA
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]) {0x31, 0x20}, 2),
                      TAG, "Set STBA failed");

    // SETPANEL(default)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC1, (uint8_t[]) {0x16}, 1),
                      TAG, "Set panel failed");

    // SETRGBCYC (9 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC3, 
                      (uint8_t[]) {0x08, 0x00, 0x0A, 0x10, 0x08, 0x54, 0x45, 0x71, 0x2C}, 9),
                      TAG, "Set RGB cycle failed");

    // SETRGBCYC(default) (17 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC4, 
                      (uint8_t[]) {0x00, 0xA0, 0x79, 0x0E, 0x0A, 0x16, 0x79, 0x0E, 0x0A, 
                      0x16, 0x79, 0x0E, 0x0A, 0x16, 0x82, 0x00, 0x03}, 17),
                      TAG, "Set RGB cycle default failed");

    // SET_GD(default) (6 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xD0, 
                      (uint8_t[]) {0x04, 0x0C, 0x6B, 0x0F, 0x07, 0x03}, 6),
                      TAG, "Set GD failed");

    // RAMCTRL(default)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xD7, (uint8_t[]) {0x00, 0x00}, 2),
                      TAG, "RAM control failed");

    //---------------- PAGE2 --------------
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xDE, (uint8_t[]) {0x02}, 1),
                      TAG, "Set PAGE2 failed");
    vTaskDelay(pdMS_TO_TICKS(1));

    // DCDC_SET (5 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB8, 
                      (uint8_t[]) {0x19, 0xA0, 0x2F, 0x04, 0x33}, 5),
                      TAG, "DCDC set failed");

    // SETRGBCYC2 (4 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC1, 
                      (uint8_t[]) {0x10, 0x66, 0x66, 0x01}, 4),
                      TAG, "Set RGB cycle2 failed");

    //---------------- PAGE0 --------------
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xDE, (uint8_t[]) {0x00}, 1),
                      TAG, "Return to PAGE0 failed");

    // Sleep out
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0),
                      TAG, "Sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    //---------------- PAGE2 --------------
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xDE, (uint8_t[]) {0x02}, 1),
                      TAG, "Set PAGE2 failed");
    vTaskDelay(pdMS_TO_TICKS(1));

    // OSCM (3 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC5, 
                      (uint8_t[]) {0x01, 0x00, 0x00}, 3),
                      TAG, "Set OSCM failed");
    vTaskDelay(pdMS_TO_TICKS(1));

    // SETMIPI_2 (3 parameters)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xCA, 
                      (uint8_t[]) {0x10, 0x20, 0xF4}, 3),
                      TAG, "Set MIPI failed");
    vTaskDelay(pdMS_TO_TICKS(1));

    //---------------- PAGE0 --------------
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xDE, (uint8_t[]) {0x00}, 1),
                      TAG, "Return to PAGE0 failed");
    vTaskDelay(pdMS_TO_TICKS(1));

    // Pixel format
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0x3A, (uint8_t[]) {0x05}, 1),
                      TAG, "Set pixel format failed");

    // Display on
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_DISPON, NULL, 0),
                      TAG, "Display on failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    // Invert display
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0x21, NULL, 0),
                      TAG, "Invert display failed");

    return ESP_OK;
}


/*
static const spd2010_lcd_init_cmd_t vendor_specific_init_default[] = {

    //{cmd, { data }, data_size, delay_ms}
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x0C, (uint8_t []){0x11}, 1, 0},
    {0x10, (uint8_t []){0x02}, 1, 0},
    {0x11, (uint8_t []){0x11}, 1, 0},
    {0x15, (uint8_t []){0x42}, 1, 0},
    {0x16, (uint8_t []){0x11}, 1, 0},
    {0x1A, (uint8_t []){0x02}, 1, 0},
    {0x11, (uint8_t []){}, 0, 10},
    {0xfd, (uint8_t []){0x06, 0x08}, 2, 0},
    {0x61, (uint8_t []){0x07, 0x04}, 2, 0},
    {0x62, (uint8_t []){0x00, 0x44, 0x45}, 3, 0},
    {0x63, (uint8_t []){0x41, 0x07, 0x12, 0x12}, 4, 0},
    {0x64, (uint8_t []){0x37}, 1, 0},
    {0x65, (uint8_t []){0x09, 0x10, 0x21}, 3, 0},
    {0x66, (uint8_t []){0x09, 0x10, 0x21}, 3, 0},
    {0x67, (uint8_t []){0x20, 0x40}, 2, 0},
    {0x68, (uint8_t []){0x90, 0x4c, 0x7C, 0x66}, 4, 0},
    {0xb1, (uint8_t []){0x0F, 0x02, 0x01}, 3, 0},
    {0xB4, (uint8_t []){0x01}, 1, 0},
    {0xB5, (uint8_t []){0x02, 0x02, 0x0a, 0x14}, 4, 0},
    {0xB6, (uint8_t []){0x04, 0x01, 0x9f, 0x00, 0x02}, 5, 0},
    {0xdf, (uint8_t []){0x11}, 1, 0},
    {0xE2, (uint8_t []){0x13, 0x00, 0x00, 0x30, 0x33, 0x3f}, 6, 0},
    {0xE5, (uint8_t []){0x3f, 0x33, 0x30, 0x00, 0x00, 0x13}, 6, 0},
    {0xE1, (uint8_t []){0x00, 0x57}, 2, 0},
    {0xE4, (uint8_t []){0x58, 0x00}, 2, 0},
    {0xE0, (uint8_t []){0x01, 0x03, 0x0e, 0x0e, 0x0c, 0x15, 0x19}, 7, 0},
    {0xE3, (uint8_t []){0x1a, 0x16, 0x0C, 0x0f, 0x0e, 0x0d, 0x02, 0x01}, 8, 0},
    {0xE6, (uint8_t []){0x00, 0xff}, 2, 0},
    {0xE7, (uint8_t []){0x01, 0x04, 0x03, 0x03, 0x00, 0x12}, 6, 0},
    {0xE8, (uint8_t []){0x00, 0x70, 0x00}, 3, 0},
    {0xEc, (uint8_t []){0x52}, 1, 0},
    {0xF1, (uint8_t []){0x01, 0x01, 0x02}, 3, 0},
    {0xF6, (uint8_t []){0x09, 0x10, 0x00, 0x00}, 4, 0},
    {0xfd, (uint8_t []){0xfa, 0xfc}, 2, 0},
    {0x3a, (uint8_t []){0x05}, 1, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x21, (uint8_t []){}, 0, 1},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0x11, (uint8_t []){}, 0, 100},
    {0x29, (uint8_t []){}, 0, 0},
};

*/

static esp_err_t panel_tk0134_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                          const void *color_data)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;

    x_start += tk0134->x_gap;
    x_end += tk0134->x_gap;
    y_start += tk0134->y_gap;
    y_end += tk0134->y_gap;

    // define an area of frame memory where MCU can access
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG, "io tx param failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG, "io tx param failed");
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * tk0134->fb_bits_per_pixel / 8;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len), TAG, "io tx color failed");

    return ESP_OK;
}

static esp_err_t panel_tk0134_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
                        "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_tk0134_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;
    if (mirror_x) {
        tk0134->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        tk0134->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        tk0134->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        tk0134->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        tk0134->madctl_val
    }, 1), TAG, "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_tk0134_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;
    if (swap_axes) {
        tk0134->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        tk0134->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        tk0134->madctl_val
    }, 1), TAG, "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_tk0134_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    tk0134->x_gap = x_gap;
    tk0134->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_tk0134_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;
    int command = 0;
    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
                        "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_tk0134_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    tk0134_panel_t *tk0134 = __containerof(panel, tk0134_panel_t, base);
    esp_lcd_panel_io_handle_t io = tk0134->io;
    int command = 0;
    if (sleep) {
        command = LCD_CMD_SLPIN;
    } else {
        command = LCD_CMD_SLPOUT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
                        "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}
