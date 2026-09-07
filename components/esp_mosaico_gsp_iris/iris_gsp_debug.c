// SPDX-License-Identifier: Apache-2.0

#include "iris_gsp_debug.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_compiler.h"
#include "esp_check.h"
#include "esp_gsp_debug.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define POINTER_SERVICE_ID   0x1001U
#define POINTER_METHOD_ID    1U
#define POINTER_MESSAGE_SIZE 12U

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_handle_t real;
} iris_gsp_panel_shim_t;

typedef struct {
    SemaphoreHandle_t lock;
    uint8_t *live;
    uint8_t *snapshot;
    uint32_t total_size;
    uint16_t width;
    uint16_t height;
    uint32_t stride;
    bool snapshot_ready;
    iris_gsp_panel_shim_t *shim;
    esp_gsp_handle_t ui;
} iris_gsp_debug_t;

static const char *TAG = "iris_gsp";
static iris_gsp_debug_t s_debug;

static iris_gsp_panel_shim_t *shim_from_panel(esp_lcd_panel_t *panel)
{
    return __containerof(panel, iris_gsp_panel_shim_t, base);
}

static void copy_rgb565_swapped(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
        dst[offset] = src[offset + 1];
        dst[offset + 1] = src[offset];
    }
}

static void blit_live(int x_start, int y_start, int x_end, int y_end,
                      const uint8_t *color, size_t src_stride)
{
    if (s_debug.live == NULL || color == NULL || src_stride == 0) {
        return;
    }
    if (x_start < 0) {
        x_start = 0;
    }
    if (y_start < 0) {
        y_start = 0;
    }
    if (x_end > s_debug.width) {
        x_end = s_debug.width;
    }
    if (y_end > s_debug.height) {
        y_end = s_debug.height;
    }
    if (x_start >= x_end || y_start >= y_end) {
        return;
    }

    const size_t row_bytes = (size_t)(x_end - x_start) * 2U;
    if (xSemaphoreTake(s_debug.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    for (int y = y_start; y < y_end; ++y) {
        uint8_t *dst = s_debug.live +
                       (size_t)y * s_debug.stride + (size_t)x_start * 2U;
        const uint8_t *src = color + (size_t)(y - y_start) * src_stride;
        copy_rgb565_swapped(dst, src, row_bytes);
    }
    xSemaphoreGive(s_debug.lock);
}

static esp_err_t shim_reset(esp_lcd_panel_t *panel)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->reset != NULL ? shim->real->reset(shim->real)
                                     : ESP_OK;
}

static esp_err_t shim_init(esp_lcd_panel_t *panel)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->init != NULL ? shim->real->init(shim->real) : ESP_OK;
}

static esp_err_t shim_del(esp_lcd_panel_t *panel)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    esp_err_t err = ESP_OK;
    if (shim->real->del != NULL) {
        err = shim->real->del(shim->real);
    }
    return err;
}

static esp_err_t shim_draw_bitmap(esp_lcd_panel_t *panel, int x_start,
                                  int y_start, int x_end, int y_end,
                                  const void *color_data)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    const size_t src_stride = (size_t)(x_end - x_start) * 2U;
    blit_live(x_start, y_start, x_end, y_end, color_data, src_stride);
    return shim->real->draw_bitmap(shim->real, x_start, y_start, x_end,
                                   y_end, color_data);
}

static esp_err_t shim_draw_bitmap_2d(esp_lcd_panel_t *panel, int x_start,
                                     int y_start, int x_end, int y_end,
                                     const void *src_data, size_t src_x_size,
                                     size_t src_y_size, int src_x_start,
                                     int src_y_start, int src_x_end,
                                     int src_y_end)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    (void)src_y_size;
    (void)src_x_end;
    (void)src_y_end;
    if (src_data != NULL && src_x_size > 0) {
        const uint8_t *src = (const uint8_t *)src_data +
                             (size_t)src_y_start * src_x_size * 2U +
                             (size_t)src_x_start * 2U;
        blit_live(x_start, y_start, x_end, y_end, src, src_x_size * 2U);
    }
    if (shim->real->draw_bitmap_2d == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return shim->real->draw_bitmap_2d(shim->real, x_start, y_start, x_end,
                                      y_end, src_data, src_x_size, src_y_size,
                                      src_x_start, src_y_start, src_x_end,
                                      src_y_end);
}

