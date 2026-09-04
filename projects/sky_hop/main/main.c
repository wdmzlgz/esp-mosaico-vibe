// SPDX-License-Identifier: Apache-2.0
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
#include "assets_ids.h"
#include "mosaico_game.h"
#include "mosaico_game_2d.h"
#include "mosaico_game_assets.h"
#include "mosaico_game_audio.h"
#include "mosaico_game_debug.h"
#include "mosaico_game_input.h"
#include "mosaico_raylib_fast.h"
#include "mosaico_raylib_port.h"
#include "mmap_generate_game_assets.h"
#include "nvs_flash.h"
#include "platform_game.h"
#include "raylib_screen_mirror.h"
#include "sky_hop_save.h"

static const char *TAG = "sky_hop";
static platform_game_t s_game;
static esp_lcd_touch_handle_t s_touch;
static MosaicoAtlas s_atlas;
static Sound s_jump, s_coin, s_stomp, s_hurt, s_win;
static Music s_music;
static uint16_t s_best_score;

#define SKY_HOP_TOUCH_POINTS 2
typedef struct {
    int32_t track_id;
    int32_t x;
    int32_t y;
    bool active;
} sky_touch_contact_t;
static sky_touch_contact_t s_contacts[SKY_HOP_TOUCH_POINTS];

#define EMBEDDED_ASSET(symbol) \
    extern const uint8_t _binary_##symbol##_start[]; \
    extern const uint8_t _binary_##symbol##_end[]
EMBEDDED_ASSET(sky_hop_atlas);
EMBEDDED_ASSET(sky_hop_jump);
EMBEDDED_ASSET(sky_hop_coin);
EMBEDDED_ASSET(sky_hop_stomp);
EMBEDDED_ASSET(sky_hop_hurt);
EMBEDDED_ASSET(sky_hop_win);
EMBEDDED_ASSET(sky_hop_music);

static void register_embedded_asset(const char *name,const uint8_t *start,
                                    const uint8_t *end)
{
    ESP_ERROR_CHECK(mosaico_game_asset_register_memory(name,start,(size_t)(end-start)));
}

static void register_embedded_assets(void)
{
    register_embedded_asset("tower.atlas",_binary_sky_hop_atlas_start,_binary_sky_hop_atlas_end);
    register_embedded_asset("jump.sound",_binary_sky_hop_jump_start,_binary_sky_hop_jump_end);
    register_embedded_asset("coin.sound",_binary_sky_hop_coin_start,_binary_sky_hop_coin_end);
    register_embedded_asset("stomp.sound",_binary_sky_hop_stomp_start,_binary_sky_hop_stomp_end);
    register_embedded_asset("hurt.sound",_binary_sky_hop_hurt_start,_binary_sky_hop_hurt_end);
    register_embedded_asset("win.sound",_binary_sky_hop_win_start,_binary_sky_hop_win_end);
    register_embedded_asset("music.sound",_binary_sky_hop_music_start,_binary_sky_hop_music_end);
}

#define PARTICLE_COUNT 24
typedef struct { float x,y,vx,vy; uint8_t life; Color color; } sky_particle_t;
static sky_particle_t s_particles[PARTICLE_COUNT];
static unsigned s_particle_cursor;

static void spawn_particles(float x, float y, Color color, unsigned count)
{
    static const float vx[] = {-2.4f,-1.6f,-.8f,.8f,1.6f,2.4f};
    for (unsigned i=0;i<count;++i) {
        sky_particle_t *p=&s_particles[s_particle_cursor++%PARTICLE_COUNT];
        *p=(sky_particle_t){x,y,vx[i%6],-2.8f-(float)(i%3),18+(uint8_t)(i%8),color};
    }
}

static void update_particles(void)
{
    for(size_t i=0;i<PARTICLE_COUNT;++i)if(s_particles[i].life){
        s_particles[i].x+=s_particles[i].vx;s_particles[i].y+=s_particles[i].vy;
        s_particles[i].vy+=.22f;--s_particles[i].life;
    }
}

static void draw_sprite(mosaico_asset_id_t id, float x, float y,
                        float width, float height, bool flip)
{
    MosaicoSpriteFrame frame;
    if (mosaico_game_2d_atlas_get_frame(s_atlas, id, &frame) != ESP_OK) return;
    Rectangle source = frame.source;
    if (flip) { source.x += source.width; source.width = -source.width; }
    DrawTexturePro(s_atlas.texture, source, (Rectangle){x, y, width, height},
                   (Vector2){0, 0}, 0, WHITE);
}

