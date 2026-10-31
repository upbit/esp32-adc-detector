# Solar dashboard with touch diagnostics; legacy radio/RLCD sources are not built.
idf_component_register(SRCS "main.c" "display.c" "ui.c" "ina226.c" "touch.c" "battery.c"
    "fonts/font_ui_text_16.c" "fonts/font_ui_numbers_36.c" "fonts/font_ui_soc_88.c" "fonts/font_ui_soc_64.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c esp_timer esp_psram esp_hw_support spi_flash)
