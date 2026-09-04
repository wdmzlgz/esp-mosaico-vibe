// SPDX-License-Identifier: Apache-2.0
#include "mosaico_game_2d.h"
#include <math.h>
#include <string.h>
#ifndef CONFIG_MOSAICO_GAME_MAX_TEXTURES
#define CONFIG_MOSAICO_GAME_MAX_TEXTURES 12
#endif
#define M2D_MAX_TEXTURES CONFIG_MOSAICO_GAME_MAX_TEXTURES
#define M2D_MAGIC 0x3141534dU
#define M2D_ATLAS_BINARY_ALPHA (1U<<0)
typedef struct __attribute__((packed)){uint32_t magic;uint16_t width,height,frame_count,flags;uint32_t rgb_bytes,alpha_bytes;} atlas_header_t;
typedef struct __attribute__((packed)){uint32_t id;uint16_t x,y,width,height;int16_t pivot_x,pivot_y;} atlas_frame_t;
typedef struct{bool used;mosaico_asset_view_t asset;const atlas_header_t *header;const atlas_frame_t *frames;const uint16_t *rgb;const uint8_t *alpha;} texture_slot_t;
static texture_slot_t s_textures[M2D_MAX_TEXTURES];
static uint16_t *s_target;static size_t s_stride;static int s_target_width,s_target_height;
static MosaicoSpriteFrame s_frame_result;
static texture_slot_t *texture_slot(Texture2D texture){if(!texture.id||texture.id>M2D_MAX_TEXTURES)return NULL;texture_slot_t *slot=&s_textures[texture.id-1];return slot->used?slot:NULL;}
void mosaico_game_2d_set_target(uint16_t *pixels,size_t stride,int width,int height){s_target=pixels;s_stride=stride;s_target_width=width;s_target_height=height;}
Texture2D Mosaico2DLoadTexture(const char *path){
 mosaico_asset_view_t asset={0};if(mosaico_game_asset_open(path,&asset)!=ESP_OK||asset.size<sizeof(atlas_header_t))return(Texture2D){0};
 const atlas_header_t *h=(const atlas_header_t*)asset.data;size_t fb=(size_t)h->frame_count*sizeof(atlas_frame_t),expected=sizeof(*h)+fb+h->rgb_bytes+h->alpha_bytes;
 if(h->magic!=M2D_MAGIC||!h->width||!h->height||expected>asset.size||h->rgb_bytes!=(uint32_t)h->width*h->height*2U)return(Texture2D){0};
 for(unsigned i=0;i<M2D_MAX_TEXTURES;++i)if(!s_textures[i].used){texture_slot_t *s=&s_textures[i];s->used=true;s->asset=asset;s->header=h;s->frames=(const atlas_frame_t*)(asset.data+sizeof(*h));s->rgb=(const uint16_t*)(asset.data+sizeof(*h)+fb);s->alpha=h->alpha_bytes?asset.data+sizeof(*h)+fb+h->rgb_bytes:NULL;return(Texture2D){.id=i+1U,.width=h->width,.height=h->height,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R5G6B5};}
 return(Texture2D){0};}