static void touch_task(void *ctx)
{
    (void)ctx;
    sky_touch_contact_t previous[SKY_HOP_TOUCH_POINTS] = {0};
    while (true) {
        esp_lcd_touch_point_data_t points[SKY_HOP_TOUCH_POINTS] = {0};
        uint8_t count = 0;
        if (esp_lcd_touch_read_data(s_touch) != ESP_OK ||
            esp_lcd_touch_get_data(s_touch, points, &count,
                                    SKY_HOP_TOUCH_POINTS) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(12));
            continue;
        }
        if (count > SKY_HOP_TOUCH_POINTS) count = SKY_HOP_TOUCH_POINTS;
        const uint64_t now = esp_timer_get_time();
        for (uint8_t i = 0; i < count; ++i) {
            if (points[i].x >= 480 || points[i].y >= 480) continue;
            (void)mosaico_game_input_touch(points[i].track_id, points[i].x,
                                            points[i].y, true, now);
        }
        for (size_t old = 0; old < SKY_HOP_TOUCH_POINTS; ++old) {
            if (!previous[old].active) continue;
            bool still_active = false;
            for (uint8_t i = 0; i < count; ++i)
                if (points[i].track_id == previous[old].track_id) still_active = true;
            if (!still_active)
                (void)mosaico_game_input_touch(previous[old].track_id,
                    previous[old].x, previous[old].y, false, now);
        }
        for (size_t i = 0; i < SKY_HOP_TOUCH_POINTS; ++i)
            previous[i] = (sky_touch_contact_t){0};
        for (uint8_t i = 0; i < count; ++i) {
            previous[i] = (sky_touch_contact_t){.track_id=points[i].track_id,
                .x=points[i].x,.y=points[i].y,.active=true};
        }
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}

static void apply_touch_actions(void)
{
    bool left = false, right = false, jump = false;
    for (size_t i = 0; i < SKY_HOP_TOUCH_POINTS; ++i) {
        if (!s_contacts[i].active || s_contacts[i].y < 360) continue;
        left |= s_contacts[i].x < 150;
        right |= s_contacts[i].x >= 150 && s_contacts[i].x < 300;
        jump |= s_contacts[i].x >= 300;
    }
    platform_game_set_action(&s_game, PLATFORM_ACTION_LEFT, left);
    platform_game_set_action(&s_game, PLATFORM_ACTION_RIGHT, right);
    platform_game_set_action(&s_game, PLATFORM_ACTION_JUMP, jump);
}

static void handle_touch_event(const mosaico_device_event_t *event)
{
    sky_touch_contact_t *contact = NULL;
    for (size_t i = 0; i < SKY_HOP_TOUCH_POINTS; ++i)
        if (s_contacts[i].active && s_contacts[i].track_id == event->value)
            contact = &s_contacts[i];
    if (!contact && event->pressed)
        for (size_t i = 0; i < SKY_HOP_TOUCH_POINTS; ++i)
            if (!s_contacts[i].active) { contact = &s_contacts[i]; break; }
    if (!contact) return;

    const bool was_active = contact->active;
    contact->track_id = event->value;
    contact->x = event->x;
    contact->y = event->y;
    contact->active = event->pressed;
    if (event->pressed && !was_active) {
        if (s_game.phase == PLATFORM_PAUSED)
            platform_game_set_action(&s_game, PLATFORM_ACTION_PAUSE, true);
        else if (s_game.phase != PLATFORM_PLAYING)
            platform_game_set_action(&s_game, PLATFORM_ACTION_RESTART, true);
        else if (event->y < 54 && event->x > 420)
            platform_game_set_action(&s_game, PLATFORM_ACTION_PAUSE, true);
    }
    apply_touch_actions();
}

static void centered(const char *text, int y, int size, Color color)
{
    DrawText(text, (480 - MeasureText(text, size)) / 2, y, size, color);
}

