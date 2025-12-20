idf_component_register(SRCS "bat_adc.c" "closer.c" "wait_group.c" "main.c"
                    PRIV_REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_driver_gpio i2c_bus bme280 esp_adc ulp
                    INCLUDE_DIRS "."
                    INCLUDE_DIRS "../../common")