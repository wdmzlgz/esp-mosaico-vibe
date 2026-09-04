// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mosaico_game_assets.h"
#include "raylib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mosaico_tilemap_slot *MosaicoTilemap;
typedef struct {mosaico_asset_id_t id;int16_t x,y,width,height;uint32_t flags;} MosaicoMapObject;
MosaicoTilemap LoadMosaicoTilemap(const char *asset_path);
void DrawMosaicoTilemapLayer(MosaicoTilemap map,uint32_t layer_id,Rectangle viewport);
bool MosaicoTilemapIsBlocked(MosaicoTilemap map,int tile_x,int tile_y);
bool MosaicoTilemapFindObject(MosaicoTilemap map,mosaico_asset_id_t id,MosaicoMapObject *out);
bool MosaicoTilemapObjectAt(MosaicoTilemap map,size_t index,MosaicoMapObject *out);
size_t MosaicoTilemapPathPoints(MosaicoTilemap map,const Vector2 **out_points);
void UnloadMosaicoTilemap(MosaicoTilemap map);
#ifdef __cplusplus
}
#endif
