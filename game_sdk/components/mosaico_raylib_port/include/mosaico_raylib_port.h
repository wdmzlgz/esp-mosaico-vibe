// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_gsp.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mosaico_raylib_port_init(esp_gsp_handle_t gsp, uint16_t canvas_bind);
void mosaico_raylib_port_deinit(void);
esp_err_t mosaico_raylib_port_copy_latest(uint16_t *out_pixels,
                                          size_t pixel_capacity);
esp_err_t mosaico_raylib_port_begin_frame(uint16_t **out_pixels,
                                          size_t *out_stride_pixels);
esp_err_t mosaico_raylib_port_present_frame(void);
void mosaico_raylib_port_display_flush(const uint16_t *pixels, uint16_t x,
                                       uint16_t y, uint16_t width,
                                       uint16_t height);
void mosaico_raylib_port_get_dimensions(uint16_t *width, uint16_t *height);

#ifdef __cplusplus
}
#endif
