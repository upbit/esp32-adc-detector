#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "ina226.h"

#define INA226_ADDRESS 0x44 // Scanned on hardware: this module straps A1 high, not the 0x40 default.
#define REG_CONFIG 0x00
#define REG_BUS_VOLTAGE 0x02
#define REG_CURRENT 0x04
#define REG_CALIBRATION 0x05
#define REG_MASK_ENABLE 0x06
#define REG_MANUFACTURER 0xFE
#define REG_DIE_ID 0xFF
#define CONVERSION_READY (1U << 3)
#define MATH_OVERFLOW (1U << 2)
#define I2C_TIMEOUT_MS 10

// R010 = 0.01 ohm; CAL = 0.00512 / (0.0001 A * 0.01 ohm) = 5120.
#define CALIBRATION 0x1400
#define CURRENT_LSB_A 0.0001f
// Zero offset measured with VOUT floating (no load, no MPPT): raw -0.7/-1.0 mA, ~-8.6 uV shunt offset.
#define CURRENT_OFFSET_A -0.00085f
#define BUS_VOLTAGE_LSB_V 0.00125f
// 16 averages, 1.1 ms per shunt/bus conversion, continuous shunt + bus (~35.2 ms).
#define CONFIGURATION 0x4527

static i2c_master_dev_handle_t device;

static esp_err_t read_register(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t err = i2c_master_transmit_receive(device, &reg, 1, data, sizeof(data), I2C_TIMEOUT_MS);
    if (err == ESP_OK) *value = ((uint16_t)data[0] << 8) | data[1];
    return err;
}

static esp_err_t write_register(uint8_t reg, uint16_t value)
{
    const uint8_t data[] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_transmit(device, data, sizeof(data), I2C_TIMEOUT_MS);
}

// Runs only after the fixed address fails: scan the bus, then test for the module's pull-ups.
static void log_diagnostics(i2c_master_bus_handle_t bus)
{
    int found = 0, nack = 0, other = 0;
    for (int address = 0x08; address <= 0x77; address++) {
        esp_err_t probe = i2c_master_probe(bus, address, 25);
        if (probe == ESP_OK) {
            ESP_LOGW("ina226", "I2C device responds at 0x%02X", address);
            found++;
        } else if (probe == ESP_ERR_NOT_FOUND) {
            nack++;
        } else {
            other++;
        }
    }
    ESP_LOGW("ina226", "I2C scan: %d found, %d NACK, %d other errors; idle SDA=%d SCL=%d (both should be 1)",
             found, nack, other, gpio_get_level(INA226_PIN_SDA), gpio_get_level(INA226_PIN_SCL));
    i2c_del_master_bus(bus);
    // Against the ~45 kohm internal pull-down, only a powered, connected module pull-up reads 1.
    gpio_config_t pins = {
        .pin_bit_mask = (1ULL << INA226_PIN_SDA) | (1ULL << INA226_PIN_SCL),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&pins);
    esp_rom_delay_us(100);
    ESP_LOGW("ina226", "With pull-down only: SDA=%d SCL=%d (1 = module pull-up seen; 0 = unpowered, open or no pull-up)",
             gpio_get_level(INA226_PIN_SDA), gpio_get_level(INA226_PIN_SCL));
    gpio_reset_pin(INA226_PIN_SDA);
    gpio_reset_pin(INA226_PIN_SCL);
}

esp_err_t ina226_init(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = INA226_PIN_SDA,
        .scl_io_num = INA226_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) return err;
    // Retry a transient NACK, as with the touch controller; the address never changes.
    for (int attempt = 0; attempt < 3; attempt++) {
        err = i2c_master_probe(bus, INA226_ADDRESS, 25);
        if (err != ESP_ERR_NOT_FOUND) break;
        if (attempt < 2) {
            ESP_LOGW("ina226", "0x%02X did not ACK; retrying in 20 ms", INA226_ADDRESS);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW("ina226", "Probe 0x%02X failed: %s", INA226_ADDRESS, esp_err_to_name(err));
        log_diagnostics(bus);
        return err;
    }
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_ADDRESS,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) goto fail;

    uint16_t manufacturer, die_id, calibration, configuration;
    err = read_register(REG_MANUFACTURER, &manufacturer);
    if (err != ESP_OK) goto fail;
    err = read_register(REG_DIE_ID, &die_id);
    if (err != ESP_OK) goto fail;
    if (manufacturer != 0x5449 || (die_id & 0xFFF0) != 0x2260) {
        ESP_LOGW("ina226", "Unexpected ID at 0x%02X: manufacturer=0x%04X die=0x%04X",
                 INA226_ADDRESS, manufacturer, die_id);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }
    err = write_register(REG_CALIBRATION, CALIBRATION);
    if (err != ESP_OK) goto fail;
    err = write_register(REG_MASK_ENABLE, 0); // ALERT stays disabled; GPIO39 is not driven.
    if (err != ESP_OK) goto fail;
    err = write_register(REG_CONFIG, CONFIGURATION);
    if (err != ESP_OK) goto fail;
    err = read_register(REG_CALIBRATION, &calibration);
    if (err != ESP_OK) goto fail;
    err = read_register(REG_CONFIG, &configuration);
    if (err != ESP_OK) goto fail;
    if (calibration != CALIBRATION || configuration != CONFIGURATION) {
        ESP_LOGW("ina226", "Readback mismatch: CAL=0x%04X CONFIG=0x%04X", calibration, configuration);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }
    ESP_LOGI("ina226", "INA226 at 0x%02X, SDA=%d SCL=%d, die=0x%04X; R010=0.01 ohm, CAL=0x1400, 0.1 mA/LSB",
             INA226_ADDRESS, INA226_PIN_SDA, INA226_PIN_SCL, die_id);
    ESP_LOGI("ina226", "16 averages / 1.1 ms shunt + bus; current positive = charging, negative = discharging");
    return ESP_OK;

fail:
    if (device) {
        i2c_master_bus_rm_device(device);
        device = NULL;
    }
    i2c_del_master_bus(bus);
    return err;
}

esp_err_t ina226_read(ina226_sample_t *sample)
{
    if (!device) return ESP_ERR_INVALID_STATE;
    uint16_t calibration, status, voltage, current;
    esp_err_t err = read_register(REG_CALIBRATION, &calibration);
    if (err != ESP_OK) return err;
    // A sensor power cycle clears CAL; do not report the resulting zero current as valid.
    if (calibration != CALIBRATION) return ESP_ERR_INVALID_STATE;
    err = read_register(REG_MASK_ENABLE, &status);
    if (err != ESP_OK) return err;
    if (status & MATH_OVERFLOW) return ESP_ERR_INVALID_RESPONSE;
    if (!(status & CONVERSION_READY)) return ESP_ERR_NOT_FINISHED;
    err = read_register(REG_BUS_VOLTAGE, &voltage);
    if (err != ESP_OK) return err;
    err = read_register(REG_CURRENT, &current);
    if (err != ESP_OK) return err;
    *sample = (ina226_sample_t){
        .voltage_v = voltage * BUS_VOLTAGE_LSB_V * INA226_BUS_VOLTAGE_GAIN,
        // Battery is on VIN = IN+, so the chip's positive IN+ -> IN- direction is discharge.
        .current_a = CURRENT_OFFSET_A - (int16_t)current * CURRENT_LSB_A,
    };
    return ESP_OK;
}
