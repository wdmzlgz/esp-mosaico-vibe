// SPDX-License-Identifier: Apache-2.0
#include <math.h>
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
#include "mosaico_game_2d.h"
#include "mosaico_game_assets.h"
#include "mosaico_game_debug.h"
#include "mosaico_game_input.h"
#include "mosaico_game_tilemap.h"
#include "mosaico_raylib_port.h"
#include "mmap_generate_game_assets.h"
#include "assets_ids.h"
#include "nvs_flash.h"
#include "mosaico_raylib_fast.h"
#include "tower_audio.h"
#include "tower_game.h"
#include "tower_screen_mirror.h"

static const char *TAG="tower_defense";
static tower_game_t s_game;
static esp_lcd_touch_handle_t s_touch;
static MosaicoAtlas s_atlas;
static MosaicoTilemap s_map;
typedef struct {float x,y;uint8_t ticks;bool active;} tower_effect_t;
static tower_effect_t s_effects[12];

static const Color C_BG={5,10,20,255};
static const Color C_GRASS={12,30,35,255};
static const Color C_GOLD={255,193,61,255};
static const Color C_CYAN={42,224,231,255};
static const Color C_RED={255,73,105,255};
static const Color C_BLUE={110,145,255,255};

static void draw_sprite(const char *name,float x,float y,float size,float rotation,Color tint)
{
    const MosaicoSpriteFrame *frame=MosaicoAtlasGetFrame(
        s_atlas,mosaico_game_asset_id(name));
    if(!frame)return;
    DrawTexturePro(s_atlas.texture,frame->source,
        (Rectangle){x,y,size,size},(Vector2){size*.5f,size*.5f},rotation,tint);
}

static void draw_sprite_id(mosaico_asset_id_t id,float x,float y,float size,Color tint)
{
    const MosaicoSpriteFrame *frame=MosaicoAtlasGetFrame(s_atlas,id);
    if(frame)DrawTexturePro(s_atlas.texture,frame->source,
        (Rectangle){x,y,size,size},(Vector2){size*.5f,size*.5f},0,tint);
}

static void load_level(void)
{
    s_map=LoadMosaicoTilemap("level01.map");
    const Vector2 *points=NULL;size_t count=MosaicoTilemapPathPoints(s_map,&points);
    tower_level_t level={.path_count=(uint8_t)count};
    for(size_t i=0;i<count&&i<TOWER_MAX_PATH_POINTS;++i){level.path[i][0]=points[i].x;level.path[i][1]=points[i].y;}
    for(unsigned i=0;i<TOWER_PAD_COUNT;++i){
        char name[12];snprintf(name,sizeof(name),"pad_%u",i);
        MosaicoMapObject object={0};
        if(MosaicoTilemapFindObject(s_map,mosaico_game_asset_id(name),&object)){
            level.pads[i][0]=object.x;level.pads[i][1]=object.y;
        }
    }
    if(!tower_game_configure_level(&level))ESP_LOGW(TAG,"using built-in level fallback");
}

static void draw_centered(const char *text,int y,int size,Color color)
{
    DrawText(text,(480-MeasureText(text,size))/2,y,size,color);
}

