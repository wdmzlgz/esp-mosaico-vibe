// SPDX-License-Identifier: Apache-2.0
// Host presenter using the same packed assets, tilemap and RGB565 sprite core.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaico_game_2d.h"
#include "mosaico_game_assets.h"
#include "mosaico_game_tilemap.h"
#include "tower_game.h"

#define HOST_WIDTH 480
#define HOST_HEIGHT 480
#define HOST_FILES 24

typedef struct {
    uint32_t frames;
    uint32_t wave;
    uint32_t score;
    uint32_t credits;
    uint32_t base_hp;
    uint32_t kills;
    uint32_t state_hash;
} host_result_t;

typedef struct {
    uint32_t frame;
    uint8_t type; /* 1=tap, 2=pause, 3=resume, 4=step, 5=reset */
    int16_t x;
    int16_t y;
} host_event_t;

typedef struct {
    char name[64];
    uint8_t *data;
    size_t size;
} host_file_t;

static char s_asset_root[512];
static host_file_t s_files[HOST_FILES];
static uint16_t *s_pixels;

uint32_t mosaico_game_asset_id(const char *name)
{
    uint32_t value = 2166136261U;
    if (!name) return 0;
    while (*name) value = (value ^ (uint8_t)*name++) * 16777619U;
    return value;
}

esp_err_t mosaico_game_assets_mount(const mosaico_asset_store_config_t *config)
{
    (void)config;
    return ESP_OK;
}

void mosaico_game_assets_unmount(void) {}
bool mosaico_game_assets_is_mounted(void) { return true; }

esp_err_t mosaico_game_asset_open(const char *name, mosaico_asset_view_t *out)
{
    if (!name || !out) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < HOST_FILES; ++i) {
        if (s_files[i].data && strcmp(s_files[i].name, name) == 0) {
            *out = (mosaico_asset_view_t){.id=mosaico_game_asset_id(name),
                .name=s_files[i].name,.data=s_files[i].data,.size=s_files[i].size};
            return ESP_OK;
        }
    }
    char path[640];
    if (snprintf(path, sizeof(path), "%s/%s", s_asset_root, name) >= (int)sizeof(path))
        return ESP_ERR_INVALID_ARG;
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return ESP_FAIL; }
    long length = ftell(file);
    rewind(file);
    if (length <= 0) { fclose(file); return ESP_FAIL; }
    for (unsigned i = 0; i < HOST_FILES; ++i) {
        if (s_files[i].data) continue;
        uint8_t *data = malloc((size_t)length);
        if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
            free(data); fclose(file); return ESP_FAIL;
        }
        fclose(file);
        snprintf(s_files[i].name, sizeof(s_files[i].name), "%s", name);
        s_files[i].data = data;
        s_files[i].size = (size_t)length;
        *out = (mosaico_asset_view_t){.id=mosaico_game_asset_id(name),
            .name=s_files[i].name,.data=data,.size=(size_t)length};
        return ESP_OK;
    }
    fclose(file);
    return ESP_ERR_NO_MEM;
}

esp_err_t mosaico_game_asset_open_id(mosaico_asset_id_t id,
                                     mosaico_asset_view_t *out)
{
    static const char *known[] = {"tower.atlas", "terrain.atlas", "level01.map"};
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (mosaico_game_asset_id(known[i]) == id)
            return mosaico_game_asset_open(known[i], out);
    return ESP_ERR_NOT_FOUND;
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | (b >> 3));
}

static void rectangle(int x, int y, int width, int height, uint16_t color)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + width > HOST_WIDTH ? HOST_WIDTH : x + width;
    int y1 = y + height > HOST_HEIGHT ? HOST_HEIGHT : y + height;
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) s_pixels[py * HOST_WIDTH + px] = color;
}

static void sprite(MosaicoAtlas atlas, const char *name, float x, float y,
                   float size, float rotation, Color tint)
{
    const MosaicoSpriteFrame *frame = MosaicoAtlasGetFrame(
        atlas, mosaico_game_asset_id(name));
    if (frame)
        Mosaico2DDrawTexturePro(atlas.texture, frame->source,
            (Rectangle){x, y, size, size}, (Vector2){size / 2, size / 2},
            rotation, tint);
}