static void render(void)
{
    const int camera = (int)s_game.camera_x;
    Camera2D world_camera = {.offset={0,0},.target={s_game.camera_x,0},
                             .rotation=0,.zoom=1};
    BeginDrawing();
    ClearBackground((Color){92, 190, 236, 255});
    DrawRectangle(0, 300, 480, 180, (Color){170, 224, 245, 255});
    for (int i = 0; i < 8; ++i) {
        int x = i * 210 - (camera / 3) % 210;
        DrawRectangle(x, 155 + (i % 2) * 28, 105, 18, (Color){235, 248, 250, 255});
        DrawRectangle(x + 20, 143 + (i % 2) * 28, 62, 24, (Color){235, 248, 250, 255});
    }

    BeginMode2D(world_camera);
    size_t block_count = 0;
    const platform_block_t *blocks = platform_game_blocks(&block_count);
    for (size_t i = 0; i < block_count; ++i) {
        int x = (int)blocks[i].x;
        if (x + (int)blocks[i].width < camera || x >= camera+480) continue;
        for (int tile_x = x; tile_x < x + (int)blocks[i].width; tile_x += 48)
            draw_sprite(MOSAICO_ASSET_ID_TERRAIN, tile_x, blocks[i].y - 2, 50, 50, false);
        if (i == 0) DrawRectangle(x, 438, (int)blocks[i].width, 42,
                                 (Color){111, 73, 45, 255});
    }
    for (size_t i = 0; i < PLATFORM_COIN_COUNT; ++i) if (!s_game.coins[i].collected) {
        int x = (int)s_game.coins[i].x, y = (int)s_game.coins[i].y;
        float pulse = 25.0f + (float)((s_game.tick / 5 + i) % 3) * 2.0f;
        draw_sprite(MOSAICO_ASSET_ID_COIN, x - pulse/2, y - pulse/2, pulse, pulse, false);
    }
    for (size_t i = 0; i < PLATFORM_ENEMY_COUNT; ++i) if (s_game.enemies[i].active) {
        int x = (int)s_game.enemies[i].x, y = (int)s_game.enemies[i].y;
        draw_sprite(MOSAICO_ASSET_ID_ENEMY_BEETLE, x - 7, y - 12, 44, 44,
                    s_game.enemies[i].speed < 0);
    }
    int px = (int)s_game.player_x, py = (int)s_game.player_y;
    mosaico_asset_id_t hero = !s_game.grounded ? MOSAICO_ASSET_ID_HERO_JUMP :
        (s_game.move_left || s_game.move_right) ?
        MosaicoAnimationFrameAt(MOSAICO_ANIMATION_HERO_RUN_FRAMES,
            MOSAICO_ANIMATION_HERO_RUN_FRAME_COUNT,
            MOSAICO_ANIMATION_HERO_RUN_FRAME_TICKS, s_game.tick, true) :
        MOSAICO_ASSET_ID_HERO_IDLE;
    draw_sprite(hero, px - 14, py - 22, 58, 64, s_game.velocity_x < 0);
    if (1380 - camera < 500)
        draw_sprite(MOSAICO_ASSET_ID_FINISH_FLAG, 1368, 292, 70, 98, false);
    for(size_t i=0;i<PARTICLE_COUNT;++i)if(s_particles[i].life)
        DrawCircle((int)s_particles[i].x,(int)s_particles[i].y,
                   2+(s_particles[i].life%3),s_particles[i].color);
    EndMode2D();

    DrawRectangle(0, 0, 480, 42, (Color){22, 42, 68, 230});
    DrawText(TextFormat("SCORE %04u", s_game.score), 14, 11, 20, RAYWHITE);
    DrawText(TextFormat("BEST %04u", s_best_score), 175, 11, 16, (Color){255,220,80,255});
    DrawText(TextFormat("LIFE %u", s_game.lives), 365, 11, 20, RAYWHITE);
    DrawRectangle(438, 4, 36, 32, (Color){52,77,104,255});
    DrawRectangle(449, 11, 4, 18, RAYWHITE); DrawRectangle(459, 11, 4, 18, RAYWHITE);
    DrawRectangle(8, 408, 138, 64, (Color){25, 43, 65, 210});
    DrawRectangle(154, 408, 138, 64, (Color){25, 43, 65, 210});
    DrawRectangle(300, 408, 172, 64, (Color){226, 95, 63, 230});
    DrawText("LEFT", 49, 429, 20, RAYWHITE);
    DrawText("RIGHT", 196, 429, 20, RAYWHITE);
    DrawText("JUMP", 354, 429, 20, RAYWHITE);

    if (s_game.phase != PLATFORM_PLAYING) {
        float t=s_game.phase_tick>=12?1.0f:(float)s_game.phase_tick/12.0f;
        t=1.0f-(1.0f-t)*(1.0f-t);
        int panel_y=(int)(-190+310*t);
        DrawRectangle(42, panel_y, 396, 190, (Color){20, 39, 65, 255});
        const char *title = s_game.phase == PLATFORM_WON ? "YOU MADE IT!" :
            s_game.phase == PLATFORM_GAME_OVER ? "TRY AGAIN" :
            s_game.phase == PLATFORM_PAUSED ? "PAUSED" : "SKY HOP";
        centered(title, panel_y+38, 38, (Color){255, 220, 80, 255});
        centered("AN ORIGINAL PLATFORM ADVENTURE", panel_y+93, 16, RAYWHITE);
        centered(s_game.phase==PLATFORM_PAUSED?"TOUCH TO RESUME":"TOUCH TO START",
                 panel_y+143, 21, (Color){129, 224, 171, 255});
    }
    EndDrawing();
}

