// SPDX-License-Identifier: Apache-2.0
#include "platform_game.h"

#include <string.h>

#define PLAYER_W 28.0f
#define PLAYER_H 36.0f
#define WORLD_W 1440.0f
#define FLOOR_Y 390.0f

static const platform_block_t BLOCKS[] = {
    {0, FLOOR_Y, 1440, 90}, {170, 330, 110, 20}, {340, 285, 100, 20},
    {500, 345, 125, 20}, {690, 300, 100, 20}, {850, 250, 110, 20},
    {1030, 325, 130, 20}, {1215, 275, 105, 20},
};

static bool overlap(float ax, float ay, float aw, float ah,
                    float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

const platform_block_t *platform_game_blocks(size_t *count)
{
    if (count) *count = sizeof(BLOCKS) / sizeof(BLOCKS[0]);
    return BLOCKS;
}

void platform_game_reset(platform_game_t *game)
{
    memset(game, 0, sizeof(*game));
    game->phase = PLATFORM_START;
    game->player_x = 42;
    game->player_y = FLOOR_Y - PLAYER_H;
    game->grounded = true;
    game->lives = 3;
    const float coin_x[PLATFORM_COIN_COUNT] = {215, 385, 545, 735, 900, 1080, 1260, 1370};
    const float coin_y[PLATFORM_COIN_COUNT] = {295, 250, 310, 265, 215, 290, 240, 350};
    for (size_t i = 0; i < PLATFORM_COIN_COUNT; ++i)
        game->coins[i] = (platform_coin_t){coin_x[i], coin_y[i], false};
    game->enemies[0] = (platform_enemy_t){560, 362, 500, 650, 1.1f, true};
    game->enemies[1] = (platform_enemy_t){930, 362, 850, 1010, -1.25f, true};
    game->enemies[2] = (platform_enemy_t){1240, 362, 1180, 1360, 1.4f, true};
}

static void set_phase(platform_game_t *game, platform_phase_t phase)
{
    game->phase = phase;
    game->phase_tick = 0;
}

void platform_game_set_action(platform_game_t *game, platform_action_t action,
                              bool pressed)
{
    if (action == PLATFORM_ACTION_RESTART && pressed) {
        platform_game_reset(game);
        set_phase(game, PLATFORM_PLAYING);
        return;
    }
    if (action == PLATFORM_ACTION_PAUSE && pressed) {
        if (game->phase == PLATFORM_PLAYING) {
            game->move_left = game->move_right = game->jump_held = false;
            set_phase(game, PLATFORM_PAUSED);
        }
        else if (game->phase == PLATFORM_PAUSED) set_phase(game, PLATFORM_PLAYING);
        return;
    }
    if (game->phase != PLATFORM_PLAYING) return;
    if (action == PLATFORM_ACTION_LEFT) game->move_left = pressed;
    if (action == PLATFORM_ACTION_RIGHT) game->move_right = pressed;
    if (action == PLATFORM_ACTION_JUMP) {
        if (pressed && !game->jump_held && game->grounded) {
            game->velocity_y = -10.5f;
            game->grounded = false;
        }
        game->jump_held = pressed;
    }
}

void platform_game_set_pointer(platform_game_t *game, float x, float y, bool pressed)
{
    if (!pressed) {
        platform_game_set_action(game, PLATFORM_ACTION_LEFT, false);
        platform_game_set_action(game, PLATFORM_ACTION_RIGHT, false);
        platform_game_set_action(game, PLATFORM_ACTION_JUMP, false);
        return;
    }
    if (game->phase == PLATFORM_PAUSED) {
        platform_game_set_action(game, PLATFORM_ACTION_PAUSE, true);
        return;
    }
    if (game->phase != PLATFORM_PLAYING) {
        platform_game_set_action(game, PLATFORM_ACTION_RESTART, true);
    }
    if (y < 54 && x > 420) {
        platform_game_set_action(game, PLATFORM_ACTION_PAUSE, true);
        return;
    }
    if (y < 360) return;
    platform_game_set_action(game, PLATFORM_ACTION_LEFT, x < 150);
    platform_game_set_action(game, PLATFORM_ACTION_RIGHT, x >= 150 && x < 300);
    platform_game_set_action(game, PLATFORM_ACTION_JUMP, x >= 300);
}

static void lose_life(platform_game_t *game)
{
    if (game->lives) --game->lives;
    if (!game->lives) {
        set_phase(game, PLATFORM_GAME_OVER);
        return;
    }
    game->player_x = 42;
    game->player_y = FLOOR_Y - PLAYER_H;
    game->velocity_x = game->velocity_y = 0;
    game->camera_x = 0;
}

void platform_game_update(platform_game_t *game)
{
    ++game->phase_tick;
    if (game->phase != PLATFORM_PLAYING) return;
    ++game->tick;
    float desired = game->move_left ? -4.0f : game->move_right ? 4.0f : 0.0f;
    game->velocity_x += (desired - game->velocity_x) * (desired ? 0.35f : 0.55f);

    float next_x = game->player_x + game->velocity_x;
    if (next_x < 0) next_x = 0;
    if (next_x > WORLD_W - PLAYER_W) next_x = WORLD_W - PLAYER_W;
    for (size_t i = 1; i < sizeof(BLOCKS)/sizeof(BLOCKS[0]); ++i) {
        const platform_block_t *b = &BLOCKS[i];
        if (!overlap(next_x, game->player_y, PLAYER_W, PLAYER_H,
                     b->x, b->y, b->width, b->height)) continue;
        if (game->velocity_x > 0) next_x = b->x - PLAYER_W;
        else if (game->velocity_x < 0) next_x = b->x + b->width;
        game->velocity_x = 0;
    }
    game->player_x = next_x;

    float old_bottom = game->player_y + PLAYER_H;
    game->velocity_y += 0.62f;
    if (game->velocity_y > 13) game->velocity_y = 13;
    float next_y = game->player_y + game->velocity_y;
    game->grounded = false;
    for (size_t i = 0; i < sizeof(BLOCKS)/sizeof(BLOCKS[0]); ++i) {
        const platform_block_t *b = &BLOCKS[i];
        if (!overlap(game->player_x, next_y, PLAYER_W, PLAYER_H,
                     b->x, b->y, b->width, b->height)) continue;
        if (game->velocity_y >= 0 && old_bottom <= b->y + 2) {
            next_y = b->y - PLAYER_H;
            game->velocity_y = 0;
            game->grounded = true;
        } else if (game->velocity_y < 0 && game->player_y >= b->y + b->height - 2) {
            next_y = b->y + b->height;
            game->velocity_y = 0;
        }
    }
    game->player_y = next_y;

    for (size_t i = 0; i < PLATFORM_COIN_COUNT; ++i) {
        platform_coin_t *coin = &game->coins[i];
        if (!coin->collected && overlap(game->player_x, game->player_y,
                PLAYER_W, PLAYER_H, coin->x - 8, coin->y - 8, 16, 16)) {
            coin->collected = true;
            game->score += 100;
        }
    }
    for (size_t i = 0; i < PLATFORM_ENEMY_COUNT; ++i) {
        platform_enemy_t *enemy = &game->enemies[i];
        if (!enemy->active) continue;
        enemy->x += enemy->speed;
        if (enemy->x < enemy->left || enemy->x > enemy->right) enemy->speed = -enemy->speed;
        if (!overlap(game->player_x, game->player_y, PLAYER_W, PLAYER_H,
                     enemy->x, enemy->y, 28, 28)) continue;
        if (game->velocity_y > 0 && game->player_y + PLAYER_H < enemy->y + 16) {
            enemy->active = false;
            game->velocity_y = -7.5f;
            game->score += 250;
        } else {
            lose_life(game);
            return;
        }
    }
    if (game->player_y > 500) lose_life(game);
    if (game->player_x > 1380) set_phase(game, PLATFORM_WON);
    float target_camera = game->player_x - 190;
    if (target_camera < 0) target_camera = 0;
    if (target_camera > WORLD_W - 480) target_camera = WORLD_W - 480;
    game->camera_x += (target_camera - game->camera_x) * 0.15f;
}

uint32_t platform_game_state_hash(const platform_game_t *game)
{
    const uint8_t *bytes = (const uint8_t *)game;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < sizeof(*game); ++i) hash = (hash ^ bytes[i]) * 16777619U;
    return hash;
}
