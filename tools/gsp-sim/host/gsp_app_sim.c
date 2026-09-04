/*
 * Thin host for a vibe GSP application: create an ESP-GSP simulator
 * session, then run the project's portable C (`gsp_app_start`) against
 * the live handle. Emscripten builds this into a browser binary.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "esp_gsp.h"
#include "gsp/gsp_types.h"
#include "gsp/sim/esp_gsp_simulator.h"

esp_err_t gsp_app_start(esp_gsp_handle_t ui);

#ifndef GSP_APP_BUNDLE_PATH
#define GSP_APP_BUNDLE_PATH "/scene/preview.gspb"
#endif

static esp_gsp_sim_session_t *s_session;
static esp_gsp_sim_window_t *s_window;
static uint8_t *s_bundle;
static size_t s_bundle_size;

#ifdef __EMSCRIPTEN__
static int64_t monotonic_us(void)
{
    return (int64_t)(emscripten_get_now() * 1000.0);
}
#else
static int64_t monotonic_us(void)
{
    struct timespec value = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &value);
    return (int64_t)value.tv_sec * 1000000LL + value.tv_nsec / 1000LL;
}
#endif

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        return NULL;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    long size = ftell(stream);
    if (size <= 0) {
        fclose(stream);
        return NULL;
    }
    if (fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    void *aligned = NULL;
    if (posix_memalign(&aligned, 64, (size_t)size) != 0) {
        fclose(stream);
        return NULL;
    }
    uint8_t *data = aligned;
    if (fread(data, 1, (size_t)size, stream) != (size_t)size) {
        free(data);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *out_size = (size_t)size;
    return data;
}

#ifdef __EMSCRIPTEN__
static EM_BOOL web_pointer_callback(int event_type,
    const EmscriptenMouseEvent *event, void *user_data)
{
    (void)user_data;
    if (s_session == NULL || event == NULL) {
        return EM_FALSE;
    }
    bool pressed = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN ||
                   (event_type == EMSCRIPTEN_EVENT_MOUSEMOVE &&
                    (event->buttons & 1U) != 0);
    double css_width = 0.0;
    double css_height = 0.0;
    int backing_width = 0;
    int backing_height = 0;
    int32_t x = event->targetX;
    int32_t y = event->targetY;
    if (emscripten_get_element_css_size(
            "#canvas", &css_width, &css_height) == EMSCRIPTEN_RESULT_SUCCESS &&
        emscripten_get_canvas_element_size(
            "#canvas", &backing_width, &backing_height) ==
            EMSCRIPTEN_RESULT_SUCCESS &&
        css_width > 0.0 && css_height > 0.0) {
        x = (int32_t)((double)event->targetX * backing_width / css_width);
        y = (int32_t)((double)event->targetY * backing_height / css_height);
    }
    esp_gsp_sim_session_feed_pointer(s_session, x, y, pressed);
    return EM_TRUE;
}

static void register_web_pointer_input(void)
{
    (void)emscripten_set_mousedown_callback("#canvas", NULL, false,
        web_pointer_callback);
    (void)emscripten_set_mouseup_callback("#canvas", NULL, false,
        web_pointer_callback);
    (void)emscripten_set_mousemove_callback("#canvas", NULL, false,
        web_pointer_callback);
}
#endif

static int step_frame(int64_t now_us)
{
    gsp_err_t tick = esp_gsp_sim_session_tick(s_session, now_us);
    if (tick != GSP_OK && tick != GSP_ERR_INVALID_STATE) {
        return 1;
    }
    if (!esp_gsp_sim_session_active(s_session)) {
        bool produced = false;
        if (esp_gsp_sim_session_render(s_session, &produced) != GSP_OK) {
            return 1;
        }
    }
    const esp_gsp_sim_surface_t *surface = esp_gsp_sim_session_surface(s_session);
    if (surface == NULL || s_window == NULL) {
        return 1;
    }
    return esp_gsp_sim_window_present(s_window, surface) == GSP_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *bundle_path = GSP_APP_BUNDLE_PATH;
    unsigned fps = 60;
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        bundle_path = argv[1];
    }

    s_bundle = read_file(bundle_path, &s_bundle_size);
    if (s_bundle == NULL) {
        fprintf(stderr, "gsp_app_sim: cannot read %s\n", bundle_path);
        return 1;
    }

    esp_gsp_sim_session_config_t config = ESP_GSP_SIM_SESSION_CONFIG_INIT();
    config.bundle = s_bundle;
    config.bundle_size = s_bundle_size;
    config.idle_poll_ms = 16;
    gsp_err_t created = esp_gsp_sim_session_create(&config, &s_session);
    if (created != GSP_OK) {
        fprintf(stderr, "gsp_app_sim: session_create failed (%d)\n",
            (int)created);
        free(s_bundle);
        return 1;
    }

    esp_gsp_handle_t ui = esp_gsp_sim_session_handle(s_session);
    if (ui == NULL || gsp_app_start(ui) != ESP_OK) {
        fprintf(stderr, "gsp_app_sim: gsp_app_start failed\n");
        esp_gsp_sim_session_destroy(s_session);
        free(s_bundle);
        return 1;
    }

    const esp_gsp_sim_surface_t *surface = esp_gsp_sim_session_surface(s_session);
    if (surface == NULL ||
        esp_gsp_sim_window_create("ESP-GSP app preview",
            surface->width, surface->height, surface->pixel_format,
            &s_window) != GSP_OK) {
        fprintf(stderr, "gsp_app_sim: window_create failed\n");
        esp_gsp_sim_session_destroy(s_session);
        free(s_bundle);
        return 1;
    }

#ifdef __EMSCRIPTEN__
    register_web_pointer_input();
    for (unsigned warm = 0; warm < 8; ++warm) {
        if (step_frame(1000000 + (int64_t)warm * (1000000 / fps)) != 0) {
            return 1;
        }
    }
#endif

    printf("gsp_app_sim: %ux%u C application WASM\n",
        surface != NULL ? surface->width : 0,
        surface != NULL ? surface->height : 0);

    int64_t now_us = 1000000;
    for (;;) {
        now_us += (int64_t)(1000000U / fps);
        if (now_us < monotonic_us()) {
            now_us = monotonic_us();
        }
        if (step_frame(now_us) != 0) {
            fprintf(stderr, "gsp_app_sim: frame failed\n");
            return 1;
        }
#ifdef __EMSCRIPTEN__
        emscripten_sleep((int)(1000U / fps));
#else
        (void)fps;
        break;
#endif
    }

#ifndef __EMSCRIPTEN__
    esp_gsp_sim_window_destroy(s_window);
    esp_gsp_sim_session_destroy(s_session);
    free(s_bundle);
#endif
    return 0;
}