static esp_err_t start_gsp(esp_gsp_handle_t *out_gsp)
{
    bsp_display_config_t cfg = BSP_DISPLAY_DEFAULT_CONFIG();
    cfg.enable_touch = false;
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&cfg, &panel), TAG, "display");
    ESP_RETURN_ON_ERROR(bsp_touch_new(BSP_DISPLAY_ROTATE_0, &s_touch), TAG, "touch");
    esp_gsp_config_t app = gsp_bundle_config();
    esp_gsp_esp_lcd_config_t host = ESP_GSP_ESP_LCD_CONFIG_INIT();
    host.perf_log = true;
    host.display = (esp_display_present_target_config_t){
        .hw = {.panel=panel, .io=bsp_display_get_panel_io(),
            .panel_type=ESP_DISPLAY_PRESENT_PANEL_IO,
            .input_pixel_format=ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
            .rotation=ESP_DISPLAY_PRESENT_ROTATE_0, .swap_bytes=true, .te_enabled=true,
            .te_sync={.gpio_num=BSP_LCD_TE, .bus_freq_hz=BSP_LCD_PIXEL_CLOCK_HZ,
                      .data_lines=BSP_LCD_DATA_WIDTH}},
        .fb = {.mode=ESP_DISPLAY_PRESENT_MODE_AUTO}};
    return esp_gsp_esp_lcd_start(&app, &host, out_gsp);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot checkpoint: nvs");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(MosaicoGameRegisterSystemInventory());
    iris_ota_support_start();
#if CONFIG_MOSAICO_GAME_DIAGNOSTIC_IRIS_ONLY
    ESP_LOGW(TAG, "diagnostic Iris-only boot");
    return;
