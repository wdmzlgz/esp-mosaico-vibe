// SPDX-License-Identifier: Apache-2.0
#include "mosaico_raylib_fast.h"

#include <stdint.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaico_game.h"
#include "mosaico_game_2d.h"
#include "mosaico_raylib_port.h"

static uint16_t *s_pixels;
static size_t s_stride;
static Camera2D s_camera;
static bool s_camera_active;

Vector2 MosaicoFastGetWorldToScreen2D(Vector2 position, Camera2D camera)
{
    float zoom = camera.zoom == 0 ? 1.0f : camera.zoom;
    float radians = camera.rotation * (3.14159265358979323846f / 180.0f);
    float cs = cosf(radians), sn = sinf(radians);
    float x = (position.x - camera.target.x) * zoom;
    float y = (position.y - camera.target.y) * zoom;
    return (Vector2){camera.offset.x + x*cs - y*sn,
                     camera.offset.y + x*sn + y*cs};
}

Vector2 MosaicoFastGetScreenToWorld2D(Vector2 position, Camera2D camera)
{
    float zoom = camera.zoom == 0 ? 1.0f : camera.zoom;
    float radians = -camera.rotation * (3.14159265358979323846f / 180.0f);
    float cs = cosf(radians), sn = sinf(radians);
    float x = position.x - camera.offset.x;
    float y = position.y - camera.offset.y;
    return (Vector2){camera.target.x + (x*cs - y*sn)/zoom,
                     camera.target.y + (x*sn + y*cs)/zoom};
}

static Vector2 active_to_screen(Vector2 position)
{
    return s_camera_active ? MosaicoFastGetWorldToScreen2D(position, s_camera)
                           : position;
}

void MosaicoFastBeginMode2D(Camera2D camera)
{
    s_camera = camera;
    if (s_camera.zoom == 0) s_camera.zoom = 1.0f;
    s_camera_active = true;
}

void MosaicoFastEndMode2D(void) { s_camera_active = false; }

static inline uint16_t rgb565(Color c)
{
    return (uint16_t)(((uint16_t)(c.r & 0xf8U) << 8) |
                      ((uint16_t)(c.g & 0xfcU) << 3) | (c.b >> 3));
}

static inline void put_pixel(int x, int y, Color color)
{
    if (!s_pixels || (unsigned)x >= MOSAICO_GAME_WIDTH ||
            (unsigned)y >= MOSAICO_GAME_HEIGHT) return;
    uint16_t *dst = &s_pixels[(size_t)y*s_stride + x];
    if (color.a == 255) {
        *dst = rgb565(color);
    } else if (color.a) {
        uint16_t old = *dst;
        unsigned a = color.a, ia = 255U - a;
        unsigned r = ((((old >> 11) & 31U) << 3)*ia + color.r*a)/255U;
        unsigned g = ((((old >> 5) & 63U) << 2)*ia + color.g*a)/255U;
        unsigned b = (((old & 31U) << 3)*ia + color.b*a)/255U;
        *dst = (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | (b >> 3));
    }
}

void MosaicoFastInitWindow(int width, int height, const char *title)
{
    (void)width; (void)height; (void)title;
}

bool MosaicoFastWindowShouldClose(void) { return false; }

void MosaicoFastBeginDrawing(void)
{
    s_pixels = NULL;
    s_stride = 0;
    (void)mosaico_raylib_port_begin_frame(&s_pixels, &s_stride);
    mosaico_game_2d_set_target(s_pixels, s_stride, MOSAICO_GAME_WIDTH,
                               MOSAICO_GAME_HEIGHT);
}

void MosaicoFastEndDrawing(void)
{
    if (s_pixels) (void)mosaico_raylib_port_present_frame();
    s_pixels = NULL;
    s_stride = 0;
    mosaico_game_2d_set_target(NULL, 0, 0, 0);
    s_camera_active = false;
}

