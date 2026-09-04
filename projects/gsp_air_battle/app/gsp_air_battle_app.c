// SPDX-License-Identifier: Apache-2.0

#include "gsp_air_battle_app.h"

#include "gsp_air_battle_sprites.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* From gspc air_battle_binds.h (alphabetical bind names). */
#define GSP_BIND_FINAL_SCORE    0
#define GSP_BIND_LIVES          1
#define GSP_BIND_OVER_OVERLAY   2
#define GSP_BIND_PLAYFIELD      3
#define GSP_BIND_SCORE          4
#define GSP_BIND_TITLE_OVERLAY  5

#define FIELD_W           480
#define FIELD_H           480
#define TICK_MS           33
#define START_LIVES       3
#define INVULN_TICKS      45
#define MAX_STARS         48
#define MAX_ENEMIES       10
#define MAX_PBULLETS      14
#define MAX_EBULLETS      12
#define MAX_BURSTS        10

#define PLAYER_W          SPR_PLAYER_W
#define PLAYER_H          SPR_PLAYER_H

enum {
    STATE_TITLE = 0,
    STATE_PLAY,
    STATE_OVER,
};

enum {
    ENEMY_SMALL = 0,
    ENEMY_MID,
    ENEMY_BIG,
};

typedef struct {
    bool alive;
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t kind;
    uint8_t hp;
} actor_t;

typedef struct {
    bool alive;
    int16_t x;
    int16_t y;
    uint8_t kind;
    uint8_t age;
} burst_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t speed;
    uint8_t shade;
} star_t;

static uint32_t s_rng = 0xC0FFEEu;
static uint8_t s_state = STATE_TITLE;
static int16_t s_player_x = (FIELD_W - PLAYER_W) / 2;
static int16_t s_player_y = FIELD_H - PLAYER_H - 28;
static int32_t s_score;
static int32_t s_shown_score = -1;
static uint8_t s_lives = START_LIVES;
static int16_t s_invuln;
static uint16_t s_tick;
static uint16_t s_input_arm;
static uint16_t s_fire_cd;
static uint16_t s_spawn_cd;
static star_t s_stars[MAX_STARS];
static actor_t s_enemies[MAX_ENEMIES];
static actor_t s_pbullets[MAX_PBULLETS];
static actor_t s_ebullets[MAX_EBULLETS];
static burst_t s_bursts[MAX_BURSTS];
static char s_score_text[24];
static char s_lives_text[16];
static char s_final_text[24];

static uint32_t rng_u32(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

static int rng_range(int min, int max)
{
    return min + (int)(rng_u32() % (uint32_t)(max - min + 1));
}

static bool aabb(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static const gsp_sprite_t *enemy_sprite(uint8_t kind)
{
    if (kind == ENEMY_BIG) {
        return &gsp_spr_enemy_large;
    }
    if (kind == ENEMY_MID) {
        return &gsp_spr_enemy_medium;
    }
    return &gsp_spr_enemy_small;
}

static void enemy_stats(uint8_t kind, uint8_t *hp, int16_t *vy, int32_t *score)
{
    if (kind == ENEMY_BIG) {
        *hp = 8;
        *vy = 2;
        *score = 1000;
        return;
    }
    if (kind == ENEMY_MID) {
        *hp = 3;
        *vy = 3;
        *score = 300;
        return;
    }
    *hp = 1;
    *vy = 4;
    *score = 100;
}

static actor_t *alloc_actor(actor_t *pool, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!pool[i].alive) {
            return &pool[i];
        }
    }
    return NULL;
}

static void spawn_burst(int16_t x, int16_t y, uint8_t kind)
{
    burst_t *burst = NULL;
    for (size_t i = 0; i < MAX_BURSTS; ++i) {
        if (!s_bursts[i].alive) {
            burst = &s_bursts[i];
            break;
        }
    }
    if (burst == NULL) {
        return;
    }
    burst->alive = true;
    burst->x = x;
    burst->y = y;
    burst->kind = kind;
    burst->age = 0;
}

static void reset_wave(void)
{
    memset(s_enemies, 0, sizeof(s_enemies));
    memset(s_pbullets, 0, sizeof(s_pbullets));
    memset(s_ebullets, 0, sizeof(s_ebullets));
    memset(s_bursts, 0, sizeof(s_bursts));
    s_player_x = (FIELD_W - PLAYER_W) / 2;
    s_player_y = FIELD_H - PLAYER_H - 28;
    s_fire_cd = 0;
    s_spawn_cd = 20;
    s_invuln = 0;
}