#endif
    ESP_ERROR_CHECK(bsp_power_init());
    ESP_ERROR_CHECK(bsp_power_set_vcc_3v3(true));
    ESP_LOGI(TAG, "boot checkpoint: runtime");
    mosaico_game_config_t config = MOSAICO_GAME_CONFIG_DEFAULT();
    config.target_fps = 30;
    ESP_ERROR_CHECK(MosaicoGameInit(&config));
    mosaico_asset_store_config_t assets = {.partition_label="game_assets",
        .max_files=MMAP_GAME_ASSETS_FILES, .checksum=MMAP_GAME_ASSETS_CHECKSUM,
        .mmap_enable=true};
    register_embedded_assets();
    esp_err_t asset_mount_error=mosaico_game_assets_mount(&assets);
    if(asset_mount_error!=ESP_OK)
        ESP_LOGW(TAG,"asset partition unavailable, using embedded assets: %s",
                 esp_err_to_name(asset_mount_error));
    ESP_LOGI(TAG, "boot checkpoint: assets ready");
    s_atlas = LoadMosaicoAtlas("tower.atlas");
    ESP_ERROR_CHECK(s_atlas.texture.id ? ESP_OK : ESP_ERR_NOT_FOUND);
    ESP_LOGI(TAG, "boot checkpoint: atlas loaded");
    esp_gsp_handle_t gsp = NULL;
    ESP_ERROR_CHECK(start_gsp(&gsp));
    ESP_LOGI(TAG, "boot checkpoint: display ready");
    ESP_ERROR_CHECK(mosaico_raylib_port_init(gsp, GSP_SKY_HOP_BIND_GAME_CANVAS));
    ESP_ERROR_CHECK(raylib_screen_mirror_register());
    InitWindow(480, 480, "Sky Hop");
    platform_game_reset(&s_game);
    esp_err_t save_error=sky_hop_save_load(&s_best_score);
    if(save_error!=ESP_OK)ESP_LOGW(TAG,"save load failed: %s",esp_err_to_name(save_error));
    ESP_LOGI(TAG, "boot checkpoint: first frame");
    render();
    ESP_ERROR_CHECK(esp_gsp_flush(gsp, 3000));
    ESP_ERROR_CHECK(esp_iris_mark_healthy());
    ESP_LOGI(TAG, "boot checkpoint: healthy");
    InitAudioDevice();
    s_jump=LoadSound("jump.sound"); s_coin=LoadSound("coin.sound");
    s_stomp=LoadSound("stomp.sound"); s_hurt=LoadSound("hurt.sound");
    s_win=LoadSound("win.sound"); s_music=LoadMusicStream("music.sound");
    SetMusicVolume(s_music, .18f); PlayMusicStream(s_music);
    ESP_ERROR_CHECK(xTaskCreate(touch_task, "game_touch", 4096, NULL, 5, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    TickType_t wake = xTaskGetTickCount();
    while (!WindowShouldClose()) {
        mosaico_device_event_t event;
        bool grounded_before=s_game.grounded;
        uint16_t score_before=s_game.score;
        uint8_t lives_before=s_game.lives;
        platform_phase_t phase_before=s_game.phase;
        bool enemy_before[PLATFORM_ENEMY_COUNT];
        for(size_t i=0;i<PLATFORM_ENEMY_COUNT;++i)enemy_before[i]=s_game.enemies[i].active;
        while (MosaicoGamePollDeviceEvent(&event)) {
            if (event.type == MOSAICO_DEVICE_EVENT_POINTER)
                platform_game_set_pointer(&s_game,event.x,event.y,event.pressed);
            else if (event.type == MOSAICO_DEVICE_EVENT_TOUCH)
                handle_touch_event(&event);
            else if(event.type==MOSAICO_DEVICE_EVENT_BUTTON){
                platform_action_t action=event.value==0?PLATFORM_ACTION_LEFT:
                    event.value==1?PLATFORM_ACTION_RIGHT:event.value==2?
                    PLATFORM_ACTION_JUMP:PLATFORM_ACTION_PAUSE;
                if(s_game.phase!=PLATFORM_PLAYING&&s_game.phase!=PLATFORM_PAUSED&&event.pressed)
                    action=PLATFORM_ACTION_RESTART;
                platform_game_set_action(&s_game,action,event.pressed);
            }else if(event.type==MOSAICO_DEVICE_EVENT_JOYSTICK||event.type==MOSAICO_DEVICE_EVENT_IMU){
                platform_game_set_action(&s_game,PLATFORM_ACTION_LEFT,event.x<-250);
                platform_game_set_action(&s_game,PLATFORM_ACTION_RIGHT,event.x>250);
            }
            if(grounded_before&&!s_game.grounded)PlaySound(s_jump);
        }
        int64_t started = esp_timer_get_time();
        platform_game_update(&s_game);
        if(s_game.score>score_before){
            bool stomp=false;
            for(size_t i=0;i<PLATFORM_ENEMY_COUNT;++i)
                if(enemy_before[i]&&!s_game.enemies[i].active)stomp=true;
            PlaySound(stomp?s_stomp:s_coin);
            spawn_particles(s_game.player_x+14,s_game.player_y+8,
                            stomp?(Color){205,125,255,255}:(Color){255,220,70,255},8);
        }
        if(s_game.lives<lives_before){PlaySound(s_hurt);spawn_particles(s_game.player_x+14,s_game.player_y+12,(Color){255,95,80,255},12);}
        if(phase_before!=PLATFORM_WON&&s_game.phase==PLATFORM_WON)PlaySound(s_win);
        if(s_game.score>s_best_score)s_best_score=s_game.score;
        if(phase_before!=s_game.phase&&(s_game.phase==PLATFORM_WON||s_game.phase==PLATFORM_GAME_OVER)){
            save_error=sky_hop_save_best_score(s_best_score);
            if(save_error!=ESP_OK)ESP_LOGW(TAG,"save write failed: %s",esp_err_to_name(save_error));
        }
        update_particles();
        uint32_t update_us = (uint32_t)(esp_timer_get_time() - started);
        started = esp_timer_get_time();
        render();
        MosaicoGameRecordTiming(update_us, (uint32_t)(esp_timer_get_time() - started));
        if (s_game.tick && s_game.tick % 300 == 0) {
            mosaico_game_debug_log(TAG);
            ESP_LOGI(TAG, "state_hash=%08lx", (unsigned long)platform_game_state_hash(&s_game));
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / config.target_fps));
    }
}
