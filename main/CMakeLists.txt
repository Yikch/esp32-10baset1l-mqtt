idf_component_register(
    SRCS "app_main.c"
    PRIV_REQUIRES esp_event esp_netif nvs_flash
                  app_config i2c_sensor sensor_payload eth_adin1110
    INCLUDE_DIRS "."
)
