// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLATFORM_COIN_COUNT 8
#define PLATFORM_ENEMY_COUNT 3

typedef enum {
    PLATFORM_START,
    PLATFORM_PLAYING,
    PLATFORM_PAUSED,
    PLATFORM_WON,
    PLATFORM_GAME_OVER,
} platform_phase_t;

typedef enum {
    PLATFORM_ACTION_LEFT,
    PLATFORM_ACTION_RIGHT,
    PLATFORM_ACTION_JUMP,
    PLATFORM_ACTION_PAUSE,
    PLATFORM_ACTION_RESTART,
} platform_action_t;

typedef struct {
    float x, y;
    bool collected;
} platform_coin_t;

typedef struct {
    float x, y, left, right, speed;
    bool active;
} platform_enemy_t;

typedef struct {
    platform_phase_t phase;
    float player_x, player_y, velocity_x, velocity_y;
    float camera_x;
    bool move_left, move_right, jump_held, grounded;
    uint32_t tick;
    uint32_t phase_tick;
    uint16_t score;
    uint8_t lives;
    platform_coin_t coins[PLATFORM_COIN_COUNT];
    platform_enemy_t enemies[PLATFORM_ENEMY_COUNT];
} platform_game_t;

typedef struct { float x, y, width, height; } platform_block_t;

void platform_game_reset(platform_game_t *game);
void platform_game_set_action(platform_game_t *game, platform_action_t action,
                              bool pressed);
void platform_game_set_pointer(platform_game_t *game, float x, float y, bool pressed);
void platform_game_update(platform_game_t *game);
const platform_block_t *platform_game_blocks(size_t *count);
uint32_t platform_game_state_hash(const platform_game_t *game);
