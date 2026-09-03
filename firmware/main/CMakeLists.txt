idf_component_register(SRCS "display_test.c" "rs485.c" "rs485_echo.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd driver)
