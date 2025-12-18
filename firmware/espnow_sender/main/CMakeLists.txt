idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_driver_gpio i2c_bus bme280
                    INCLUDE_DIRS "."
                    INCLUDE_DIRS "../../common")