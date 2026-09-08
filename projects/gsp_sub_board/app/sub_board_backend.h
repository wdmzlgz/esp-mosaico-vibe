#pragma once
#include "esp_gsp.h"
#include <stdint.h>
/* Verified against gspc 0.2.8's generated sub_board_binds/actions headers.
 * Keep these IDs in sync when changing the scene (host has no IDF headers). */
enum { SB_CAMERA_BIND=0, SB_HOME_BIND=1, SB_LEFT_BIND=2, SB_NOTICE_BIND=3,
       SB_NOTICE_BOX_BIND=4, SB_PREVIEW_BIND=5, SB_PREVIEW_BOX_BIND=6, SB_RIGHT_BIND=7 };
enum { SB_HOME=0, SB_CAMERA=1, SB_LIGHTS=2 };
/* Presence: bit 0 = left camera, bit 1 = left matrix, bit 2 = right matrix. */
esp_err_t sb_backend_start(esp_gsp_handle_t ui);
unsigned sb_backend_present(void);
void sb_backend_request(unsigned mode);
int sb_backend_error(void);
void sb_backend_tick(void);
void sb_color(unsigned tick, unsigned pixel, uint8_t rgb[3]);
void sb_publish(esp_gsp_handle_t ui, uint16_t *pixels);
