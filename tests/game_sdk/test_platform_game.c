#include <assert.h>
#include <stdio.h>
#include "platform_game.h"

int main(void)
{
    platform_game_t game;
    platform_game_reset(&game);
    assert(game.phase == PLATFORM_START && game.lives == 3);
    platform_game_set_pointer(&game, 220, 430, true);
    assert(game.phase == PLATFORM_PLAYING && game.move_right);
    for (int i = 0; i < 20; ++i) platform_game_update(&game);
    assert(game.player_x > 42);
    platform_game_set_pointer(&game, 350, 430, true);
    platform_game_update(&game);
    assert(game.velocity_y < 0 && !game.grounded);
    platform_game_set_action(&game, PLATFORM_ACTION_PAUSE, true);
    assert(game.phase == PLATFORM_PAUSED);
    uint32_t paused_tick = game.tick;
    platform_game_update(&game);
    assert(game.tick == paused_tick);
    platform_game_set_action(&game, PLATFORM_ACTION_PAUSE, true);
    assert(game.phase == PLATFORM_PLAYING);
    assert(platform_game_state_hash(&game) != 0);
    puts("platform game model: ok");
    return 0;
}
