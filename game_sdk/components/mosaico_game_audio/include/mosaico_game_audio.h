// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "raylib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t mixed_chunks;
    uint32_t write_errors;
    uint32_t underruns;
    uint32_t voice_steals;
    uint8_t active_sfx_voices;
    bool music_playing;
} mosaico_audio_stats_t;
void MosaicoAudioInit(void);
void MosaicoAudioClose(void);
bool MosaicoAudioReady(void);
Sound MosaicoAudioLoadSound(const char *asset_path);
void MosaicoAudioUnloadSound(Sound sound);
void MosaicoAudioPlaySound(Sound sound);
void MosaicoAudioStopSound(Sound sound);
bool MosaicoAudioIsSoundPlaying(Sound sound);
void MosaicoAudioSetSoundVolume(Sound sound,float volume);
Music MosaicoAudioLoadMusic(const char *asset_path);
void MosaicoAudioUnloadMusic(Music music);
void MosaicoAudioPlayMusic(Music music);
void MosaicoAudioUpdateMusic(Music music);
void MosaicoAudioStopMusic(Music music);
void MosaicoAudioSetMusicVolume(Music music,float volume);
void MosaicoAudioGetStats(mosaico_audio_stats_t *stats);
#ifdef __cplusplus
}
#endif
#define InitAudioDevice MosaicoAudioInit
#define CloseAudioDevice MosaicoAudioClose
#define IsAudioDeviceReady MosaicoAudioReady
#define LoadSound MosaicoAudioLoadSound
#define UnloadSound MosaicoAudioUnloadSound
#define PlaySound MosaicoAudioPlaySound
#define StopSound MosaicoAudioStopSound
#define IsSoundPlaying MosaicoAudioIsSoundPlaying
#define SetSoundVolume MosaicoAudioSetSoundVolume
#define LoadMusicStream MosaicoAudioLoadMusic
#define UnloadMusicStream MosaicoAudioUnloadMusic
#define PlayMusicStream MosaicoAudioPlayMusic
#define UpdateMusicStream MosaicoAudioUpdateMusic
#define StopMusicStream MosaicoAudioStopMusic
#define SetMusicVolume MosaicoAudioSetMusicVolume
