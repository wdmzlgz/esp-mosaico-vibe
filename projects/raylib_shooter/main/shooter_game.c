// SPDX-License-Identifier: Apache-2.0
#include "shooter_game.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static uint32_t next_random(shooter_game_t *game)
{
    uint32_t x = game->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return game->rng = x ? x : 0x6d2b79f5U;
}

static bool overlaps(const shooter_actor_t *a, float aw, float ah,
                     const shooter_actor_t *b, float bw, float bh)
{
    return a->x < b->x + bw && a->x + aw > b->x &&
           a->y < b->y + bh && a->y + ah > b->y;
}

void shooter_game_reset(shooter_game_t *game, uint32_t seed)
{
    memset(game, 0, sizeof(*game));
    game->phase = SHOOTER_START;
    game->player = (shooter_actor_t){.x=222, .y=420, .active=true};
    game->lives = 3;
    game->rng = seed ? seed : 1;
}

void shooter_game_set_pointer(shooter_game_t *game, float x, float y, bool pressed)
{
    if (!pressed) return;
    if (game->phase == SHOOTER_START || game->phase == SHOOTER_GAME_OVER) {
        uint32_t seed = game->rng;
        shooter_game_reset(game, seed);
        game->phase = SHOOTER_PLAYING;
    }
    if (game->phase == SHOOTER_PLAYING) {
        game->player.x = fmaxf(0, fminf(444, x - 18));
        game->player.y = fmaxf(280, fminf(438, y - 18));
    }
}

void shooter_game_toggle_pause(shooter_game_t *game)
{
    if (game->phase == SHOOTER_PLAYING) game->phase = SHOOTER_PAUSED;
    else if (game->phase == SHOOTER_PAUSED) game->phase = SHOOTER_PLAYING;
}

static void spawn_bullet(shooter_game_t *game)
{
    for (size_t i=0; i<SHOOTER_MAX_BULLETS; ++i) if (!game->bullets[i].active) {
        game->bullets[i] = (shooter_actor_t){.x=game->player.x+15,
            .y=game->player.y-12, .vy=-5.33f, .active=true};
        ++game->shots_fired;
        return;
    }
}

static void spawn_enemy(shooter_game_t *game)
{
    for (size_t i=0; i<SHOOTER_MAX_ENEMIES; ++i) if (!game->enemies[i].active) {
        uint32_t value=next_random(game); uint8_t kind=value%3U;
        game->enemies[i]=(shooter_actor_t){.x=12+(float)(value%420U), .y=-32,
            .vx=kind==1?((value>>9)&1?0.8f:-0.8f):0,
            .vy=1.0f+kind*0.47f, .kind=kind, .active=true};
        return;
    }
}

void shooter_game_update(shooter_game_t *game)
{
    if (game->phase != SHOOTER_PLAYING) return;
    ++game->tick;
    if (!game->fire_cooldown) { spawn_bullet(game); game->fire_cooldown=10; }
    else --game->fire_cooldown;
    if (!game->spawn_cooldown) { spawn_enemy(game); game->spawn_cooldown=11+next_random(game)%18U; }
    else --game->spawn_cooldown;
    for (size_t i=0; i<SHOOTER_MAX_BULLETS; ++i) {
        shooter_actor_t *b=&game->bullets[i]; if (!b->active) continue;
        b->y+=b->vy; if (b->y < -12) b->active=false;
    }
    for (size_t i=0; i<SHOOTER_MAX_ENEMIES; ++i) {
        shooter_actor_t *e=&game->enemies[i]; if (!e->active) continue;
        e->x+=e->vx; e->y+=e->vy;
        if (e->x<0 || e->x>450) e->vx=-e->vx;
        if (e->y>490 || overlaps(e,28,28,&game->player,36,36)) {
            e->active=false;
            if (game->lives && --game->lives==0) game->phase=SHOOTER_GAME_OVER;
            continue;
        }
        for (size_t j=0; j<SHOOTER_MAX_BULLETS; ++j) {
            shooter_actor_t *b=&game->bullets[j];
            if (b->active && overlaps(e,28,28,b,6,12)) {
                e->active=b->active=false; game->score+=10U*(e->kind+1U); break;
            }
        }
    }
}

uint32_t shooter_game_state_hash(const shooter_game_t *game)
{
    const uint8_t *bytes=(const uint8_t *)game; uint32_t hash=2166136261U;
    for (size_t i=0; i<sizeof(*game); ++i) hash=(hash^bytes[i])*16777619U;
    return hash;
}