static void start_run(esp_gsp_handle_t ui)
{
    s_state = STATE_PLAY;
    s_score = 0;
    s_lives = START_LIVES;
    reset_wave();
    (void)esp_gsp_set_visible(ui, GSP_BIND_TITLE_OVERLAY, false);
    (void)esp_gsp_set_visible(ui, GSP_BIND_OVER_OVERLAY, false);
}

static void game_over(esp_gsp_handle_t ui)
{
    s_state = STATE_OVER;
    snprintf(s_final_text, sizeof(s_final_text), "SCORE %ld", (long)s_score);
    (void)esp_gsp_set_text(ui, GSP_BIND_FINAL_SCORE, s_final_text);
    (void)esp_gsp_set_visible(ui, GSP_BIND_OVER_OVERLAY, true);
}

static void fire_player(void)
{
    actor_t *shot = alloc_actor(s_pbullets, MAX_PBULLETS);
    if (shot == NULL) {
        return;
    }
    shot->alive = true;
    shot->x = (int16_t)(s_player_x + PLAYER_W / 2 - 2);
    shot->y = (int16_t)(s_player_y - 8);
    shot->vx = 0;
    shot->vy = -12;
}

static void fire_enemy(const actor_t *enemy)
{
    actor_t *shot = alloc_actor(s_ebullets, MAX_EBULLETS);
    if (shot == NULL) {
        return;
    }
    const gsp_sprite_t *spr = enemy_sprite(enemy->kind);
    shot->alive = true;
    shot->x = (int16_t)(enemy->x + (int16_t)spr->w / 2 - 2);
    shot->y = (int16_t)(enemy->y + (int16_t)spr->h - 2);
    shot->vx = 0;
    shot->vy = 5;
}

static void spawn_enemy(void)
{
    actor_t *enemy = alloc_actor(s_enemies, MAX_ENEMIES);
    if (enemy == NULL) {
        return;
    }
    uint8_t kind = ENEMY_SMALL;
    uint32_t roll = rng_u32() % 100u;
    if (s_score >= 2500 && roll < 12u) {
        kind = ENEMY_BIG;
    } else if (s_score >= 400 && roll < 38u) {
        kind = ENEMY_MID;
    }
    uint8_t hp;
    int16_t vy;
    int32_t score;
    enemy_stats(kind, &hp, &vy, &score);
    (void)score;
    const gsp_sprite_t *spr = enemy_sprite(kind);
    int max_x = FIELD_W - (int)spr->w;
    if (max_x < 0) {
        max_x = 0;
    }
    enemy->alive = true;
    enemy->kind = kind;
    enemy->hp = hp;
    enemy->x = (int16_t)rng_range(4, max_x > 4 ? max_x : 4);
    enemy->y = (int16_t)(-(int)spr->h - rng_range(0, 40));
    enemy->vx = (int16_t)rng_range(-1, 1);
    enemy->vy = vy;
}

static void hit_player(esp_gsp_handle_t ui)
{
    if (s_invuln > 0) {
        return;
    }
    spawn_burst((int16_t)(s_player_x + PLAYER_W / 2),
                (int16_t)(s_player_y + PLAYER_H / 2), ENEMY_MID);
    if (s_lives == 0) {
        game_over(ui);
        return;
    }
    s_lives--;
    if (s_lives == 0) {
        game_over(ui);
        return;
    }
    s_invuln = INVULN_TICKS;
    s_player_x = (FIELD_W - PLAYER_W) / 2;
    s_player_y = FIELD_H - PLAYER_H - 28;
}

static void step_shots(actor_t *pool, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        actor_t *shot = &pool[i];
        if (!shot->alive) {
            continue;
        }
        shot->x = (int16_t)(shot->x + shot->vx);
        shot->y = (int16_t)(shot->y + shot->vy);
        if (shot->y < -16 || shot->y > FIELD_H + 8 ||
                shot->x < -8 || shot->x > FIELD_W + 8) {
            shot->alive = false;
        }
    }
}

