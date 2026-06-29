idf_component_register(
    SRCS
        "main.c"
        "sh1106.c"
        "input.c"
        "softap_portal.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        lwip
)
