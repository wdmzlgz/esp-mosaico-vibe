// SPDX-License-Identifier: Apache-2.0
#include "mosaico_raylib_port.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mosaico_game.h"

#define FRAME_BYTES ((size_t)MOSAICO_GAME_WIDTH * MOSAICO_GAME_HEIGHT * 2U)
#define FRAME_COUNT CONFIG_MOSAICO_GAME_FRAMEBUFFER_COUNT

typedef struct {
    uint16_t *pixels;
    atomic_bool borrowed;
} frame_slot_t;

static const char *TAG = "mosaico_raylib";
static esp_gsp_handle_t s_gsp;
static uint16_t s_bind;
static frame_slot_t s_frames[FRAME_COUNT];
static atomic_uint s_next;
static uint16_t *s_latest;
static SemaphoreHandle_t s_latest_mutex;
static frame_slot_t *s_drawing_slot;

static void release_frame(void *ctx)
{
    frame_slot_t *slot = ctx;
    atomic_store_explicit(&slot->borrowed, false, memory_order_release);
}

esp_err_t mosaico_raylib_port_init(esp_gsp_handle_t gsp, uint16_t canvas_bind)
{
    if (!gsp) return ESP_ERR_INVALID_ARG;
    if (s_gsp) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < FRAME_COUNT; ++i) {
        s_frames[i].pixels = heap_caps_malloc(
            FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frames[i].pixels) {
            mosaico_raylib_port_deinit();
            ESP_LOGE(TAG, "PSRAM framebuffer allocation failed (%u bytes)",
                     (unsigned)FRAME_BYTES);
            return ESP_ERR_NO_MEM;
        }
        memset(s_frames[i].pixels, 0, FRAME_BYTES);
        atomic_init(&s_frames[i].borrowed, false);
    }
    atomic_init(&s_next, 0);
    /* The latest screenshot aliases one of the retained GSP frame slots.
     * Keeping a third full-screen copy cost 450 KiB and one PSRAM memcpy on
     * every frame. Access and slot rewrites are serialized by the mutex. */
    s_latest = s_frames[0].pixels;
    s_latest_mutex = xSemaphoreCreateMutex();
    if (!s_latest_mutex) {
        mosaico_raylib_port_deinit();
        return ESP_ERR_NO_MEM;
    }
    s_bind = canvas_bind;
    s_gsp = gsp;
    return ESP_OK;
}

void mosaico_raylib_port_deinit(void)
{
    if (s_gsp) {
        (void)esp_gsp_canvas_stop(s_gsp, s_bind);
        (void)esp_gsp_flush(s_gsp, 1000);
    }
    for (size_t i = 0; i < FRAME_COUNT; ++i) {
        free(s_frames[i].pixels);
        s_frames[i].pixels = NULL;
        atomic_store_explicit(&s_frames[i].borrowed, false,
                              memory_order_release);
    }
    s_latest = NULL;
    if (s_latest_mutex) {
        vSemaphoreDelete(s_latest_mutex);
        s_latest_mutex = NULL;
    }
    s_gsp = NULL;
    s_drawing_slot = NULL;
}

esp_err_t mosaico_raylib_port_begin_frame(uint16_t **out_pixels,
                                          size_t *out_stride_pixels)
{
    if (!out_pixels || !out_stride_pixels) return ESP_ERR_INVALID_ARG;
    *out_pixels = NULL;
    *out_stride_pixels = 0;
    if (!s_gsp || !s_latest_mutex || s_drawing_slot) {
        return ESP_ERR_INVALID_STATE;
    }

    unsigned first = atomic_fetch_add(&s_next, 1U) % FRAME_COUNT;
    for (unsigned attempt = 0; attempt < FRAME_COUNT; ++attempt) {
        frame_slot_t *slot = &s_frames[(first + attempt) % FRAME_COUNT];
        bool expected = false;
        if (!atomic_compare_exchange_strong(&slot->borrowed, &expected, true)) {
            continue;
        }
        if (xSemaphoreTake(s_latest_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            atomic_store(&slot->borrowed, false);
            return ESP_ERR_TIMEOUT;
        }
        s_drawing_slot = slot;
        *out_pixels = slot->pixels;
        *out_stride_pixels = MOSAICO_GAME_WIDTH;
        return ESP_OK;
    }
    MosaicoGameRecordFrame(0, 0, 0, true);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mosaico_raylib_port_present_frame(void)
{
    frame_slot_t *slot = s_drawing_slot;
    if (!slot || !s_latest_mutex) return ESP_ERR_INVALID_STATE;

    s_latest = slot->pixels;
    s_drawing_slot = NULL;
    xSemaphoreGive(s_latest_mutex);

    int64_t started = esp_timer_get_time();
    esp_err_t err = esp_gsp_canvas_try_push(
        s_gsp, s_bind, slot->pixels,
        MOSAICO_GAME_WIDTH * sizeof(uint16_t), release_frame, slot);
    bool dropped = err != ESP_OK;
    if (dropped) atomic_store(&slot->borrowed, false);
    MosaicoGameRecordFrame(0, 0,
        (uint32_t)(esp_timer_get_time() - started), dropped);
    return err;
}

esp_err_t mosaico_raylib_port_copy_latest(uint16_t *out_pixels,
                                          size_t pixel_capacity)
{
    if (!out_pixels || pixel_capacity < FRAME_BYTES / sizeof(uint16_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_latest || !s_latest_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_latest_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(out_pixels, s_latest, FRAME_BYTES);
    xSemaphoreGive(s_latest_mutex);
    return ESP_OK;
}

void mosaico_raylib_port_display_flush(const uint16_t *pixels, uint16_t x,
                                       uint16_t y, uint16_t width,
                                       uint16_t height)
{
    int64_t started = esp_timer_get_time();
    bool dropped = true;
    if (s_gsp && pixels && x == 0 && y == 0 &&
            width == MOSAICO_GAME_WIDTH && height == MOSAICO_GAME_HEIGHT) {
        unsigned first = atomic_fetch_add(&s_next, 1U) % FRAME_COUNT;
        for (unsigned attempt = 0; attempt < FRAME_COUNT; ++attempt) {
            frame_slot_t *slot = &s_frames[(first + attempt) % FRAME_COUNT];
            bool expected = false;
            if (!atomic_compare_exchange_strong(&slot->borrowed, &expected, true)) {
                continue;
            }
            if (!s_latest_mutex ||
                    xSemaphoreTake(s_latest_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
                atomic_store(&slot->borrowed, false);
                break;
            }
            memcpy(slot->pixels, pixels, FRAME_BYTES);
            s_latest = slot->pixels;
            xSemaphoreGive(s_latest_mutex);
            esp_err_t err = esp_gsp_canvas_try_push(
                s_gsp, s_bind, slot->pixels, width * sizeof(uint16_t),
                release_frame, slot);
            if (err == ESP_OK) {
                dropped = false;
            } else {
                atomic_store(&slot->borrowed, false);
            }
            break;
        }
    }
    uint32_t present_us = (uint32_t)(esp_timer_get_time() - started);
    MosaicoGameRecordFrame(0, 0, present_us, dropped);
}

void mosaico_raylib_port_get_dimensions(uint16_t *width, uint16_t *height)
{
    if (width) *width = MOSAICO_GAME_WIDTH;
    if (height) *height = MOSAICO_GAME_HEIGHT;
}
