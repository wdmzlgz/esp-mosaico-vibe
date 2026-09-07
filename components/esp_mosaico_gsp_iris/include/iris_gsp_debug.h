#pragma once

#include "esp_err.h"
#include "esp_gsp.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Intercept panel blits so Iris can screenshot a GSP framebuffer, and
 * register pointer RPC 0x1001/1 so the workbench can inject touches.
 * Wrap the panel before esp_gsp_esp_lcd_start(); register after the UI
 * handle exists and before iris_ota_support_start().
 */
esp_err_t iris_gsp_debug_wrap_panel(esp_lcd_panel_handle_t real,
                                    uint16_t width, uint16_t height,
                                    esp_lcd_panel_handle_t *out_panel);
esp_err_t iris_gsp_debug_register(esp_gsp_handle_t ui);

#ifdef __cplusplus
}
#endif
