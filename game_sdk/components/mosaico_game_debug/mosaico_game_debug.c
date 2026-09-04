// SPDX-License-Identifier: Apache-2.0
#include "mosaico_game_debug.h"
#include "esp_log.h"
#include "mosaico_game.h"
void mosaico_game_debug_log(const char *tag){
    mosaico_game_stats_t s; MosaicoGameGetStats(&s);
    ESP_LOGI(tag?tag:"mosaico_game","fps=%.1f frame=%lu dropped=%lu update=%luus render=%luus present=%luus heap=%u psram=%u",s.fps,(unsigned long)s.frames,(unsigned long)s.dropped_frames,(unsigned long)s.update_us,(unsigned long)s.render_us,(unsigned long)s.present_us,(unsigned)s.free_internal_bytes,(unsigned)s.free_psram_bytes);
}
