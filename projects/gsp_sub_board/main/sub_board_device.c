// SPDX-License-Identifier: Apache-2.0
#include "sub_board_backend.h"
#include "hot_plug_register.h"
#include "sub_board_pixels.h"
#include "mosaico_camera.h"
#include "bsp/subboard.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <linux/videodev2.h>

static atomic_uint requested, presence;
static atomic_int failure;
static esp_gsp_handle_t device_ui;
static mosaico_camera_handle_t camera;
static led_strip_handle_t strips[2];
static const char *TAG="sub_board";
static void stop_camera(void)
{
    if (camera) { mosaico_camera_del(camera); camera=NULL; }
}
static void stop_strip(unsigned side)
{
    if (strips[side]) {
        led_strip_clear(strips[side]);
        led_strip_del(strips[side]);
        strips[side]=NULL;
    }
}
static esp_err_t start_strip(unsigned side)
{
    led_strip_config_t cfg={
        .strip_gpio_num=side ? CONFIG_SUB_BOARD_DIN_RIGHT_GPIO : CONFIG_SUB_BOARD_DIN_LEFT_GPIO,
        .max_leds=64, .led_model=LED_MODEL_WS2812,
        .color_component_format=LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt={.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=10000000};
    return led_strip_new_rmt_device(&cfg,&rmt,&strips[side]);
}
static esp_err_t capture(void)
{
    mosaico_camera_frame_t f;
    esp_err_t err=mosaico_camera_get_frame(camera,&f);
    if (err!=ESP_OK) return err;
    size_t stride=f.bytes_per_line?f.bytes_per_line:f.width*2;
    uint16_t *out=NULL;
    if (f.pixel_format!=V4L2_PIX_FMT_UYVY || f.width<480 || f.height<480 ||
        stride<f.width*2 || f.size<stride*f.height) {
        err=ESP_ERR_INVALID_SIZE;
        goto done;
    }
    out=heap_caps_malloc(480*480*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!out) { err=ESP_ERR_NO_MEM; goto done; }
    if (!sb_crop_uyvy(f.data,f.size,f.width,f.height,stride,out)) err=ESP_ERR_INVALID_SIZE;

done:
    { esp_err_t returned=mosaico_camera_return_frame(camera,&f);
      if (err==ESP_OK) err=returned; }
    if (out) {
        if (err==ESP_OK && atomic_load(&requested)==SB_CAMERA) sb_publish(device_ui,out);
        else free(out);
    }
    return err;
}
static void worker(void *ctx)
{
    (void)ctx;
    unsigned last_mode=SB_HOME, phase=0;
    uint32_t generations[2]={0};
    for (;;) {
        unsigned present=0;
        for (unsigned side=0;side<2;side++) {
            hot_plug_kind_t kind=HOT_PLUG_NONE; uint32_t generation=0;
            if (hot_plug_register_get_slot(side,&kind,&generation)==ESP_OK) {
                if (kind==HOT_PLUG_CAMERA) present|=1;
                if (kind==HOT_PLUG_MATRIX) present|=2U<<side;
                if (generation!=generations[side]) stop_strip(side);
                generations[side]=generation;
            }
        }
        atomic_store(&presence,present);
        unsigned mode=atomic_load(&requested);
        if (mode!=last_mode) { phase=0; last_mode=mode; }
        esp_err_t err=ESP_OK;
        if (mode!=SB_CAMERA || !(present&1)) stop_camera();
        for (unsigned side=0;side<2;side++) {
            if (mode!=SB_LIGHTS || !(present&(2U<<side))) stop_strip(side);
            else {
                if (!strips[side]) err=start_strip(side);
                if (strips[side] && err==ESP_OK) {
                    for (unsigned i=0;i<64 && err==ESP_OK;i++) {
                        uint8_t rgb[3]; sb_color(phase,i,rgb);
                        /* Canonical logical grid -> rotated right hardware. */
                        err=led_strip_set_pixel(strips[side],sb_matrix_index(side,i),rgb[0],rgb[1],rgb[2]);
                    }
                    if (err==ESP_OK) err=led_strip_refresh(strips[side]);
                }
            }
            if (err!=ESP_OK) break;
        }
        if (mode==SB_CAMERA && (present&1) && err==ESP_OK) {
            if (!camera) {
                mosaico_camera_config_t cfg=MOSAICO_CAMERA_DEFAULT_CONFIG();
                cfg.slot=MOSAICO_MODULE_MGR_SLOT_LEFT;
                cfg.frame_timeout_ms=200;
                err=mosaico_camera_new(&cfg,&camera);
            }
            if (err==ESP_OK) err=capture();
        }
        if (err!=ESP_OK) {
            ESP_LOGE(TAG,"Demo failed: %s",esp_err_to_name(err));
            atomic_store(&requested,SB_HOME);
            stop_camera(); stop_strip(0); stop_strip(1);
            atomic_store(&failure,1);
        }
        phase++;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
esp_err_t sb_backend_start(esp_gsp_handle_t ui)
{
    esp_err_t err=hot_plug_register_init();
    if (err!=ESP_OK) return err;
    device_ui=ui;
    return xTaskCreate(worker,"sub_board",6144,NULL,3,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
unsigned sb_backend_present(void) { return atomic_load(&presence); }
void sb_backend_request(unsigned mode) { atomic_store(&requested,mode); }
int sb_backend_error(void) { return atomic_exchange(&failure,0); }
void sb_backend_tick(void) {}
