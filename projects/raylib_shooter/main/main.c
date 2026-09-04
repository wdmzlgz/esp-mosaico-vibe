// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "bsp/esp_mosaico.h"
#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#include "bundle_gsp.h"
#include "esp_check.h"
#include "esp_display_present_config.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iris_ota_support.h"
#include "mosaico_game.h"
#include "mosaico_game_debug.h"
#include "mosaico_game_input.h"
#include "mosaico_raylib_port.h"
#include "nvs_flash.h"
#include "mosaico_raylib_fast.h"
#include "raylib_screen_mirror.h"
#include "game_audio.h"
#include "shooter_game.h"

static const char *TAG="raylib_shooter";
static shooter_game_t s_game;
static esp_lcd_touch_handle_t s_touch;

static void draw_centered(const char *text,int y,int size,Color color)
{
    int width=MeasureText(text,size);
    DrawText(text,(MOSAICO_GAME_WIDTH-width)/2,y,size,color);
}

static void touch_task(void *ctx)
{
    (void)ctx; bool was_pressed=false;
    while (true) {
        esp_lcd_touch_point_data_t points[1]={0}; uint8_t count=0;
        bool pressed=esp_lcd_touch_read_data(s_touch)==ESP_OK &&
            esp_lcd_touch_get_data(s_touch,points,&count,1)==ESP_OK && count;
        mosaico_device_event_t event={.type=MOSAICO_DEVICE_EVENT_POINTER,
            .x=pressed?points[0].x:0,.y=pressed?points[0].y:0,
            .pressed=pressed,.timestamp_us=esp_timer_get_time()};
        if (pressed||was_pressed) (void)mosaico_game_input_pointer(
            event.x,event.y,event.pressed,event.timestamp_us);
        was_pressed=pressed; vTaskDelay(pdMS_TO_TICKS(12));
    }
}

static void draw_ship(float x,float y)
{
    float pulse=3.0f+(float)(s_game.tick%5U);
    DrawTriangle((Vector2){x+18,y+43+pulse},(Vector2){x+11,y+31},
                 (Vector2){x+25,y+31},(Color){180,70,18,255});
    DrawTriangle((Vector2){x+18,y},(Vector2){x-2,y+35},
                 (Vector2){x+38,y+35},(Color){45,210,255,255});
    DrawTriangle((Vector2){x+18,y+7},(Vector2){x+8,y+32},
                 (Vector2){x+28,y+32},(Color){20,52,104,255});
    DrawRectangle((int)x+13,(int)y+14,10,10,(Color){190,250,255,255});
    DrawRectangleLines((int)x+11,(int)y+12,14,14,(Color){70,150,190,255});
}

static void draw_enemy(const shooter_actor_t *enemy)
{
    static const Color colors[]={{255,118,58,255},{255,38,156,255},{75,235,165,255}};
    Color c=colors[enemy->kind]; float x=enemy->x,y=enemy->y;
    if(enemy->kind==0){
        DrawTriangle((Vector2){x+14,y},(Vector2){x,y+27},(Vector2){x+28,y+27},c);
        DrawRectangle((int)x+8,(int)y+11,12,12,(Color){30,18,40,255});
    }else if(enemy->kind==1){
        DrawRectangle((int)x,(int)y+4,28,22,c);
        DrawTriangle((Vector2){x,y+15},(Vector2){x-7,y+27},(Vector2){x+7,y+24},c);
        DrawTriangle((Vector2){x+28,y+15},(Vector2){x+35,y+27},(Vector2){x+21,y+24},c);
    }else{
        DrawRectangle((int)x+4,(int)y+4,20,20,c);
        DrawRectangle((int)x,(int)y+10,28,8,(Color){30,18,40,255});
        DrawRectangle((int)x+10,(int)y,8,28,(Color){30,18,40,255});
    }
    DrawRectangle((int)x+8,(int)y+13,4,4,RAYWHITE);
    DrawRectangle((int)x+18,(int)y+13,4,4,RAYWHITE);
}

static void draw_background(void)
{
    for(int i=0;i<54;++i){
        int x=(i*83+(int)s_game.tick)%480;
        int y=(i*47+(int)(s_game.tick*(i%3+1)))%480;
        Color c=i%5?(Color){75,125,190,150}:(Color){215,250,255,220};
        if(i%7==0) DrawRectangle(x,y,2,2,c); else DrawPixel(x,y,c);
    }
}

