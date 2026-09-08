#include "hot_plug_register.h"
#include "mosaico_module_mgr.h"
#include <stddef.h>
/* Names in the EEPROM are descriptive; the BSP board_type is the identity.
 * In particular, do not confuse MATRIX_LED (64 pixels) with BUTTON_LED (3). */
static const struct { mosaico_board_type_t type; hot_plug_kind_t kind; } registry[] = {
    { MOSAICO_BOARD_TYPE_CAMERA, HOT_PLUG_CAMERA },
    { MOSAICO_BOARD_TYPE_MATRIX_LED, HOT_PLUG_MATRIX },
};
esp_err_t hot_plug_register_init(void) { return mosaico_module_mgr_init(NULL); }
esp_err_t hot_plug_register_get_slot(unsigned slot, hot_plug_kind_t *kind, uint32_t *generation)
{
    if (slot >= 2 || !kind || !generation) return ESP_ERR_INVALID_ARG;
    mosaico_module_mgr_info_t info;
    esp_err_t err = mosaico_module_mgr_get_info((mosaico_module_mgr_slot_t)slot, &info);
    if (err != ESP_OK) return err;
    *kind = HOT_PLUG_NONE;
    *generation = info.generation;
    if (info.state != MOSAICO_MODULE_MGR_STATE_READY && info.state != MOSAICO_MODULE_MGR_STATE_CLAIMED) return ESP_OK;
    for (size_t i = 0; i < sizeof(registry)/sizeof(registry[0]); i++) {
        if (registry[i].type == info.eeprom.board_type && !(slot == 1 && registry[i].kind == HOT_PLUG_CAMERA)) *kind = registry[i].kind;
    }
    return ESP_OK;
}
