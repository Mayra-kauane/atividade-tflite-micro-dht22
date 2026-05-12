idf_component_register(SRCS "dht_sensor_main.c"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_timer
                       INCLUDE_DIRS "")