void Mosaico2DUnloadTexture(Texture2D texture){texture_slot_t*s=texture_slot(texture);if(s)memset(s,0,sizeof(*s));}
static inline uint16_t tint565(uint16_t p,Color t){if(t.r==255&&t.g==255&&t.b==255)return p;return(uint16_t)((((p>>11)&31U)*t.r/255U)<<11|(((p>>5)&63U)*t.g/255U)<<5|((p&31U)*t.b/255U));}
static inline uint16_t blend565(uint16_t d,uint16_t s,unsigned a){if(a>=255)return s;unsigned ia=255-a;return(uint16_t)(((((s&0xf81fU)*a+(d&0xf81fU)*ia)>>8)&0xf81fU)|((((s&0x07e0U)*a+(d&0x07e0U)*ia)>>8)&0x07e0U));}
void Mosaico2DDrawTexturePro(Texture2D texture,Rectangle source,Rectangle dest,Vector2 origin,float rotation,Color tint){
 texture_slot_t*s=texture_slot(texture);if(!s||!s_target||source.width==0||source.height==0||dest.width==0||dest.height==0||!tint.a)return;
 bool fx=source.width<0,fy=source.height<0;float sw=fabsf(source.width),sh=fabsf(source.height),rad=rotation*0.01745329252f,cs=cosf(rad),sn=sinf(rad);int dw=(int)fabsf(dest.width),dh=(int)fabsf(dest.height),extent=dw>dh?dw:dh;bool identity=fabsf(rotation)<.001f;
 int x0=identity?(int)(dest.x-origin.x):(int)(dest.x-origin.x-extent),y0=identity?(int)(dest.y-origin.y):(int)(dest.y-origin.y-extent),x1=identity?x0+dw:(int)(dest.x+extent),y1=identity?y0+dh:(int)(dest.y+extent);
 if(x0<0)x0=0;
 if(y0<0)y0=0;
 if(x1>s_target_width)x1=s_target_width;
 if(y1>s_target_height)y1=s_target_height;
 if(identity&&!fx&&!fy&&!s->alpha&&tint.r==255&&tint.g==255&&tint.b==255&&
    tint.a==255&&dw==(int)sw&&dh==(int)sh){
  int left=(int)(dest.x-origin.x),top=(int)(dest.y-origin.y);
  int sx=(int)source.x+(x0-left),sy=(int)source.y+(y0-top),copy=x1-x0;
  if(copy>0&&sx>=0&&sy>=0&&sx+copy<=s->header->width&&
     sy+(y1-y0)<=s->header->height){
   for(int y=y0;y<y1;++y,++sy)memcpy(&s_target[(size_t)y*s_stride+x0],
      &s->rgb[(size_t)sy*s->header->width+sx],(size_t)copy*sizeof(uint16_t));
   return;
  }
 }
 if(identity){
  int left=(int)(dest.x-origin.x),top=(int)(dest.y-origin.y),isw=(int)sw,ish=(int)sh;
  for(int y=y0;y<y1;++y){int ly=y-top;if(ly<0||ly>=dh)continue;int sy=ly*ish/dh;if(fy)sy=ish-1-sy;sy+=(int)source.y;if((unsigned)sy>=s->header->height)continue;
   for(int x=x0;x<x1;++x){int lx=x-left;if(lx<0||lx>=dw)continue;int sx=lx*isw/dw;if(fx)sx=isw-1-sx;sx+=(int)source.x;if((unsigned)sx>=s->header->width)continue;size_t i=(size_t)sy*s->header->width+sx;unsigned raw_a=s->alpha?s->alpha[i]:255U;if(!raw_a)continue;uint16_t*dst=&s_target[(size_t)y*s_stride+x];uint16_t src=tint565(s->rgb[i],tint);if((s->header->flags&M2D_ATLAS_BINARY_ALPHA)&&tint.a==255)*dst=src;else{unsigned a=raw_a*tint.a/255U;*dst=blend565(*dst,src,a);}}}
  return;
 }
 for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){float dx=x-dest.x,dy=y-dest.y;float lx=dx*cs+dy*sn+origin.x,ly=-dx*sn+dy*cs+origin.y;if(lx<0||ly<0||lx>=dw||ly>=dh)continue;int sx=(int)(lx*sw/dw),sy=(int)(ly*sh/dh);if(fx)sx=(int)sw-1-sx;if(fy)sy=(int)sh-1-sy;sx+=(int)source.x;sy+=(int)source.y;if((unsigned)sx>=s->header->width||(unsigned)sy>=s->header->height)continue;size_t i=(size_t)sy*s->header->width+sx;unsigned a=(s->alpha?s->alpha[i]:255U)*tint.a/255U;if(!a)continue;uint16_t*dst=&s_target[(size_t)y*s_stride+x];*dst=blend565(*dst,tint565(s->rgb[i],tint),a);}}
MosaicoAtlas LoadMosaicoAtlas(const char*path){Texture2D t=Mosaico2DLoadTexture(path);texture_slot_t*s=texture_slot(t);return(MosaicoAtlas){.texture=t,.descriptor=s?s->header:NULL,.frame_count=s?s->header->frame_count:0};}
const MosaicoSpriteFrame*MosaicoAtlasGetFrame(MosaicoAtlas a,mosaico_asset_id_t id){texture_slot_t*s=texture_slot(a.texture);if(!s)return NULL;for(uint16_t i=0;i<s->header->frame_count;++i){const atlas_frame_t*f=&s->frames[i];if(f->id==id){s_frame_result=(MosaicoSpriteFrame){.id=id,.source={(float)f->x,(float)f->y,(float)f->width,(float)f->height},.pivot={(float)f->pivot_x,(float)f->pivot_y}};return&s_frame_result;}}return NULL;}
esp_err_t mosaico_game_2d_atlas_get_frame(MosaicoAtlas a,mosaico_asset_id_t id,MosaicoSpriteFrame*out){
 if(!out)return ESP_ERR_INVALID_ARG;
 texture_slot_t*s=texture_slot(a.texture);
 if(!s)return ESP_ERR_INVALID_STATE;
 for(uint16_t i=0;i<s->header->frame_count;++i){const atlas_frame_t*f=&s->frames[i];if(f->id==id){*out=(MosaicoSpriteFrame){.id=id,.source={(float)f->x,(float)f->y,(float)f->width,(float)f->height},.pivot={(float)f->pivot_x,(float)f->pivot_y}};return ESP_OK;}}
 return ESP_ERR_NOT_FOUND;}
mosaico_asset_id_t MosaicoAnimationFrameAt(const mosaico_asset_id_t*frames,size_t count,uint32_t frame_ticks,uint32_t elapsed,bool loop){if(!frames||!count||!frame_ticks)return 0;size_t frame=elapsed/frame_ticks;if(loop)frame%=count;else if(frame>=count)frame=count-1;return frames[frame];}
void UnloadMosaicoAtlas(MosaicoAtlas a){Mosaico2DUnloadTexture(a.texture);}
