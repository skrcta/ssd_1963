idf_component_register(SRCS "display_test.c" "rs485.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd driver)
