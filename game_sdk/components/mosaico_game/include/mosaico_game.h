// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOSAICO_GAME_API_VERSION 2U
#define MOSAICO_GAME_WIDTH 480
#define MOSAICO_GAME_HEIGHT 480
#define MOSAICO_GAME_DEFAULT_FPS CONFIG_MOSAICO_GAME_DEFAULT_FPS

typedef struct {
    int width;
    int height;
    int target_fps;
    size_t game_task_stack;
} mosaico_game_config_t;

#define MOSAICO_GAME_CONFIG_DEFAULT() { \
    .width = MOSAICO_GAME_WIDTH, \
    .height = MOSAICO_GAME_HEIGHT, \
    .target_fps = MOSAICO_GAME_DEFAULT_FPS, \
    .game_task_stack = 12288U, \
}

typedef enum {
    MOSAICO_DEVICE_EVENT_NONE = 0,
    MOSAICO_DEVICE_EVENT_POINTER,
    MOSAICO_DEVICE_EVENT_TOUCH,
    MOSAICO_DEVICE_EVENT_BUTTON,
    MOSAICO_DEVICE_EVENT_JOYSTICK,
    MOSAICO_DEVICE_EVENT_IMU,
    MOSAICO_DEVICE_EVENT_ATTACHED,
    MOSAICO_DEVICE_EVENT_DETACHED,
} mosaico_device_event_type_t;

typedef struct {
    mosaico_device_event_type_t type;
    int32_t x;
    int32_t y;
    int32_t value;
    bool pressed;
    uint64_t timestamp_us;
} mosaico_device_event_t;

typedef struct {
    uint32_t frames;
    uint32_t dropped_frames;
    float fps;
    uint32_t update_us;
    uint32_t render_us;
    uint32_t present_us;
    size_t free_internal_bytes;
    size_t free_psram_bytes;
} mosaico_game_stats_t;

esp_err_t MosaicoGameInit(const mosaico_game_config_t *config);
/* Register before esp_iris_start() so Recovery-first installs can verify the
 * active layout and persisted update result after the game boots. */
esp_err_t MosaicoGameRegisterSystemInventory(void);
void MosaicoGameShutdown(void);
bool MosaicoGamePollDeviceEvent(mosaico_device_event_t *event);
bool MosaicoGamePostDeviceEvent(const mosaico_device_event_t *event);
void MosaicoGameGetStats(mosaico_game_stats_t *stats);
void MosaicoGameRecordFrame(uint32_t update_us, uint32_t render_us,
                            uint32_t present_us, bool dropped);
void MosaicoGameRecordTiming(uint32_t update_us, uint32_t render_us);
const mosaico_game_config_t *MosaicoGameGetConfig(void);

#ifdef __cplusplus
}
#endif