static void draw_hud(void)
{
    DrawRectangleLines(12,12,456,48,(Color){26,100,132,255});
    DrawRectangle(12,12,5,5,(Color){30,210,230,255});
    DrawRectangle(463,55,5,5,(Color){30,210,230,255});
    DrawText(TextFormat("%06lu",(unsigned long)s_game.score),28,20,23,RAYWHITE);
    for(unsigned i=0;i<3;++i){
        Color c=i<s_game.lives?(Color){255,55,150,255}:(Color){55,38,75,255};
        DrawRectangle(399+(int)i*18,29,12,12,c);
    }
}

static void draw_card(const char *title,const char *subtitle,Color accent)
{
    DrawRectangle(34,146,412,200,(Color){7,12,34,255});
    DrawRectangleLines(34,146,412,200,accent);
    DrawRectangle(34,146,18,3,accent); DrawRectangle(34,146,3,18,accent);
    DrawRectangle(428,343,18,3,accent); DrawRectangle(443,328,3,18,accent);
    DrawRectangle(70,166,340,3,accent);
    draw_centered(title,190,38,RAYWHITE);
    draw_centered(subtitle,242,18,(Color){150,187,220,255});
    DrawRectangle(126,286,228,42,accent);
}

static void render_game(void)
{
    BeginDrawing();
    /* The software backend has a dedicated framebuffer-clear fast path.
     * Avoid expressing the full-screen background as two generic quads: on
     * ESP32 those become four rasterized triangles and dominate frame time. */
    ClearBackground((Color){3,7,24,255});
    draw_background();
    if(s_game.phase!=SHOOTER_START) draw_ship(s_game.player.x,s_game.player.y);
    for(size_t i=0;i<SHOOTER_MAX_BULLETS;++i) if(s_game.bullets[i].active)
    { int x=(int)s_game.bullets[i].x,y=(int)s_game.bullets[i].y;
      DrawRectangle(x-2,y,10,12,(Color){12,52,78,255});
      DrawRectangle(x,y,6,12,(Color){180,250,255,255}); }
    for(size_t i=0;i<SHOOTER_MAX_ENEMIES;++i) if(s_game.enemies[i].active)
        draw_enemy(&s_game.enemies[i]);
    draw_hud();
    if(s_game.phase==SHOOTER_START){
        draw_card("MOSAICO STRIKE","NEON DEFENSE // SECTOR 31",(Color){30,210,230,255});
        draw_centered("TOUCH TO LAUNCH",297,18,(Color){2,18,32,255});
        draw_centered("DRAG TO STEER // AUTO FIRE",370,16,(Color){100,145,185,255});
    }else if(s_game.phase==SHOOTER_PAUSED){
        DrawRectangle(0,0,480,480,(Color){2,5,16,255});
        draw_card("PAUSED","COMBAT SYSTEMS STANDBY",(Color){255,188,55,255});
        draw_centered("TOUCH TO RESUME",297,18,(Color){34,21,3,255});
    }else if(s_game.phase==SHOOTER_GAME_OVER){
        DrawRectangle(0,0,480,480,(Color){2,5,16,255});
        draw_card("MISSION LOST",TextFormat("FINAL SCORE  %06lu",(unsigned long)s_game.score),
                  (Color){255,42,128,255});
        draw_centered("TOUCH TO RETRY",297,18,RAYWHITE);
    }
    EndDrawing();
}

static esp_err_t start_gsp(esp_gsp_handle_t *out_gsp)
{
    bsp_display_config_t cfg=BSP_DISPLAY_DEFAULT_CONFIG(); cfg.enable_touch=false;
    esp_lcd_panel_handle_t panel=NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&cfg,&panel),TAG,"display");
    ESP_RETURN_ON_ERROR(bsp_touch_new(BSP_DISPLAY_ROTATE_0,&s_touch),TAG,"touch");
    esp_gsp_config_t app=gsp_bundle_config();
    esp_gsp_esp_lcd_config_t host=ESP_GSP_ESP_LCD_CONFIG_INIT(); host.perf_log=true;
    host.display=(esp_display_present_target_config_t){
        .hw={.panel=panel,.io=bsp_display_get_panel_io(),
            .panel_type=ESP_DISPLAY_PRESENT_PANEL_IO,
            .input_pixel_format=ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
            .rotation=ESP_DISPLAY_PRESENT_ROTATE_0,.swap_bytes=true,.te_enabled=true,
            .te_sync={.gpio_num=BSP_LCD_TE,.bus_freq_hz=BSP_LCD_PIXEL_CLOCK_HZ,
                      .data_lines=BSP_LCD_DATA_WIDTH}},
        .fb={.mode=ESP_DISPLAY_PRESENT_MODE_AUTO}};
    return esp_gsp_esp_lcd_start(&app,&host,out_gsp);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(MosaicoGameRegisterSystemInventory());

    /* Bring up the retained management plane before display and renderer
     * initialization. The mirror callback returns INVALID_STATE until the
     * first framebuffer exists, but early boot failures remain observable and
     * the recovery-first installer can make a precise health decision. */
    iris_ota_support_start();
    ESP_LOGI(TAG,"management plane ready; renderer startup follows");