Texture2D MosaicoFastLoadTexture(const char *asset_path)
{ return Mosaico2DLoadTexture(asset_path); }
void MosaicoFastUnloadTexture(Texture2D texture)
{ Mosaico2DUnloadTexture(texture); }
void MosaicoFastDrawTexturePro(Texture2D texture, Rectangle source,
                               Rectangle dest, Vector2 origin,
                               float rotation, Color tint)
{
    if (s_camera_active) {
        Vector2 screen = active_to_screen((Vector2){dest.x, dest.y});
        float zoom = s_camera.zoom;
        dest = (Rectangle){screen.x, screen.y, dest.width*zoom, dest.height*zoom};
        origin = (Vector2){origin.x*zoom, origin.y*zoom};
        rotation += s_camera.rotation;
    }
    Mosaico2DDrawTexturePro(texture, source, dest, origin, rotation, tint);
}
void MosaicoFastDrawTexture(Texture2D texture, int x, int y, Color tint)
{
    MosaicoFastDrawTexturePro(texture,(Rectangle){0,0,(float)texture.width,(float)texture.height},
        (Rectangle){(float)x,(float)y,(float)texture.width,(float)texture.height},
        (Vector2){0,0},0,tint);
}
void MosaicoFastDrawTextureV(Texture2D texture, Vector2 position, Color tint)
{ MosaicoFastDrawTexture(texture,(int)position.x,(int)position.y,tint); }
void MosaicoFastDrawTextureRec(Texture2D texture, Rectangle source,
                               Vector2 position, Color tint)
{
    MosaicoFastDrawTexturePro(texture,source,
        (Rectangle){position.x,position.y,fabsf(source.width),fabsf(source.height)},
        (Vector2){0,0},0,tint);
}

void MosaicoFastClearBackground(Color color)
{
    if (!s_pixels) return;
    uint16_t px = rgb565(color);
    uint32_t pair = (uint32_t)px | ((uint32_t)px << 16);
    for (int y = 0; y < MOSAICO_GAME_HEIGHT; ++y) {
        uint16_t *row = s_pixels + (size_t)y*s_stride;
        for (int x = 0; x < MOSAICO_GAME_WIDTH; x += 2) {
            memcpy(row + x, &pair, sizeof(pair));
        }
    }
}

void MosaicoFastDrawPixel(int x, int y, Color color)
{
    Vector2 p = active_to_screen((Vector2){(float)x, (float)y});
    put_pixel((int)p.x, (int)p.y, color);
}

void MosaicoFastDrawRectangle(int x, int y, int width, int height, Color color)
{
    if (s_camera_active) {
        Vector2 p = active_to_screen((Vector2){(float)x, (float)y});
        x = (int)p.x; y = (int)p.y;
        width = (int)(width*s_camera.zoom); height = (int)(height*s_camera.zoom);
    }
    if (!s_pixels || width <= 0 || height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > MOSAICO_GAME_WIDTH ? MOSAICO_GAME_WIDTH : x + width;
    int y1 = y + height > MOSAICO_GAME_HEIGHT ? MOSAICO_GAME_HEIGHT : y + height;
    if (x0 >= x1 || y0 >= y1) return;
    if (color.a != 255) {
        for (int yy=y0; yy<y1; ++yy) for (int xx=x0; xx<x1; ++xx)
            put_pixel(xx, yy, color);
        return;
    }
    uint16_t px = rgb565(color);
    uint32_t pair = (uint32_t)px | ((uint32_t)px << 16);
    for (int yy=y0; yy<y1; ++yy) {
        uint16_t *dst = s_pixels + (size_t)yy*s_stride + x0;
        int count = x1 - x0;
        if (((uintptr_t)dst & 3U) && count) { *dst++ = px; --count; }
        while (count >= 2) { memcpy(dst, &pair, sizeof(pair)); dst += 2; count -= 2; }
        if (count) *dst = px;
    }
}

void MosaicoFastDrawLine(int x0, int y0, int x1, int y1, Color color)
{
    Vector2 a = active_to_screen((Vector2){(float)x0, (float)y0});
    Vector2 b = active_to_screen((Vector2){(float)x1, (float)y1});
    x0=(int)a.x; y0=(int)a.y; x1=(int)b.x; y1=(int)b.y;
    int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1;
    int error=dx+dy;
    for (;;) {
        put_pixel(x0,y0,color);
        if (x0==x1 && y0==y1) break;
        int twice=2*error;
        if (twice>=dy) { error+=dy; x0+=sx; }
        if (twice<=dx) { error+=dx; y0+=sy; }
    }
}

void MosaicoFastDrawCircle(int center_x, int center_y, float radius, Color color)
{
    Vector2 center=active_to_screen((Vector2){(float)center_x,(float)center_y});
    int r=(int)(radius*(s_camera_active?s_camera.zoom:1.0f));
    int rr=r*r;
    for(int y=-r;y<=r;++y){
        int span=(int)sqrtf((float)(rr-y*y));
        for(int x=-span;x<=span;++x)put_pixel((int)center.x+x,(int)center.y+y,color);
    }
}

void MosaicoFastDrawRectangleLines(int x, int y, int width, int height,
                                   Color color)
{
    MosaicoFastDrawRectangle(x, y, width, 1, color);
    MosaicoFastDrawRectangle(x, y + height - 1, width, 1, color);
    MosaicoFastDrawRectangle(x, y + 1, 1, height - 2, color);
    MosaicoFastDrawRectangle(x + width - 1, y + 1, 1, height - 2, color);
}

static inline int edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (px-ax)*(by-ay) - (py-ay)*(bx-ax);
}