static bool load_level(MosaicoTilemap map)
{
    const Vector2 *points = NULL;
    size_t count = MosaicoTilemapPathPoints(map, &points);
    tower_level_t level = {.path_count = (uint8_t)count};
    for (size_t i = 0; i < count && i < TOWER_MAX_PATH_POINTS; ++i) {
        level.path[i][0] = points[i].x;
        level.path[i][1] = points[i].y;
    }
    for (unsigned i = 0; i < TOWER_PAD_COUNT; ++i) {
        char name[12];
        snprintf(name, sizeof(name), "pad_%u", i);
        MosaicoMapObject object;
        if (MosaicoTilemapFindObject(map, mosaico_game_asset_id(name), &object)) {
            level.pads[i][0] = object.x;
            level.pads[i][1] = object.y;
        }
    }
    return tower_game_configure_level(&level);
}

int mosaico_tower_host_render_replay(const char *asset_root, unsigned frames,
    const host_event_t *events, size_t event_count, uint16_t *pixels,
    host_result_t *result)
{
    if (!asset_root || !pixels || !result) return -1;
    snprintf(s_asset_root, sizeof(s_asset_root), "%s", asset_root);
    s_pixels = pixels;
    mosaico_game_2d_set_target(pixels, HOST_WIDTH, HOST_WIDTH, HOST_HEIGHT);
    MosaicoAtlas atlas = LoadMosaicoAtlas("tower.atlas");
    MosaicoTilemap map = LoadMosaicoTilemap("level01.map");
    if (!atlas.texture.id || !map || !load_level(map)) return -2;

    tower_game_t game;
    tower_game_reset(&game, 0x544f5745U);
#define TAP(px, py) do { tower_game_set_pointer(&game, px, py, true); \
                         tower_game_set_pointer(&game, px, py, false); } while (0)
    if (!events) {
        TAP(240, 220); TAP(58, 154); TAP(200, 435); TAP(195, 130);
        TAP(350, 435); TAP(414, 218);
    }
    bool paused = false;
    size_t next_event = 0;
    for (unsigned i = 0; i < frames; ++i) {
        bool single_step = false;
        while (next_event < event_count && events[next_event].frame == i) {
            const host_event_t *event = &events[next_event++];
            if (event->type == 1) TAP(event->x, event->y);
            else if (event->type == 2) paused = true;
            else if (event->type == 3) paused = false;
            else if (event->type == 4) single_step = true;
            else if (event->type == 5) {
                tower_game_reset(&game, 0x544f5745U);
                paused = false;
            }
        }
        if (!paused || single_step) tower_game_update(&game);
    }

    rectangle(0, 0, HOST_WIDTH, HOST_HEIGHT, rgb565(12, 30, 35));
    DrawMosaicoTilemapLayer(map, 0, (Rectangle){0, 0, 480, 320});
    sprite(atlas, "reactor_core", 467, 354, 64, 0, WHITE);
    for (unsigned i = 0; i < TOWER_PAD_COUNT; ++i) {
        tower_slot_t *tower = &game.towers[i];
        sprite(atlas, "build_pad", tower->x, tower->y, 54, 0, WHITE);
        if (tower->occupied) {
            static const char *names[] = {"tower_pulse", "tower_rapid", "tower_frost"};
            sprite(atlas, names[tower->type], tower->x, tower->y - 4, 58, 0, WHITE);
        }
    }
    for (unsigned i = 0; i < TOWER_MAX_ENEMIES; ++i) {
        tower_enemy_t *enemy = &game.enemies[i];
        if (!enemy->active) continue;
        static const char *names[] = {"enemy_drone", "enemy_brute", "enemy_scout"};
        int size = enemy->kind == 1 ? 48 : enemy->kind == 2 ? 34 : 40;
        sprite(atlas, names[enemy->kind], enemy->x, enemy->y, size, 0,
               enemy->slow_ticks ? (Color){145, 205, 255, 255} : WHITE);
    }
    for (unsigned i = 0; i < TOWER_MAX_PROJECTILES; ++i) {
        tower_projectile_t *shot = &game.projectiles[i];
        if (!shot->active) continue;
        static const char *names[] = {"projectile_pulse", "projectile_rapid", "projectile_frost"};
        sprite(atlas, names[shot->kind], shot->x, shot->y, 16, 0, WHITE);
    }
    rectangle(0, 0, 480, 69, rgb565(5, 10, 20));
    rectangle(0, 65, 480, 4, rgb565(13, 72, 83));
    rectangle(0, 392, 480, 88, rgb565(3, 10, 16));
    *result = (host_result_t){frames, game.wave, game.score, game.credits,
                              game.base_hp, game.kills,
                              tower_game_state_hash(&game)};
    return 0;
}

int mosaico_tower_host_render(const char *asset_root, unsigned frames,
                              uint16_t *pixels, host_result_t *result)
{
    return mosaico_tower_host_render_replay(asset_root, frames, NULL, 0,
                                             pixels, result);
}
