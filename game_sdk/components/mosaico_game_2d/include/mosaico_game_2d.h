// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "mosaico_game_assets.h"
#include "raylib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { mosaico_asset_id_t id; Rectangle source; Vector2 pivot; } MosaicoSpriteFrame;
typedef struct { Texture2D texture; const void *descriptor; uint16_t frame_count; } MosaicoAtlas;
void mosaico_game_2d_set_target(uint16_t *pixels,size_t stride,int width,int height);
MosaicoAtlas LoadMosaicoAtlas(const char *asset_path);
const MosaicoSpriteFrame *MosaicoAtlasGetFrame(MosaicoAtlas atlas,mosaico_asset_id_t frame_id);
esp_err_t mosaico_game_2d_atlas_get_frame(MosaicoAtlas atlas,
    mosaico_asset_id_t frame_id,MosaicoSpriteFrame *out_frame);
mosaico_asset_id_t MosaicoAnimationFrameAt(const mosaico_asset_id_t *frames,
    size_t frame_count,uint32_t frame_ticks,uint32_t elapsed_ticks,bool loop);
void UnloadMosaicoAtlas(MosaicoAtlas atlas);
Texture2D Mosaico2DLoadTexture(const char *asset_path);
void Mosaico2DUnloadTexture(Texture2D texture);
void Mosaico2DDrawTexturePro(Texture2D texture,Rectangle source,Rectangle dest,Vector2 origin,float rotation,Color tint);
#ifdef __cplusplus
}
#endif
