// SPDX-License-Identifier: Apache-2.0

#include "board_display.h"
#include "bsp/esp_mosaico.h"
#include "bundle_gsp.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_log.h"
#include "gsp_air_battle_app.h"
#include "iris_gsp_debug.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"

static const char *TAG = "gsp_air_battle";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_display_present_target_config_t display;
    ESP_ERROR_CHECK(board_display_init(&display));
    ESP_ERROR_CHECK(iris_gsp_debug_wrap_panel(
        display.hw.panel, BSP_LCD_H_RES, BSP_LCD_V_RES, &display.hw.panel));

    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(board_touch_init(&touch));

    esp_gsp_config_t app_config = gsp_bundle_config();
    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;

    esp_gsp_handle_t ui;
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&app_config, &lcd, &ui));
    ESP_ERROR_CHECK(gsp_app_start(ui));
    ESP_ERROR_CHECK(iris_gsp_debug_register(ui));

    /* Start ESP-Iris and expose the enter-Recovery RPC. */
    iris_ota_support_start();
    ESP_LOGI(TAG, "GSP Air Battle ready at %dx%d RGB565",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
}