static esp_err_t shim_mirror(esp_lcd_panel_t *panel, bool x_axis, bool y_axis)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->mirror != NULL
               ? shim->real->mirror(shim->real, x_axis, y_axis)
               : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t shim_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->swap_xy != NULL
               ? shim->real->swap_xy(shim->real, swap_axes)
               : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t shim_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->set_gap != NULL
               ? shim->real->set_gap(shim->real, x_gap, y_gap)
               : ESP_OK;
}

static esp_err_t shim_invert_color(esp_lcd_panel_t *panel, bool invert)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->invert_color != NULL
               ? shim->real->invert_color(shim->real, invert)
               : ESP_OK;
}

static esp_err_t shim_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->disp_on_off != NULL
               ? shim->real->disp_on_off(shim->real, on_off)
               : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t shim_disp_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->disp_sleep != NULL
               ? shim->real->disp_sleep(shim->real, sleep)
               : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t shim_set_brightness(esp_lcd_panel_t *panel, int brightness)
{
    iris_gsp_panel_shim_t *shim = shim_from_panel(panel);
    return shim->real->set_brightness != NULL
               ? shim->real->set_brightness(shim->real, brightness)
               : ESP_ERR_NOT_SUPPORTED;
}

static void release_snapshot(void)
{
    heap_caps_free(s_debug.snapshot);
    s_debug.snapshot = NULL;
    s_debug.snapshot_ready = false;
}