static void touch_task(void *ctx)
{
    (void)ctx;
    bool was_pressed=false;
    while(true){
        esp_lcd_touch_point_data_t points[1]={0}; uint8_t count=0;
        bool pressed=esp_lcd_touch_read_data(s_touch)==ESP_OK &&
            esp_lcd_touch_get_data(s_touch,points,&count,1)==ESP_OK && count;
        mosaico_device_event_t event={.type=MOSAICO_DEVICE_EVENT_POINTER,
            .x=pressed?points[0].x:0,.y=pressed?points[0].y:0,
            .pressed=pressed,.timestamp_us=esp_timer_get_time()};
        if(pressed||was_pressed) (void)mosaico_game_input_pointer(
            event.x,event.y,event.pressed,event.timestamp_us);
        was_pressed=pressed;
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}

static void draw_pad(const tower_slot_t *tower)
{
    int x=tower->x,y=tower->y;
    Color main=tower->type==TOWER_PULSE?C_CYAN:
        tower->type==TOWER_RAPID?C_GOLD:C_BLUE;
    draw_sprite("build_pad",x,y,54,0,tower->occupied?(Color){150,150,150,255}:WHITE);
    if(!tower->occupied){
        Color plus=(s_game.tick/15U)%2?C_CYAN:(Color){76,139,139,255};
        DrawRectangle(x-8,y-2,16,4,plus); DrawRectangle(x-2,y-8,4,16,plus);
        return;
    }
    const char*names[]={"tower_pulse","tower_rapid","tower_frost"};
    draw_sprite(names[tower->type],x,y-4,58,0,WHITE);
    DrawRectangle(x-13,y+14,26,7,(Color){4,15,21,255});
    for(int i=0;i<3;++i) DrawRectangle(x-10+i*7,y+16,5,3,
        i<tower->level?main:(Color){39,67,70,255});
}

static void draw_enemy(const tower_enemy_t *enemy)
{
    int x=(int)enemy->x,y=(int)enemy->y;
    const char*names[]={"enemy_drone","enemy_brute","enemy_scout"};
    int size=enemy->kind==1?48:enemy->kind==2?34:40;
    draw_sprite(names[enemy->kind],x,y,size,0,
                enemy->slow_ticks?(Color){145,205,255,255}:WHITE);
    int hpw=(int)((enemy->hp/enemy->max_hp)*28.0f);
    DrawRectangle(x-15,y-size/2-9,30,5,(Color){5,13,18,255});
    DrawRectangle(x-14,y-size/2-8,hpw,3,
        enemy->hp/enemy->max_hp>0.35f?(Color){73,236,135,255}:C_RED);
}

static void draw_projectiles(void)
{
    for(size_t i=0;i<TOWER_MAX_PROJECTILES;++i){
        const tower_projectile_t *p=&s_game.projectiles[i];
        if(!p->active) continue;
        const char*names[]={"projectile_pulse","projectile_rapid","projectile_frost"};
        draw_sprite(names[p->kind],p->x,p->y,p->kind==TOWER_RAPID?14:18,0,WHITE);
    }
}

static void add_explosion(float x,float y)
{
    for(size_t i=0;i<sizeof(s_effects)/sizeof(s_effects[0]);++i){
        if(s_effects[i].active)continue;
        s_effects[i]=(tower_effect_t){.x=x,.y=y,.ticks=12,.active=true};
        return;
    }
}

static void draw_effects(void)
{
    for(size_t i=0;i<sizeof(s_effects)/sizeof(s_effects[0]);++i){
        tower_effect_t*effect=&s_effects[i];
        if(!effect->active)continue;
        mosaico_asset_id_t frame=MosaicoAnimationFrameAt(
            MOSAICO_ANIMATION_EXPLOSION_FRAMES,
            MOSAICO_ANIMATION_EXPLOSION_FRAME_COUNT,
            MOSAICO_ANIMATION_EXPLOSION_FRAME_TICKS,12U-effect->ticks,false);
        draw_sprite_id(frame,effect->x,effect->y,54+(12-effect->ticks)*2,WHITE);
    }
}

static void draw_core(void)
{
    float pulse=62+(float)((s_game.tick/5U)%4);
    draw_sprite("reactor_core",467,354,pulse,0,s_game.base_hp>6?WHITE:(Color){255,110,110,255});
}

static void draw_hud(void)
{
    DrawRectangle(0,0,480,69,C_BG);
    DrawRectangle(0,65,480,4,(Color){13,72,83,255});
    DrawRectangle(0,65,110,2,C_CYAN);
    DrawRectangle(12,9,118,47,(Color){10,27,39,255});
    DrawRectangleLines(12,9,118,47,(Color){35,100,112,255});
    DrawRectangle(18,15,4,35,C_CYAN);
    DrawText(TextFormat("WAVE %02u",s_game.wave),29,14,17,RAYWHITE);
    DrawText(TextFormat("CRED %03u",s_game.credits),29,36,13,C_GOLD);
    DrawRectangle(140,9,132,47,(Color){10,27,39,255});
    DrawRectangleLines(140,9,132,47,(Color){35,77,91,255});
    DrawText("TACTICAL SCORE",151,15,11,(Color){107,151,160,255});
    DrawText(TextFormat("%06lu",(unsigned long)s_game.score),151,34,17,RAYWHITE);
    DrawRectangle(282,9,125,47,(Color){10,27,39,255});
    DrawRectangleLines(282,9,125,47,(Color){35,77,91,255});
    DrawText(TextFormat("CORE %02u",s_game.base_hp),293,15,14,
        s_game.base_hp>6?(Color){102,233,139,255}:C_RED);
    DrawRectangle(293,39,102,7,(Color){28,48,53,255});
    DrawRectangle(295,41,s_game.base_hp*5,3,
        s_game.base_hp>6?(Color){78,228,134,255}:C_RED);
    DrawRectangle(418,9,50,47,(Color){13,36,47,255});
    DrawRectangleLines(418,9,50,47,C_CYAN);
    DrawRectangle(432,21,7,22,(Color){181,246,245,255});
    DrawRectangle(447,21,7,22,(Color){181,246,245,255});
}

static void draw_shop_card(int type,int x,const char *name,const char *role,
                           const char *cost,Color color)
{
    bool selected=s_game.selected_type==type;
    DrawRectangle(x+3,405,143,67,(Color){2,8,14,255});
    DrawRectangle(x,402,146,67,selected?(Color){19,47,56,255}:(Color){10,25,32,255});
    DrawRectangleLines(x,402,146,67,selected?color:(Color){39,72,78,255});
    DrawRectangle(x,402,selected?45:18,3,color);
    DrawRectangle(x+8,411,35,42,(Color){5,16,24,255});
    DrawRectangleLines(x+8,411,35,42,(Color){44,81,88,255});
    DrawRectangle(x+13,419,25,25,color);
    if(type==TOWER_PULSE) DrawRectangle(x+23,413,5,24,RAYWHITE);
    else if(type==TOWER_RAPID){
        DrawRectangle(x+17,413,5,24,RAYWHITE);DrawRectangle(x+29,413,5,24,RAYWHITE);
    }else DrawTriangle((Vector2){x+25,411},(Vector2){x+14,442},(Vector2){x+36,442},RAYWHITE);
    DrawText(name,x+50,411,14,RAYWHITE);
    DrawText(role,x+50,431,10,(Color){101,148,155,255});
    DrawText(cost,x+96,449,13,C_GOLD);
    if(selected){DrawRectangle(x+7,460,62,2,color);DrawText("READY",x+13,449,10,color);}
}

static void draw_shop(void)
{
    DrawRectangle(0,392,480,88,(Color){3,10,16,255});
    DrawRectangle(0,392,480,2,(Color){31,106,112,255});
    draw_shop_card(TOWER_PULSE,8,"PULSE","BALANCED","$70",C_CYAN);
    draw_shop_card(TOWER_RAPID,164,"RAPID","FIRE RATE","$95",C_GOLD);
    draw_shop_card(TOWER_FROST,320,"FROST","SLOW FIELD","$120",C_BLUE);
}

static void draw_overlay(const char *title,const char *line,const char *action,Color accent)
{
    DrawRectangle(29,125,422,204,(Color){1,7,13,210});
    DrawRectangle(35,119,410,202,(Color){5,16,27,246});
    DrawRectangleLines(35,119,410,202,(Color){31,81,92,255});
    DrawRectangle(35,119,410,5,accent);
    DrawRectangle(35,119,36,12,accent);DrawRectangle(409,119,36,12,accent);
    DrawText("MOSAICO // DEFENSE PROTOCOL",52,140,11,(Color){94,142,153,255});
    draw_centered(title,166,32,RAYWHITE);
    DrawRectangle(103,207,274,2,(Color){30,78,88,255});
    draw_centered(line,224,16,(Color){171,211,211,255});
    DrawRectangle(107,268,266,43,(Color){2,11,18,255});
    DrawRectangle(111,264,258,43,accent);
    DrawRectangle(118,271,244,29,(Color){8,27,34,255});
    draw_centered(action,279,15,RAYWHITE);
}

static void render_game(void)
{
    BeginDrawing();
    ClearBackground(C_GRASS);
    DrawMosaicoTilemapLayer(s_map,0,(Rectangle){0,0,480,320});
    draw_core();
    for(size_t i=0;i<TOWER_PAD_COUNT;++i) draw_pad(&s_game.towers[i]);
    for(size_t i=0;i<TOWER_MAX_ENEMIES;++i) if(s_game.enemies[i].active)
        draw_enemy(&s_game.enemies[i]);
    draw_projectiles();
    draw_effects();
    draw_hud(); draw_shop();
    if(s_game.phase==TOWER_START)
        draw_overlay("CIRCUIT KEEP","BUILD TOWERS // DEFEND THE CORE","TOUCH TO START",C_CYAN);
    else if(s_game.phase==TOWER_PAUSED)
        draw_overlay("PAUSED","TACTICAL GRID ON HOLD","TOUCH PAUSE TO RESUME",C_GOLD);
    else if(s_game.phase==TOWER_GAME_OVER)
        draw_overlay("CORE LOST",TextFormat("WAVE %u // SCORE %05lu",s_game.wave,
                     (unsigned long)s_game.score),"TOUCH TO RETRY",C_RED);
    else if(s_game.intermission){
        DrawRectangle(164,374,152,18,(Color){4,15,22,255});
        DrawRectangleLines(164,374,152,18,(Color){77,103,100,255});
        draw_centered(TextFormat("WAVE %u // T-%u",s_game.wave,
            (s_game.intermission+29)/30),378,11,C_GOLD);
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
    iris_ota_support_start();
    ESP_LOGI(TAG,"management plane ready; renderer startup follows");
#if CONFIG_MOSAICO_GAME_DIAGNOSTIC_IRIS_ONLY
    ESP_LOGW(TAG,"diagnostic Iris-only boot; renderer is intentionally disabled");
    return;
#endif
    ESP_ERROR_CHECK(bsp_power_init());
    ESP_ERROR_CHECK(bsp_power_set_vcc_3v3(true));
    mosaico_game_config_t config=MOSAICO_GAME_CONFIG_DEFAULT(); config.target_fps=30;
    ESP_ERROR_CHECK(MosaicoGameInit(&config));
    mosaico_asset_store_config_t assets={.partition_label="game_assets",
        .max_files=MMAP_GAME_ASSETS_FILES,.checksum=MMAP_GAME_ASSETS_CHECKSUM,
        .mmap_enable=true};
    ESP_ERROR_CHECK(mosaico_game_assets_mount(&assets));
    s_atlas=LoadMosaicoAtlas("tower.atlas");
    ESP_ERROR_CHECK(s_atlas.texture.id?ESP_OK:ESP_ERR_NOT_FOUND);
    load_level();
    ESP_ERROR_CHECK(s_map?ESP_OK:ESP_ERR_NOT_FOUND);
    esp_gsp_handle_t gsp=NULL;
    ESP_ERROR_CHECK(start_gsp(&gsp));
    ESP_ERROR_CHECK(mosaico_raylib_port_init(gsp,GSP_TOWER_DEFENSE_BIND_GAME_CANVAS));
    ESP_ERROR_CHECK(tower_screen_mirror_register());
    InitWindow(480,480,"Circuit Keep");
    tower_game_reset(&s_game,0x544f5745U);
    render_game();
    ESP_ERROR_CHECK(esp_gsp_flush(gsp,3000));
    ESP_ERROR_CHECK(esp_iris_mark_healthy());
    ESP_LOGI(TAG,"first frame presented; OTA image accepted");
    ESP_ERROR_CHECK(tower_audio_init());
    ESP_ERROR_CHECK(xTaskCreate(touch_task,"tower_touch",4096,NULL,5,NULL)==pdPASS?
                    ESP_OK:ESP_ERR_NO_MEM);
    TickType_t wake=xTaskGetTickCount(); uint32_t log_frames=0;
    while(!WindowShouldClose()){
        mosaico_device_event_t event;
        tower_phase_t phase_before=s_game.phase;
        uint32_t shots_before=s_game.shots,kills_before=s_game.kills;
        uint16_t credits_before=s_game.credits,wave_before=s_game.wave;
        uint8_t hp_before=s_game.base_hp;
        bool enemy_before[TOWER_MAX_ENEMIES];
        float enemy_x[TOWER_MAX_ENEMIES],enemy_y[TOWER_MAX_ENEMIES];
        for(size_t i=0;i<TOWER_MAX_ENEMIES;++i){
            enemy_before[i]=s_game.enemies[i].active;
            enemy_x[i]=s_game.enemies[i].x;enemy_y[i]=s_game.enemies[i].y;
        }
        bool input=false;
        while(MosaicoGamePollDeviceEvent(&event)) if(event.type==MOSAICO_DEVICE_EVENT_POINTER){
            tower_game_set_pointer(&s_game,event.x,event.y,event.pressed); input=true;
        }
        if(phase_before!=TOWER_PLAYING&&s_game.phase==TOWER_PLAYING)
            (void)tower_audio_play(TOWER_AUDIO_START);
        if(phase_before!=s_game.phase)tower_audio_set_music(
            s_game.phase==TOWER_PAUSED,s_game.phase==TOWER_GAME_OVER);
        if(s_game.credits<credits_before) (void)tower_audio_play(TOWER_AUDIO_BUILD);
        if(s_game.phase!=TOWER_PLAYING&&!input){
            vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000/config.target_fps)); continue;
        }
        int64_t update_started=esp_timer_get_time();
        tower_game_update(&s_game);
        uint32_t update_us=(uint32_t)(esp_timer_get_time()-update_started);
        for(size_t i=0;i<TOWER_MAX_ENEMIES;++i){
            if(enemy_before[i]&&!s_game.enemies[i].active&&
               s_game.base_hp==hp_before)add_explosion(enemy_x[i],enemy_y[i]);
        }
        for(size_t i=0;i<sizeof(s_effects)/sizeof(s_effects[0]);++i){
            if(s_effects[i].active&&!--s_effects[i].ticks)s_effects[i].active=false;
        }
        if(s_game.shots>shots_before) (void)tower_audio_play(TOWER_AUDIO_SHOT);
        if(s_game.kills>kills_before) (void)tower_audio_play(TOWER_AUDIO_KILL);
        if(s_game.wave>wave_before) (void)tower_audio_play(TOWER_AUDIO_WAVE);
        if(s_game.base_hp<hp_before) (void)tower_audio_play(
            s_game.phase==TOWER_GAME_OVER?TOWER_AUDIO_GAME_OVER:TOWER_AUDIO_LEAK);
        int64_t render_started=esp_timer_get_time(); render_game();
        MosaicoGameRecordTiming(update_us,(uint32_t)(esp_timer_get_time()-render_started));
        if(++log_frames%100U==0){
            mosaico_game_debug_log(TAG);
            ESP_LOGI(TAG,"wave=%u enemies=%u/%u state_hash=%08lx",s_game.wave,
                s_game.wave_spawned,s_game.wave_total,
                (unsigned long)tower_game_state_hash(&s_game));
        }
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000/config.target_fps));
    }
}
