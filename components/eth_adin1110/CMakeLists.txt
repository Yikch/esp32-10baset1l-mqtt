idf_component_register(
    SRCS "eth_adin1110.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif
    PRIV_REQUIRES adin1110 esp_event esp_driver_gpio
)
