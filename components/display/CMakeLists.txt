idf_component_register(
    SRCS "display.c" "display_font.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board max7219 esp_driver_spi
)