static void step_play(esp_gsp_handle_t ui)
{
    if (s_fire_cd == 0) {
        fire_player();
        s_fire_cd = 4;
    } else {
        s_fire_cd--;
    }

    uint16_t spawn_every = 36;
    if (s_score >= 6000) {
        spawn_every = 14;
    } else if (s_score >= 2500) {
        spawn_every = 18;
    } else if (s_score >= 800) {
        spawn_every = 24;
    }
    if (s_spawn_cd == 0) {
        spawn_enemy();
        s_spawn_cd = spawn_every;
    } else {
        s_spawn_cd--;
    }

    step_shots(s_pbullets, MAX_PBULLETS);
    step_shots(s_ebullets, MAX_EBULLETS);

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        actor_t *enemy = &s_enemies[i];
        if (!enemy->alive) {
            continue;
        }
        enemy->x = (int16_t)(enemy->x + enemy->vx);
        enemy->y = (int16_t)(enemy->y + enemy->vy);
        const gsp_sprite_t *spr = enemy_sprite(enemy->kind);
        if (enemy->x < 0) {
            enemy->x = 0;
            enemy->vx = (int16_t)(-enemy->vx);
        } else if (enemy->x + (int16_t)spr->w > FIELD_W) {
            enemy->x = (int16_t)(FIELD_W - (int16_t)spr->w);
            enemy->vx = (int16_t)(-enemy->vx);
        }
        if (enemy->kind != ENEMY_SMALL && (s_tick % 38u) == (i % 38u) &&
                enemy->y > 8 && enemy->y < FIELD_H - 80) {
            fire_enemy(enemy);
        }
        if (enemy->y > FIELD_H + 8) {
            enemy->alive = false;
        }
    }

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        actor_t *enemy = &s_enemies[i];
        if (!enemy->alive) {
            continue;
        }
        const gsp_sprite_t *spr = enemy_sprite(enemy->kind);
        int eh = (int)spr->h - 10;
        int ew = (int)spr->w - 10;
        if (ew < 8) {
            ew = 8;
        }
        if (eh < 8) {
            eh = 8;
        }
        int ex = enemy->x + 5;
        int ey = enemy->y + 5;
        for (size_t b = 0; b < MAX_PBULLETS; ++b) {
            actor_t *shot = &s_pbullets[b];
            if (!shot->alive) {
                continue;
            }
            if (!aabb(shot->x, shot->y, 4, 10, ex, ey, ew, eh)) {
                continue;
            }
            shot->alive = false;
            if (enemy->hp > 1) {
                enemy->hp--;
            } else {
                int32_t prize;
                uint8_t hp;
                int16_t vy;
                enemy_stats(enemy->kind, &hp, &vy, &prize);
                s_score += prize;
                spawn_burst((int16_t)(enemy->x + (int16_t)spr->w / 2),
                            (int16_t)(enemy->y + (int16_t)spr->h / 2),
                            enemy->kind);
                enemy->alive = false;
            }
            break;
        }
        if (!enemy->alive || s_state != STATE_PLAY) {
            continue;
        }
        if (aabb(s_player_x + 8, s_player_y + 10, PLAYER_W - 16, PLAYER_H - 16,
                 ex, ey, ew, eh)) {
            enemy->alive = false;
            spawn_burst((int16_t)(enemy->x + (int16_t)spr->w / 2),
                        (int16_t)(enemy->y + (int16_t)spr->h / 2),
                        enemy->kind);
            hit_player(ui);
        }
    }

    if (s_state != STATE_PLAY) {
        return;
    }
    for (size_t i = 0; i < MAX_EBULLETS; ++i) {
        actor_t *shot = &s_ebullets[i];
        if (!shot->alive) {
            continue;
        }
        if (aabb(shot->x, shot->y, 4, 8, s_player_x + 10, s_player_y + 12,
                 PLAYER_W - 20, PLAYER_H - 18)) {
            shot->alive = false;
            hit_player(ui);
            if (s_state != STATE_PLAY) {
                return;
            }
        }
    }
}

static void step_stars(void)
{
    for (size_t i = 0; i < MAX_STARS; ++i) {
        unsigned y = (unsigned)s_stars[i].y + s_stars[i].speed;
        s_stars[i].y = (uint16_t)(y % FIELD_H);
    }
}

static void step_bursts(void)
{
    for (size_t i = 0; i < MAX_BURSTS; ++i) {
        if (!s_bursts[i].alive) {
            continue;
        }
        s_bursts[i].age++;
        if (s_bursts[i].age > 10) {
            s_bursts[i].alive = false;
        }
    }
}