#if CONFIG_MOSAICO_GAME_DIAGNOSTIC_IRIS_ONLY
    ESP_LOGW(TAG,"diagnostic Iris-only boot; renderer is intentionally disabled");
    return;
#endif

    ESP_ERROR_CHECK(bsp_power_init());
    ESP_ERROR_CHECK(bsp_power_set_vcc_3v3(true));
    mosaico_game_config_t config=MOSAICO_GAME_CONFIG_DEFAULT();
    config.target_fps=30;
    ESP_ERROR_CHECK(MosaicoGameInit(&config));
    esp_gsp_handle_t gsp=NULL;
    ESP_LOGI(TAG,"starting GSP Canvas presenter");
    ESP_ERROR_CHECK(start_gsp(&gsp));
    ESP_LOGI(TAG,"GSP Canvas presenter ready");
    ESP_ERROR_CHECK(mosaico_raylib_port_init(
        gsp, GSP_RAYLIB_SHOOTER_BIND_GAME_CANVAS));
    ESP_ERROR_CHECK(raylib_screen_mirror_register());
    InitWindow(480,480,"Mosaico Strike"); shooter_game_reset(&s_game,0x4d4f5341U);
    render_game();
    ESP_ERROR_CHECK(esp_gsp_flush(gsp,3000));
    ESP_ERROR_CHECK(esp_iris_mark_healthy());
    ESP_LOGI(TAG,"first frame presented; OTA image accepted");
    ESP_ERROR_CHECK(game_audio_init());
    ESP_ERROR_CHECK(xTaskCreate(touch_task,"game_touch",4096,NULL,5,NULL)==pdPASS?
                    ESP_OK:ESP_ERR_NO_MEM);
    TickType_t wake=xTaskGetTickCount(); uint32_t log_frames=0;
    while(!WindowShouldClose()){
        mosaico_device_event_t event;
        bool state_changed=false;
        while(MosaicoGamePollDeviceEvent(&event)) if(event.type==MOSAICO_DEVICE_EVENT_POINTER){
            shooter_phase_t before=s_game.phase;
            shooter_game_set_pointer(&s_game,event.x,event.y,event.pressed);
            state_changed|=before!=s_game.phase;
            if(before!=SHOOTER_PLAYING&&s_game.phase==SHOOTER_PLAYING)
                (void)game_audio_play(GAME_AUDIO_START);
        }
        if(s_game.phase!=SHOOTER_PLAYING&&!state_changed){
            vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000/config.target_fps));
            continue;
        }
        int64_t update_started=esp_timer_get_time();
        uint32_t old_score=s_game.score, old_shots=s_game.shots_fired;
        uint8_t old_lives=s_game.lives;
        shooter_phase_t old_phase=s_game.phase;
        shooter_game_update(&s_game);
        if(s_game.shots_fired>old_shots)
            (void)game_audio_play(GAME_AUDIO_SHOT);
        if(s_game.score>old_score) (void)game_audio_play(GAME_AUDIO_DESTROY);
        if(s_game.lives<old_lives) (void)game_audio_play(
            s_game.phase==SHOOTER_GAME_OVER?GAME_AUDIO_GAME_OVER:GAME_AUDIO_HIT);
        else if(old_phase!=SHOOTER_GAME_OVER&&s_game.phase==SHOOTER_GAME_OVER)
            (void)game_audio_play(GAME_AUDIO_GAME_OVER);
        uint32_t update_us=(uint32_t)(esp_timer_get_time()-update_started);
        int64_t render_started=esp_timer_get_time(); render_game();
        MosaicoGameRecordTiming(update_us,
            (uint32_t)(esp_timer_get_time()-render_started));
        if(++log_frames%100U==0){mosaico_game_debug_log(TAG);
            ESP_LOGI(TAG,"state_hash=%08lx",
                (unsigned long)shooter_game_state_hash(&s_game));}
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000/config.target_fps));
    }
}
