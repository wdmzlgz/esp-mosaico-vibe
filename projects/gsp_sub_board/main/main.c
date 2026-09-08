// SPDX-License-Identifier: Apache-2.0

#include "board_display.h"
#include "bsp/esp_mosaico.h"
#include "bundle_gsp.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_log.h"
#include "gsp_sub_board_app.h"
#include "iris_gsp_debug.h"
#include "iris_screen_mirror.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"
#include "ui_bundle.h"

static const char *TAG = "gsp_sub_board";

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    /* Keep Recovery reachable even when the external UI image is absent or
     * invalid. */
    iris_ota_support_start();

    esp_gsp_config_t app_config;
    const esp_err_t bundle_err = ui_bundle_open(&app_config);
    if (bundle_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "GSP UI unavailable (0x%x); ESP-Iris Recovery RPC remains active",
                 bundle_err);
        return;
    }

    esp_display_present_target_config_t display;
    if (board_display_init(&display) != ESP_OK) {
        ESP_LOGE(TAG, "display init failed; ESP-Iris Recovery RPC remains active");
        return;
    }
    if (iris_gsp_debug_wrap_panel(
            display.hw.panel, BSP_LCD_H_RES, BSP_LCD_V_RES,
            &display.hw.panel) != ESP_OK) {
        ESP_LOGE(TAG, "debug panel wrap failed; ESP-Iris Recovery RPC remains active");
        return;
    }

    esp_lcd_touch_handle_t touch = NULL;
    if (board_touch_init(&touch) != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed; ESP-Iris Recovery RPC remains active");
        return;
    }

    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;

    if (iris_screen_mirror_init() != ESP_OK) {
        ESP_LOGE(TAG, "screen mirror init failed; ESP-Iris Recovery RPC remains active");
        return;
    }

    esp_gsp_handle_t ui;
    if (esp_gsp_esp_lcd_start(&app_config, &lcd, &ui) != ESP_OK) {
        ESP_LOGE(TAG, "GSP start failed; ESP-Iris Recovery RPC remains active");
        return;
    }
    if (gsp_app_start(ui) != ESP_OK) {
        ESP_LOGE(TAG, "GSP app start failed; ESP-Iris Recovery RPC remains active");
        return;
    }
    if (iris_gsp_debug_register(ui) != ESP_OK) {
        ESP_LOGE(TAG, "GSP debug register failed");
    }

    ESP_LOGI(TAG, "GSP Sub Board ready at %dx%d RGB565",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
}