static esp_err_t snapshot_live(void)
{
    if (s_debug.live == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_debug.snapshot == NULL) {
        s_debug.snapshot = heap_caps_malloc(
            s_debug.total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_debug.snapshot == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_debug.lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(s_debug.snapshot, s_debug.live, s_debug.total_size);
    s_debug.snapshot_ready = true;
    xSemaphoreGive(s_debug.lock);
    return ESP_OK;
}

static esp_err_t screen_begin(const esp_iris_media_desc_t *requested,
                              esp_iris_media_desc_t *actual,
                              uint32_t *total_size, void *user_ctx)
{
    (void)requested;
    (void)user_ctx;
    if (actual == NULL || total_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_debug.snapshot != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = snapshot_live();
    if (err != ESP_OK) {
        release_snapshot();
        return err;
    }
    *actual = (esp_iris_media_desc_t) {
        .x = 0,
        .y = 0,
        .width = s_debug.width,
        .height = s_debug.height,
        .stride = s_debug.stride,
        .format = ESP_IRIS_PIXEL_FORMAT_RGB565,
        .quality = 0,
    };
    *total_size = s_debug.total_size;
    return ESP_OK;
}

static esp_err_t screen_read(uint32_t offset, uint8_t *out, size_t capacity,
                             size_t *out_size, void *user_ctx)
{
    (void)user_ctx;
    if (s_debug.snapshot == NULL || out == NULL || out_size == NULL ||
        capacity == 0 || offset >= s_debug.total_size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (offset == 0 && !s_debug.snapshot_ready) {
        esp_err_t err = snapshot_live();
        if (err != ESP_OK) {
            return err;
        }
    } else if (!s_debug.snapshot_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t size = s_debug.total_size - offset;
    if (size > capacity) {
        size = capacity;
    }
    memcpy(out, s_debug.snapshot + offset, size);
    *out_size = size;
    if (offset + size == s_debug.total_size) {
        s_debug.snapshot_ready = false;
    }
    return ESP_OK;
}

static void screen_end(void *user_ctx)
{
    (void)user_ctx;
    release_snapshot();
}

static int16_t clamp_coord(int16_t value, int16_t max)
{
    if (value < 0) {
        return 0;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static int16_t get_le16(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static esp_err_t pointer_rpc(const esp_iris_rpc_request_t *request,
                             uint8_t *response, size_t response_capacity,
                             size_t *response_size, void *user_ctx)
{
    (void)user_ctx;
    if (request->payload_size != POINTER_MESSAGE_SIZE ||
        response_capacity < POINTER_MESSAGE_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_debug.ui == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(response, request->payload, POINTER_MESSAGE_SIZE);
    const uint8_t phase = response[0];
    int16_t x = clamp_coord(get_le16(response + 2),
                            (int16_t)(s_debug.width - 1));
    int16_t y = clamp_coord(get_le16(response + 4),
                            (int16_t)(s_debug.height - 1));
    put_le16(response + 2, (uint16_t)x);
    put_le16(response + 4, (uint16_t)y);

    const bool pressed = phase != 2U;
    if (esp_gsp_inject_touch(s_debug.ui, x, y, pressed) != ESP_OK) {
        return ESP_FAIL;
    }
    *response_size = POINTER_MESSAGE_SIZE;
    return ESP_OK;
}

esp_err_t iris_gsp_debug_wrap_panel(esp_lcd_panel_handle_t real,
                                    uint16_t width, uint16_t height,
                                    esp_lcd_panel_handle_t *out_panel)
{
    ESP_RETURN_ON_FALSE(real != NULL && out_panel != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "panel wrap arguments");
    ESP_RETURN_ON_FALSE(width > 0 && height > 0, ESP_ERR_INVALID_ARG, TAG,
                        "panel size");
    ESP_RETURN_ON_FALSE(s_debug.shim == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "panel already wrapped");

    const uint32_t stride = (uint32_t)width * 2U;
    const uint32_t total_size = stride * height;
    uint8_t *live = heap_caps_calloc(
        1, total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(live != NULL, ESP_ERR_NO_MEM, TAG, "live framebuffer");

    iris_gsp_panel_shim_t *shim = calloc(1, sizeof(*shim));
    if (shim == NULL) {
        heap_caps_free(live);
        return ESP_ERR_NO_MEM;
    }

    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    if (lock == NULL) {
        heap_caps_free(live);
        free(shim);
        return ESP_ERR_NO_MEM;
    }

    shim->real = real;
    shim->base.reset = shim_reset;
    shim->base.init = shim_init;
    shim->base.del = shim_del;
    shim->base.draw_bitmap = shim_draw_bitmap;
    shim->base.draw_bitmap_2d =
        real->draw_bitmap_2d != NULL ? shim_draw_bitmap_2d : NULL;
    shim->base.mirror = shim_mirror;
    shim->base.swap_xy = shim_swap_xy;
    shim->base.set_gap = shim_set_gap;
    shim->base.invert_color = shim_invert_color;
    shim->base.disp_on_off = shim_disp_on_off;
    shim->base.disp_sleep = shim_disp_sleep;
    shim->base.set_brightness = shim_set_brightness;
    shim->base.user_data = real->user_data;

    s_debug.lock = lock;
    s_debug.live = live;
    s_debug.total_size = total_size;
    s_debug.width = width;
    s_debug.height = height;
    s_debug.stride = stride;
    s_debug.shim = shim;
    *out_panel = &shim->base;
    ESP_LOGI(TAG, "Wrapped %ux%u RGB565 panel for Iris mirror", width, height);
    return ESP_OK;
}

esp_err_t iris_gsp_debug_register(esp_gsp_handle_t ui)
{
    ESP_RETURN_ON_FALSE(ui != NULL, ESP_ERR_INVALID_ARG, TAG, "ui handle");
    ESP_RETURN_ON_FALSE(s_debug.live != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "panel was not wrapped");

    s_debug.ui = ui;
    const esp_iris_screen_backend_t backend = {
        .begin = screen_begin,
        .read = screen_read,
        .end = screen_end,
        .user_ctx = &s_debug,
    };
    ESP_RETURN_ON_ERROR(esp_iris_screen_register(&backend), TAG,
                        "register screen");
    ESP_RETURN_ON_ERROR(esp_iris_rpc_register(POINTER_SERVICE_ID,
                                              POINTER_METHOD_ID, pointer_rpc,
                                              NULL),
                        TAG, "register pointer RPC");
    ESP_LOGI(TAG, "Registered %ux%u RGB565 screen and pointer RPC 0x1001/1",
             s_debug.width, s_debug.height);
    return ESP_OK;
}
