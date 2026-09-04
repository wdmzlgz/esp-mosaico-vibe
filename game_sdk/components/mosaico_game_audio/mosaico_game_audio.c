// SPDX-License-Identifier: Apache-2.0
#include "mosaico_game_audio.h"
#include <stdint.h>
#include <string.h>
#include "bsp/esp_mosaico.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mosaico_game_assets.h"
#include "sdkconfig.h"
#define AUDIO_MAGIC 0x314e534dU
#define AUDIO_RATE 24000U
#define AUDIO_CHUNK 240U
#define AUDIO_CLIPS CONFIG_MOSAICO_GAME_AUDIO_CLIPS
#define AUDIO_VOICES CONFIG_MOSAICO_GAME_AUDIO_VOICES
typedef struct __attribute__((packed)){uint32_t magic,rate;uint16_t channels,bits;uint32_t frames,crc;} sound_header_t;
typedef struct{bool used;const uint8_t*data;uint32_t frames;uint8_t bits;float volume;} clip_t;
typedef struct{clip_t*clip;uint32_t cursor;uint32_t serial;int predictor;int index;bool active;} voice_t;
static const int16_t IMA_STEP[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t IMA_INDEX[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static const char*TAG="mosaico_audio";static clip_t s_clips[AUDIO_CLIPS];static voice_t s_voices[AUDIO_VOICES];static voice_t s_music_voice;static float s_music_volume=.28f;static bool s_music_playing,s_ready,s_running;static SemaphoreHandle_t s_lock;static TaskHandle_t s_task;static uint32_t s_serial;static mosaico_audio_stats_t s_stats;
static clip_t*sound_clip(Sound s){return(clip_t*)s.stream.buffer;}static clip_t*music_clip(Music m){return(clip_t*)m.ctxData;}
static clip_t*load_clip(const char*path){mosaico_asset_view_t a={0};if(mosaico_game_asset_open(path,&a)!=ESP_OK||a.size<sizeof(sound_header_t))return NULL;const sound_header_t*h=(const sound_header_t*)a.data;size_t bytes=h->bits==16?(size_t)h->frames*2:4U+(h->frames?((size_t)h->frames-1U+1U)/2U:0U);if(h->magic!=AUDIO_MAGIC||h->rate!=AUDIO_RATE||h->channels!=1||(h->bits!=16&&h->bits!=4)||sizeof(*h)+bytes>a.size)return NULL;for(int i=0;i<AUDIO_CLIPS;++i)if(!s_clips[i].used){s_clips[i]=(clip_t){.used=true,.data=a.data+sizeof(*h),.frames=h->frames,.bits=h->bits,.volume=1};return&s_clips[i];}return NULL;}
static void reset_voice(voice_t*v,clip_t*c){*v=(voice_t){.clip=c,.active=c!=NULL};if(c&&c->bits==4){v->predictor=(int16_t)((uint16_t)c->data[0]|(uint16_t)c->data[1]<<8);v->index=c->data[2];}}
static int16_t next_sample(voice_t*v){clip_t*c=v->clip;if(!c||v->cursor>=c->frames){v->active=false;return 0;}if(c->bits==16){const uint8_t*p=c->data+v->cursor++*2U;return(int16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}if(v->cursor++==0)return(int16_t)v->predictor;uint32_t n=v->cursor-2U;uint8_t code=(c->data[4+n/2]>>(n&1?4:0))&15U;int step=IMA_STEP[v->index],diff=step>>3;if(code&4)diff+=step;if(code&2)diff+=step>>1;if(code&1)diff+=step>>2;v->predictor+=code&8?-diff:diff;if(v->predictor>32767)v->predictor=32767;if(v->predictor<-32768)v->predictor=-32768;v->index+=IMA_INDEX[code];if(v->index<0)v->index=0;if(v->index>88)v->index=88;return(int16_t)v->predictor;}
static void audio_task(void*arg){(void)arg;i2s_std_config_t cfg={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16,I2S_SLOT_MODE_STEREO),.gpio_cfg={.mclk=BSP_AUDIO_I2S_MCLK,.bclk=BSP_AUDIO_I2S_SCLK,.ws=BSP_AUDIO_I2S_LRCLK,.dout=BSP_AUDIO_I2S_SDOUT,.din=GPIO_NUM_NC}};cfg.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;esp_codec_dev_handle_t codec=NULL;if(bsp_audio_init(&cfg)==ESP_OK)codec=bsp_audio_codec_speaker_init();esp_codec_dev_sample_info_t info={.sample_rate=AUDIO_RATE,.bits_per_sample=16,.channel=1,.channel_mask=0};if(!codec||esp_codec_dev_open(codec,&info)!=ESP_CODEC_DEV_OK){ESP_LOGW(TAG,"audio output unavailable");s_ready=false;s_running=false;s_task=NULL;vTaskDelete(NULL);return;}esp_codec_dev_set_out_vol(codec,72);s_ready=true;int16_t out[AUDIO_CHUNK];int64_t last_log=esp_timer_get_time();while(s_running){xSemaphoreTake(s_lock,portMAX_DELAY);uint8_t active=0;for(int v=0;v<AUDIO_VOICES;++v)active+=s_voices[v].active?1:0;s_stats.active_sfx_voices=active;s_stats.music_playing=s_music_playing;for(unsigned i=0;i<AUDIO_CHUNK;++i){int32_t mix=0;for(int v=0;v<AUDIO_VOICES;++v){voice_t*voice=&s_voices[v];if(voice->active)mix+=(int32_t)(next_sample(voice)*voice->clip->volume);}if(s_music_playing&&s_music_voice.clip){if(!s_music_voice.active)reset_voice(&s_music_voice,s_music_voice.clip);mix+=(int32_t)(next_sample(&s_music_voice)*s_music_volume);}if(mix>32767)mix=32767;if(mix<-32768)mix=-32768;out[i]=(int16_t)mix;}++s_stats.mixed_chunks;xSemaphoreGive(s_lock);int64_t started=esp_timer_get_time();if(esp_codec_dev_write(codec,out,sizeof(out))!=ESP_CODEC_DEV_OK){++s_stats.write_errors;vTaskDelay(pdMS_TO_TICKS(5));}if(esp_timer_get_time()-started>20000)++s_stats.underruns;if(esp_timer_get_time()-last_log>=5000000){ESP_LOGI(TAG,"chunks=%lu underrun=%lu write_error=%lu steals=%lu voices=%u music=%u",(unsigned long)s_stats.mixed_chunks,(unsigned long)s_stats.underruns,(unsigned long)s_stats.write_errors,(unsigned long)s_stats.voice_steals,s_stats.active_sfx_voices,s_stats.music_playing);last_log=esp_timer_get_time();}}esp_codec_dev_close(codec);s_ready=false;s_task=NULL;vTaskDelete(NULL);}
void MosaicoAudioInit(void){if(s_task)return;memset(&s_stats,0,sizeof(s_stats));s_lock=xSemaphoreCreateMutex();if(!s_lock)return;s_running=true;if(xTaskCreate(audio_task,"game_audio",4096,NULL,6,&s_task)!=pdPASS){s_running=false;vSemaphoreDelete(s_lock);s_lock=NULL;}}
void MosaicoAudioClose(void){s_running=false;while(s_task)vTaskDelay(1);if(s_lock){vSemaphoreDelete(s_lock);s_lock=NULL;}memset(s_clips,0,sizeof(s_clips));}
bool MosaicoAudioReady(void){return s_ready;}
Sound MosaicoAudioLoadSound(const char*path){clip_t*c=load_clip(path);return(Sound){.stream={.buffer=(rAudioBuffer*)c,.sampleRate=AUDIO_RATE,.sampleSize=16,.channels=1},.frameCount=c?c->frames:0};}
void MosaicoAudioUnloadSound(Sound s){clip_t*c=sound_clip(s);if(c)c->used=false;}
void MosaicoAudioPlaySound(Sound s){clip_t*c=sound_clip(s);if(!c||!s_lock)return;xSemaphoreTake(s_lock,portMAX_DELAY);int selected=-1;uint32_t oldest=UINT32_MAX;for(int i=0;i<AUDIO_VOICES;++i){if(!s_voices[i].active){selected=i;break;}if(s_voices[i].serial<oldest){oldest=s_voices[i].serial;selected=i;}}if(s_voices[selected].active)++s_stats.voice_steals;reset_voice(&s_voices[selected],c);s_voices[selected].serial=++s_serial;xSemaphoreGive(s_lock);}
void MosaicoAudioStopSound(Sound s){clip_t*c=sound_clip(s);if(!c||!s_lock)return;xSemaphoreTake(s_lock,portMAX_DELAY);for(int i=0;i<AUDIO_VOICES;++i)if(s_voices[i].clip==c)s_voices[i].active=false;xSemaphoreGive(s_lock);}
bool MosaicoAudioIsSoundPlaying(Sound s){clip_t*c=sound_clip(s);for(int i=0;i<AUDIO_VOICES;++i)if(s_voices[i].clip==c&&s_voices[i].active)return true;return false;}
void MosaicoAudioSetSoundVolume(Sound s,float v){clip_t*c=sound_clip(s);if(c)c->volume=v<0?0:v>1?1:v;}
Music MosaicoAudioLoadMusic(const char*path){clip_t*c=load_clip(path);return(Music){.stream={.buffer=(rAudioBuffer*)c,.sampleRate=AUDIO_RATE,.sampleSize=16,.channels=1},.frameCount=c?c->frames:0,.looping=true,.ctxData=c};}
void MosaicoAudioUnloadMusic(Music m){clip_t*c=music_clip(m);if(c)c->used=false;}
void MosaicoAudioPlayMusic(Music m){if(!s_lock)return;xSemaphoreTake(s_lock,portMAX_DELAY);reset_voice(&s_music_voice,music_clip(m));s_music_playing=s_music_voice.clip!=NULL;xSemaphoreGive(s_lock);}
void MosaicoAudioUpdateMusic(Music m){(void)m;}
void MosaicoAudioStopMusic(Music m){(void)m;s_music_playing=false;}
void MosaicoAudioSetMusicVolume(Music m,float v){(void)m;s_music_volume=v<0?0:v>1?1:v;}
void MosaicoAudioGetStats(mosaico_audio_stats_t*stats){if(!stats)return;if(s_lock)xSemaphoreTake(s_lock,portMAX_DELAY);*stats=s_stats;if(s_lock)xSemaphoreGive(s_lock);}