void MosaicoFastDrawTriangle(Vector2 av, Vector2 bv, Vector2 cv, Color color)
{
    av=active_to_screen(av); bv=active_to_screen(bv); cv=active_to_screen(cv);
    int ax=(int)av.x, ay=(int)av.y, bx=(int)bv.x, by=(int)bv.y;
    int cx=(int)cv.x, cy=(int)cv.y;
    int minx=ax<bx?(ax<cx?ax:cx):(bx<cx?bx:cx);
    int maxx=ax>bx?(ax>cx?ax:cx):(bx>cx?bx:cx);
    int miny=ay<by?(ay<cy?ay:cy):(by<cy?by:cy);
    int maxy=ay>by?(ay>cy?ay:cy):(by>cy?by:cy);
    if(minx<0) minx=0;
    if(miny<0) miny=0;
    if(maxx>=MOSAICO_GAME_WIDTH) maxx=MOSAICO_GAME_WIDTH-1;
    if(maxy>=MOSAICO_GAME_HEIGHT) maxy=MOSAICO_GAME_HEIGHT-1;
    int area=edge(ax,ay,bx,by,cx,cy);
    for(int y=miny;y<=maxy;++y) for(int x=minx;x<=maxx;++x){
        int w0=edge(bx,by,cx,cy,x,y), w1=edge(cx,cy,ax,ay,x,y);
        int w2=edge(ax,ay,bx,by,x,y);
        if((area>=0&&w0>=0&&w1>=0&&w2>=0)||
           (area<0&&w0<=0&&w1<=0&&w2<=0)) put_pixel(x,y,color);
    }
}

static const uint8_t DIGITS[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};
static const uint8_t LETTERS[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

static const uint8_t *glyph(char ch)
{
    static const uint8_t slash[7]={1,2,2,4,8,8,16};
    static const uint8_t dash[7]={0,0,0,31,0,0,0};
    static const uint8_t colon[7]={0,4,4,0,4,4,0};
    static const uint8_t dot[7]={0,0,0,0,0,6,6};
    if(ch>='0'&&ch<='9') return DIGITS[ch-'0'];
    if(ch>='A'&&ch<='Z') return LETTERS[ch-'A'];
    if(ch>='a'&&ch<='z') return LETTERS[ch-'a'];
    if(ch=='/') return slash;
    if(ch=='-') return dash;
    if(ch==':') return colon;
    if(ch=='.') return dot;
    return NULL;
}

void MosaicoFastDrawText(const char *text, int x, int y, int font_size,
                         Color color)
{
    if(!text) return;
    bool restore_camera=s_camera_active;
    if(s_camera_active){Vector2 p=active_to_screen((Vector2){(float)x,(float)y});x=(int)p.x;y=(int)p.y;font_size=(int)(font_size*s_camera.zoom);s_camera_active=false;}
    int scale=font_size/8; if(scale<1)scale=1;
    for(;*text;++text,x+=6*scale){
        const uint8_t *rows=glyph(*text); if(!rows) continue;
        for(int yy=0;yy<7;++yy) for(int xx=0;xx<5;++xx)
            if(rows[yy]&(1U<<(4-xx)))
                MosaicoFastDrawRectangle(x+xx*scale,y+yy*scale,scale,scale,color);
    }
    s_camera_active=restore_camera;
}

int MosaicoFastMeasureText(const char *text, int font_size)
{
    if(!text||!*text) return 0;
    int scale=font_size/8; if(scale<1)scale=1;
    return (int)strlen(text)*6*scale-scale;
}

const char *MosaicoFastTextFormat(const char *format, ...)
{
    static char buffers[2][64];
    static unsigned index;
    char *out = buffers[index++ & 1U];
    va_list args;
    va_start(args, format);
    vsnprintf(out, sizeof(buffers[0]), format, args);
    va_end(args);
    return out;
}

bool MosaicoFastCheckCollisionRecs(Rectangle a, Rectangle b)
{
    return a.x < b.x+b.width && a.x+a.width > b.x &&
           a.y < b.y+b.height && a.y+a.height > b.y;
}

bool MosaicoFastCheckCollisionCircles(Vector2 a,float ar,Vector2 b,float br)
{
    float dx=a.x-b.x,dy=a.y-b.y,r=ar+br;
    return dx*dx+dy*dy <= r*r;
}

bool MosaicoFastCheckCollisionPointRec(Vector2 p,Rectangle r)
{
    return p.x>=r.x && p.x<=r.x+r.width && p.y>=r.y && p.y<=r.y+r.height;
}
