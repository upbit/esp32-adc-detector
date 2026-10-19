# Minimal AMOLED bring-up; legacy radio/RLCD sources remain outside this build.
idf_component_register(SRCS "main.c" "display.c" "radar.c" "ui.c" "touch.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c esp_timer esp_psram esp_hw_support spi_flash)