static void refresh_hud(esp_gsp_handle_t ui)
{
    if (s_shown_score != s_score) {
        snprintf(s_score_text, sizeof(s_score_text), "SCORE %ld", (long)s_score);
        (void)esp_gsp_set_text(ui, GSP_BIND_SCORE, s_score_text);
        s_shown_score = s_score;
    }
    snprintf(s_lives_text, sizeof(s_lives_text), "LIVES %u", (unsigned)s_lives);
    (void)esp_gsp_set_text(ui, GSP_BIND_LIVES, s_lives_text);
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static uint16_t sky_pixel(int x, int y)
{
    unsigned t = (unsigned)y;
    unsigned r = 10u + t * 12u / FIELD_H;
    unsigned g = 16u + t * 18u / FIELD_H;
    unsigned b = 28u + t * 28u / FIELD_H;
    unsigned n = (unsigned)((x * 37 + y * 91) & 255);
    if (n > 253) {
        r = g = b = 180;
    }
    return rgb565(r, g, b);
}

static void put_rgb565(const esp_gsp_canvas_surface_t *surface,
                       int canvas_x, int canvas_y, uint16_t color)
{
    if (canvas_x < surface->x || canvas_y < surface->y ||
            canvas_x >= surface->x + surface->width ||
            canvas_y >= surface->y + surface->height) {
        return;
    }
    uint8_t *row = (uint8_t *)surface->pixels +
                   (size_t)(canvas_y - surface->y) * surface->stride_bytes;
    if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB888) {
        unsigned r = (unsigned)((color >> 11) & 0x1Fu) * 255u / 31u;
        unsigned g = (unsigned)((color >> 5) & 0x3Fu) * 255u / 63u;
        unsigned b = (unsigned)(color & 0x1Fu) * 255u / 31u;
        uint8_t *px = row + (size_t)(canvas_x - surface->x) * 3u;
        px[0] = (uint8_t)b;
        px[1] = (uint8_t)g;
        px[2] = (uint8_t)r;
        return;
    }
    memcpy(row + (size_t)(canvas_x - surface->x) * 2u, &color, sizeof(color));
}

static void fill_sky(const esp_gsp_canvas_surface_t *surface)
{
    for (uint16_t row = 0; row < surface->height; ++row) {
        int y = surface->y + row;
        for (uint16_t col = 0; col < surface->width; ++col) {
            int x = surface->x + col;
            put_rgb565(surface, x, y, sky_pixel(x, y));
        }
    }
    for (size_t i = 0; i < MAX_STARS; ++i) {
        uint16_t shade = (uint16_t)(120u + s_stars[i].shade);
        put_rgb565(surface, (int)s_stars[i].x, (int)s_stars[i].y,
                   rgb565(shade, shade, 200u));
    }
}

static void blit_sprite(const esp_gsp_canvas_surface_t *surface,
                        const gsp_sprite_t *spr, int x, int y)
{
    for (uint16_t sy = 0; sy < spr->h; ++sy) {
        int cy = y + (int)sy;
        if (cy < surface->y || cy >= surface->y + surface->height) {
            continue;
        }
        for (uint16_t sx = 0; sx < spr->w; ++sx) {
            uint8_t alpha = spr->alpha[sy * spr->w + sx];
            if (alpha < 24) {
                continue;
            }
            put_rgb565(surface, x + (int)sx, cy, spr->rgb[sy * spr->w + sx]);
        }
    }
}

static void fill_rect(const esp_gsp_canvas_surface_t *surface,
                      int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            put_rgb565(surface, xx, yy, color);
        }
    }
}

static void draw_bursts(const esp_gsp_canvas_surface_t *surface)
{
    for (size_t i = 0; i < MAX_BURSTS; ++i) {
        const burst_t *burst = &s_bursts[i];
        if (!burst->alive) {
            continue;
        }
        int radius = 6 + burst->age * 2;
        uint16_t color = burst->kind == ENEMY_BIG ?
                         rgb565(255, 180, 80) : rgb565(255, 220, 140);
        for (int dy = -radius; dy <= radius; dy += 2) {
            for (int dx = -radius; dx <= radius; dx += 2) {
                if (dx * dx + dy * dy <= radius * radius) {
                    put_rgb565(surface, burst->x + dx, burst->y + dy, color);
                }
            }
        }
    }
}

