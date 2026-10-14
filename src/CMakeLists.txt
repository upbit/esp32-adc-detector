idf_component_register(SRCS "main.c" "starchart.c" "starchart_render.c" "chart_hardware.c"
    INCLUDE_DIRS "../include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_timer esp_wifi
             esp_event esp_netif nvs_flash)
set_source_files_properties(starchart_render.c PROPERTIES COMPILE_OPTIONS "-O2")
