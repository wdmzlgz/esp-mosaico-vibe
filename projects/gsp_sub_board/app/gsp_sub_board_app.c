// SPDX-License-Identifier: Apache-2.0
#include "gsp_sub_board_app.h"
#include "sub_board_backend.h"
#include "driver/gpio.h"
#if !defined(ESP_PLATFORM)
#include "sub_board_pixels.h"
#include <emscripten.h>
#endif
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned mode, previous_presence, ticks, notice_until;
static int previous_key = 1;
static void release_frame(void *ctx) { free(ctx); }
void sb_publish(esp_gsp_handle_t ui, uint16_t *pixels)
{
    if (esp_gsp_canvas_push(ui, SB_PREVIEW_BIND, pixels, 960, release_frame, pixels) != ESP_OK) free(pixels);
    else esp_gsp_set_visible(ui, SB_PREVIEW_BOX_BIND, true);
}
static void notice(esp_gsp_handle_t ui, const char *text)
{
    esp_gsp_set_text(ui, SB_NOTICE_BIND, text);
    esp_gsp_set_visible(ui, SB_NOTICE_BOX_BIND, true);
    notice_until = ticks + 100;
}
static void set_mode(esp_gsp_handle_t ui, unsigned next)
{
    mode = next;
    if (next == SB_CAMERA) esp_gsp_set_visible(ui, SB_PREVIEW_BOX_BIND, false);
    sb_backend_request(next);
    esp_gsp_set_visible(ui, SB_HOME_BIND, next != SB_CAMERA);
    if (next == SB_CAMERA) notice(ui, "Starting camera...");
    else if (next == SB_LIGHTS) notice(ui, "Breathing lights - top-right key to exit");
    else notice(ui, "Demo stopped");
}
static void event(esp_gsp_handle_t ui, const esp_gsp_event_t *e, void *ctx)
{
    (void)ctx;
    if (!e || e->type != ESP_GSP_EVENT_CALL || ticks < 3) return;
    unsigned present = sb_backend_present();
    if (e->action_id == 0 && (present & 1)) set_mode(ui, SB_CAMERA);
    if ((e->action_id == 1 && (present & 2)) || (e->action_id == 2 && (present & 4))) set_mode(ui, SB_LIGHTS);
}
static void tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ctx;
    ticks++;
    sb_backend_tick();
    unsigned present = sb_backend_present();
    int key = gpio_get_level(GPIO_NUM_7);
    if (!key && previous_key) set_mode(ui, SB_HOME);
    previous_key = key;
    if ((mode == SB_CAMERA && !(present & 1)) || (mode == SB_LIGHTS && !(present & 6))) set_mode(ui, SB_HOME);
    if (present != previous_presence) {
        char text[100];
        snprintf(text, sizeof(text), "Left: %s | Right: %s",
                 present & 1 ? "camera inserted" : present & 2 ? "lights inserted" : "empty",
                 present & 4 ? "lights inserted" : "empty");
        notice(ui, text);
        previous_presence = present;
    }
    esp_gsp_set_visible(ui, SB_CAMERA_BIND, (present & 1) != 0);
    esp_gsp_set_visible(ui, SB_LEFT_BIND, (present & 2) != 0);
    esp_gsp_set_visible(ui, SB_RIGHT_BIND, (present & 4) != 0);
    if (sb_backend_error()) {
        set_mode(ui, SB_HOME);
        notice(ui, "Device unavailable - check connection/permission");
    }
    if (ticks == notice_until) esp_gsp_set_visible(ui, SB_NOTICE_BOX_BIND, false);
}
esp_err_t gsp_app_start(esp_gsp_handle_t ui)
{
    gpio_config_t key = { .pin_bit_mask = 1ULL << GPIO_NUM_7,
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    esp_err_t err = gpio_config(&key);
    if (err != ESP_OK) return err;
    err = sb_backend_start(ui);
    if (err != ESP_OK) return err;
    esp_gsp_set_visible(ui, SB_CAMERA_BIND, false);
    esp_gsp_set_visible(ui, SB_LEFT_BIND, false);
    esp_gsp_set_visible(ui, SB_RIGHT_BIND, false);
    if (esp_gsp_on_event(ui, event, NULL) != ESP_OK) return ESP_FAIL;
    return esp_gsp_timer_create(ui, 30, tick, NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

#if !defined(ESP_PLATFORM)
static esp_gsp_handle_t host_ui;
static unsigned host_mode, phase;

EM_JS(void, host_init, (), {
    window.SubBoardCamera = { generation: 0, video: null, stream: null, error: 0,
      canvas: document.createElement('canvas') };
    SubBoardCamera.canvas.width = SubBoardCamera.canvas.height = 480;
    SubBoardCamera.ctx = SubBoardCamera.canvas.getContext('2d', {willReadFrequently:true});
});

EM_JS(int, host_presence, (), {
    const s = window.MosaicoSideboards?.getState() || {};
    return (s.left === 'camera' ? 1 : s.left === 'ws2812' ? 2 : 0) |
      (s.right === 'ws2812' ? 4 : 0);
});

EM_JS(void, host_request, (int mode), {
    const s = SubBoardCamera;
    const generation = ++s.generation;
    if (s.stream) s.stream.getTracks().forEach(t => t.stop());
    if (s.video) { s.video.pause(); s.video.srcObject = null; }
    s.stream = s.video = null;
    s.error = 0;
    window.MosaicoSideboards?.clearWs2812('left');
    window.MosaicoSideboards?.clearWs2812('right');
    if (mode !== 1) return;
    if (!navigator.mediaDevices?.getUserMedia) { s.error = 1; return; }
    navigator.mediaDevices.getUserMedia({video:true,audio:false}).then(async stream => {
      if (generation !== s.generation) {
        stream.getTracks().forEach(t=>t.stop());
        return;
      }
      s.stream = stream;
      const video = document.createElement('video');
      video.muted = true;
      video.playsInline = true;
      video.srcObject = stream;
      s.video = video;
      try {
        await video.play();
      } catch (_) {
        stream.getTracks().forEach(t=>t.stop());
        if (generation === s.generation) s.error = 1;
      }
    }).catch(() => {
      if (generation === s.generation) s.error = 1;
    });
});

EM_JS(int, host_error, (), {
    const error = SubBoardCamera.error;
    SubBoardCamera.error = 0;
    return error;
});

EM_JS(int, host_frame, (uint16_t *out), {
    const s = SubBoardCamera;
    const video = s.video;
    if (!video || video.readyState < 2 || !video.videoWidth) return 0;
    const size = Math.min(480, video.videoWidth, video.videoHeight);
    s.ctx.drawImage(video,
      (video.videoWidth-size)/2, (video.videoHeight-size)/2, size, size,
      0, 0, 480, 480);
    const pixels = s.ctx.getImageData(0,0,480,480).data;
    for (let i=0; i<480*480; i++) {
      HEAPU16[(out>>1)+i] =
        ((pixels[i*4]>>3)<<11) |
        ((pixels[i*4+1]>>2)<<5) |
        (pixels[i*4+2]>>3);
    }
    return 1;
});

EM_JS(void, host_pixel,
      (int side, int index, int r, int g, int b, int input_peak), {
    /* The PCB's electrical order runs down columns. The right board is the
       same PCB mounted 180 degrees, and its physical index was reversed by
       portable C, so undo that mounting rotation when placing browser cells. */
    const nativeRow = index & 7;
    const nativeColumn = index >> 3;
    const visualRow = side ? 7-nativeRow : nativeRow;
    const visualColumn = side ? 7-nativeColumn : nativeColumn;
    /* Preserve hue separately from linear PWM intensity. CSS uses intensity
       as the opacity of an emission layer over the physical diffuser. */
    const peak = Math.max(r, g, b);
    const displayRgb = peak
      ? [r, g, b].map(value => Math.round(value * 255 / peak))
      : [0, 0, 0];
    const displayLevel = peak
      ? 0.72 * Math.pow(Math.max(0, Math.min(1, peak / input_peak)), 0.65)
      : 0;
    window.MosaicoSideboards?.setWs2812Pixel(
      side?'right':'left', visualRow, visualColumn,
      peak ? displayRgb : 'off', displayLevel);
});

esp_err_t sb_backend_start(esp_gsp_handle_t ui)
{
    host_ui = ui;
    host_init();
    return ESP_OK;
}

unsigned sb_backend_present(void)
{
    return host_presence();
}

void sb_backend_request(unsigned next_mode)
{
    host_mode = next_mode;
    phase = 0;
    host_request(next_mode);
}

int sb_backend_error(void)
{
    return host_error();
}

void sb_backend_tick(void)
{
    unsigned present = host_presence();
    if (host_mode == SB_CAMERA && (present & 1)) {
        uint16_t *pixels = malloc(480 * 480 * 2);
        if (pixels) {
            if (host_frame(pixels)) sb_publish(host_ui, pixels);
            else free(pixels);
        }
    }
    for (unsigned side = 0; side < 2; side++) {
        for (unsigned i = 0; i < 64; i++) {
            uint8_t rgb[3] = {0};
            if (host_mode == SB_LIGHTS && (present & (2U << side))) {
                sb_color(phase, i, rgb);
            }
            host_pixel(side, sb_matrix_index(side, i),
                       rgb[0], rgb[1], rgb[2], SB_LED_MAX_BRIGHTNESS);
        }
    }
    phase++;
}
#endif
