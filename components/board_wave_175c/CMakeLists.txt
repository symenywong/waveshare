idf_component_register(
    SRCS
        "src/board_wave_175c.c"
        "src/board_wave_175c_contract.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_lcd
        espressif__esp_lcd_co5300
        heap
        log
)
