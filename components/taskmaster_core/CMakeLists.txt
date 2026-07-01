# taskmaster_core — the OS component: app framework + platform services (PLAN §6.1).
# Sources are grouped into subfolders by domain; each module's .c and .h sit together.
# Every subfolder is on the include path, so `#include "foo.h"` resolves by name (app
# components + inter-module includes are unaffected by the layout).
idf_component_register(
    SRCS
        # platform/ — board + OLED driver + input + LVGL display glue
        "platform/sh1106.c"
        "platform/input.c"
        "platform/lvgl_disp.c"
        # ui/ — UI toolkit: the render task, OS frame + hint bar, generic list, Launcher, fonts
        "ui/ui.c"
        "ui/ui_frame.c"
        "ui/ui_list.c"
        "ui/hint_bar.c"
        "ui/hint_glyphs.c"
        "ui/launcher.c"
        "ui/lv_font_tm_sans.c"
        "ui/lv_font_tm5x7.c"
        # app/ — app framework: registry + background-job service
        "app/app_manager.c"
        "app/async_job.c"
        # storage/ — device config schema + per-app storage + app-declared config
        "storage/nvs_config.c"
        "storage/app_store.c"
        "storage/app_config.c"
        # net/ — Wi-Fi owner, provisioning portal, connectivity status
        "net/net_status.c"
        "net/wifi_mgr.c"
        "net/softap_portal.c"
        # settings/ — the Settings hub app + its schema editor + confirm dialog
        "settings/app_settings.c"
        "settings/settings_menu.c"
        "settings/confirm.c"
        # test/ — §6A.4 leak harness (debug builds)
        "test/leak_test.c"
    # Public headers live beside their .c in each module folder — all on the include path.
    INCLUDE_DIRS
        "platform" "ui" "app" "storage" "net" "settings" "test"
    # public deps: headers pull these in (board_pins.h→gpio, input.h→freertos,
    # ui_frame.h/lvgl_disp.h→lvgl)
    REQUIRES
        esp_driver_gpio
        freertos
        lvgl
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
        esp_app_format     # esp_app_get_description() for the Device-info screen
        esp_https_ota      # OTA update from fw_url (Settings → Check update, step 7)
        bootloader_support # pulled in by esp_https_ota.h (bootloader_common.h)
        esp-tls            # esp_crt_bundle_attach for the OTA HTTPS connection
        mbedtls            # esp_crt_bundle.h
)
