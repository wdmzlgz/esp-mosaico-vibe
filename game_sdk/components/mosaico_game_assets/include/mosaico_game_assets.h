// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t mosaico_asset_id_t;
typedef enum {
    MOSAICO_ASSET_BINARY=0, MOSAICO_ASSET_PNG, MOSAICO_ASSET_FONT,
    MOSAICO_ASSET_AUDIO, MOSAICO_ASSET_ATLAS, MOSAICO_ASSET_TILEMAP
} mosaico_asset_type_t;
typedef struct { mosaico_asset_id_t id; mosaico_asset_type_t type; const uint8_t *data; size_t size; } mosaico_asset_t;

typedef struct {
    mosaico_asset_id_t id;
    const char *name;
    const uint8_t *data;
    size_t size;
} mosaico_asset_view_t;

typedef struct {
    const char *partition_label;
    int max_files;
    uint16_t checksum;
    bool mmap_enable;
} mosaico_asset_store_config_t;

const mosaico_asset_t *mosaico_game_asset_find(const mosaico_asset_t *assets,size_t count,mosaico_asset_id_t id);
mosaico_asset_id_t mosaico_game_asset_id(const char *name);
esp_err_t mosaico_game_assets_mount(const mosaico_asset_store_config_t *config);
esp_err_t mosaico_game_asset_register_memory(const char *name,
                                              const void *data, size_t size);
void mosaico_game_assets_unmount(void);
bool mosaico_game_assets_is_mounted(void);
esp_err_t mosaico_game_asset_open(const char *name, mosaico_asset_view_t *out);
esp_err_t mosaico_game_asset_open_id(mosaico_asset_id_t id,
                                     mosaico_asset_view_t *out);

#ifdef __cplusplus
}
#endif