static void draw_playfield(const esp_gsp_canvas_surface_t *surface, void *user_ctx)
{
    (void)user_ctx;
    fill_sky(surface);
    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        if (s_enemies[i].alive) {
            blit_sprite(surface, enemy_sprite(s_enemies[i].kind),
                        s_enemies[i].x, s_enemies[i].y);
        }
    }
    for (size_t i = 0; i < MAX_PBULLETS; ++i) {
        if (s_pbullets[i].alive) {
            fill_rect(surface, s_pbullets[i].x, s_pbullets[i].y, 4, 10,
                      rgb565(160, 240, 255));
        }
    }
    for (size_t i = 0; i < MAX_EBULLETS; ++i) {
        if (s_ebullets[i].alive) {
            fill_rect(surface, s_ebullets[i].x, s_ebullets[i].y, 4, 8,
                      rgb565(255, 120, 80));
        }
    }
    bool hide_player = (s_state == STATE_PLAY && s_invuln > 0 &&
                        ((s_tick / 3u) & 1u) != 0);
    if (s_state != STATE_OVER && !hide_player) {
        blit_sprite(surface, &gsp_spr_player, s_player_x, s_player_y);
    } else if (s_state == STATE_OVER && s_lives > 0) {
        blit_sprite(surface, &gsp_spr_player, s_player_x, s_player_y);
    }
    draw_bursts(surface);
}

static void on_pointer(esp_gsp_handle_t ui, int32_t x, int32_t y,
                       bool pressed, void *user_ctx)
{
    (void)ui;
    (void)user_ctx;
    if (s_state != STATE_PLAY || !pressed) {
        return;
    }
    int nx = (int)x - PLAYER_W / 2;
    int ny = (int)y - PLAYER_H / 2;
    if (nx < 0) {
        nx = 0;
    } else if (nx > FIELD_W - PLAYER_W) {
        nx = FIELD_W - PLAYER_W;
    }
    if (ny < 36) {
        ny = 36;
    } else if (ny > FIELD_H - PLAYER_H - 4) {
        ny = FIELD_H - PLAYER_H - 4;
    }
    s_player_x = (int16_t)nx;
    s_player_y = (int16_t)ny;
}

static void on_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event,
                     void *user_ctx)
{
    (void)user_ctx;
    if (event == NULL || event->type != ESP_GSP_EVENT_CALL) {
        return;
    }
    /* GSP_ACT_ID_PLAY is 0; ignore the first frames so a zeroed startup
     * event cannot skip the title card. */
    if (s_input_arm < 12 || event->action_id != 0) {
        return;
    }
    if (s_state != STATE_PLAY) {
        start_run(ui);
    }
}

static void on_tick(esp_gsp_handle_t ui, void *user_ctx)
{
    (void)user_ctx;
    s_tick++;
    if (s_input_arm < 12) {
        s_input_arm++;
    }
    step_stars();
    step_bursts();
    if (s_state == STATE_TITLE) {
        (void)esp_gsp_set_visible(ui, GSP_BIND_TITLE_OVERLAY, true);
        (void)esp_gsp_set_visible(ui, GSP_BIND_OVER_OVERLAY, false);
    }
    if (s_state == STATE_PLAY) {
        if (s_invuln > 0) {
            s_invuln--;
        }
        step_play(ui);
    }
    refresh_hud(ui);
    (void)esp_gsp_canvas_invalidate(ui, GSP_BIND_PLAYFIELD);
}

esp_err_t gsp_app_start(esp_gsp_handle_t ui)
{
    for (size_t i = 0; i < MAX_STARS; ++i) {
        s_stars[i].x = (uint16_t)rng_range(0, FIELD_W - 1);
        s_stars[i].y = (uint16_t)rng_range(0, FIELD_H - 1);
        s_stars[i].speed = (uint8_t)rng_range(1, 3);
        s_stars[i].shade = (uint8_t)rng_range(40, 120);
    }
    reset_wave();
    s_state = STATE_TITLE;
    s_score = 0;
    s_lives = START_LIVES;
    s_input_arm = 0;

    if (esp_gsp_on_event(ui, on_event, NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    if (esp_gsp_set_pointer_observer(ui, on_pointer, NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    if (esp_gsp_canvas_set_draw_cb(ui, GSP_BIND_PLAYFIELD, draw_playfield,
                                   NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    (void)esp_gsp_set_visible(ui, GSP_BIND_TITLE_OVERLAY, true);
    (void)esp_gsp_set_visible(ui, GSP_BIND_OVER_OVERLAY, false);
    refresh_hud(ui);
    (void)esp_gsp_canvas_invalidate(ui, GSP_BIND_PLAYFIELD);

    void *timer = esp_gsp_timer_create(ui, TICK_MS, on_tick, NULL);
    return timer == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}
