#pragma once
#include "esp_err.h"
#include <stdint.h>
/* Registered EEPROM board types, independent instances for both slots. */
typedef enum { HOT_PLUG_NONE, HOT_PLUG_CAMERA, HOT_PLUG_MATRIX } hot_plug_kind_t;
esp_err_t hot_plug_register_init(void);
esp_err_t hot_plug_register_get_slot(unsigned slot, hot_plug_kind_t *kind, uint32_t *generation);
