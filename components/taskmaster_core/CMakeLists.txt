# taskmaster_core — the OS component: app framework + platform services (PLAN §6.1).
idf_component_register(
    SRCS
        "app_manager.c"
        "task_model.c"
        "net_status.c"
        "nvs_config.c"
        "app_store.c"
        "wifi_mgr.c"
        "launcher.c"
        "ui.c"
        "sh1106.c"
        "input.c"
        "softap_portal.c"
    INCLUDE_DIRS "include"          # public API: app.h, app_manager.h, task_model.h, net_status.h, nvs_config.h, app_store.h, launcher.h, ui.h, sh1106.h, input.h, softap_portal.h, board_pins.h
    PRIV_INCLUDE_DIRS "."           # private: font5x7.h next to sh1106.c
    # public deps: headers in include/ pull these in (board_pins.h→gpio, input.h→freertos)
    REQUIRES
        esp_driver_gpio
        freertos
    # private deps: used only in the .c files
    PRIV_REQUIRES
        esp_driver_i2c
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        lwip
        nvs_flash
        esp_timer
)
