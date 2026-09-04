// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Include the public Raylib types first, then redirect the common embedded 2D
 * API to Mosaico's RGB565 fast path. Game source keeps the familiar Raylib API
 * while avoiding the generic software-OpenGL rasterizer on device. */
#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

void MosaicoFastInitWindow(int width, int height, const char *title);
bool MosaicoFastWindowShouldClose(void);
void MosaicoFastBeginDrawing(void);
void MosaicoFastEndDrawing(void);
void MosaicoFastBeginMode2D(Camera2D camera);
void MosaicoFastEndMode2D(void);
Vector2 MosaicoFastGetWorldToScreen2D(Vector2 position, Camera2D camera);
Vector2 MosaicoFastGetScreenToWorld2D(Vector2 position, Camera2D camera);
void MosaicoFastClearBackground(Color color);
void MosaicoFastDrawPixel(int x, int y, Color color);
void MosaicoFastDrawLine(int start_x, int start_y, int end_x, int end_y,
                         Color color);
void MosaicoFastDrawCircle(int center_x, int center_y, float radius,
                           Color color);
void MosaicoFastDrawRectangle(int x, int y, int width, int height, Color color);
void MosaicoFastDrawRectangleLines(int x, int y, int width, int height,
                                   Color color);
void MosaicoFastDrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color color);
Texture2D MosaicoFastLoadTexture(const char *asset_path);
void MosaicoFastUnloadTexture(Texture2D texture);
void MosaicoFastDrawTexture(Texture2D texture, int x, int y, Color tint);
void MosaicoFastDrawTextureV(Texture2D texture, Vector2 position, Color tint);
void MosaicoFastDrawTextureRec(Texture2D texture, Rectangle source,
                               Vector2 position, Color tint);
void MosaicoFastDrawTexturePro(Texture2D texture, Rectangle source,
                               Rectangle dest, Vector2 origin,
                               float rotation, Color tint);
void MosaicoFastDrawText(const char *text, int x, int y, int font_size,
                         Color color);
int MosaicoFastMeasureText(const char *text, int font_size);
const char *MosaicoFastTextFormat(const char *format, ...);
bool MosaicoFastCheckCollisionRecs(Rectangle first, Rectangle second);
bool MosaicoFastCheckCollisionCircles(Vector2 first, float first_radius,
                                      Vector2 second, float second_radius);
bool MosaicoFastCheckCollisionPointRec(Vector2 point, Rectangle rectangle);

#ifdef __cplusplus
}
#endif

#define InitWindow MosaicoFastInitWindow
#define WindowShouldClose MosaicoFastWindowShouldClose
#define BeginDrawing MosaicoFastBeginDrawing
#define EndDrawing MosaicoFastEndDrawing
#define BeginMode2D MosaicoFastBeginMode2D
#define EndMode2D MosaicoFastEndMode2D
#define GetWorldToScreen2D MosaicoFastGetWorldToScreen2D
#define GetScreenToWorld2D MosaicoFastGetScreenToWorld2D
#define ClearBackground MosaicoFastClearBackground
#define DrawPixel MosaicoFastDrawPixel
#define DrawLine MosaicoFastDrawLine
#define DrawCircle MosaicoFastDrawCircle
#define DrawRectangle MosaicoFastDrawRectangle
#define DrawRectangleLines MosaicoFastDrawRectangleLines
#define DrawTriangle MosaicoFastDrawTriangle
#define LoadTexture MosaicoFastLoadTexture
#define UnloadTexture MosaicoFastUnloadTexture
#define DrawTexture MosaicoFastDrawTexture
#define DrawTextureV MosaicoFastDrawTextureV
#define DrawTextureRec MosaicoFastDrawTextureRec
#define DrawTexturePro MosaicoFastDrawTexturePro
#define DrawText MosaicoFastDrawText
#define MeasureText MosaicoFastMeasureText
#define TextFormat MosaicoFastTextFormat
#define CheckCollisionRecs MosaicoFastCheckCollisionRecs
#define CheckCollisionCircles MosaicoFastCheckCollisionCircles
#define CheckCollisionPointRec MosaicoFastCheckCollisionPointRec
