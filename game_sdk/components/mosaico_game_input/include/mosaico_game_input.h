// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mosaico_game.h"

#ifdef __cplusplus
extern "C" {
#endif

bool mosaico_game_input_pointer(int32_t x, int32_t y, bool pressed, uint64_t timestamp_us);
/* A physical contact. value carries the controller's stable track ID. */
bool mosaico_game_input_touch(int32_t track_id, int32_t x, int32_t y,
                              bool pressed, uint64_t timestamp_us);
bool mosaico_game_input_button(int32_t button, bool pressed, uint64_t timestamp_us);
bool mosaico_game_input_joystick(int32_t x, int32_t y, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
