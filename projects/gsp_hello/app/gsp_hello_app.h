#pragma once

#include "esp_gsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Portable Hello World logic. Device and the host WASM simulator both call
 *  this after they have a live `esp_gsp_handle_t`. */
esp_err_t gsp_app_start(esp_gsp_handle_t ui);

#ifdef __cplusplus
}
#endif
