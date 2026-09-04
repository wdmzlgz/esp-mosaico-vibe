// SPDX-License-Identifier: Apache-2.0
#include "sky_hop_save.h"

#include "nvs.h"

#define SKY_HOP_SAVE_VERSION 1U

esp_err_t sky_hop_save_load(uint16_t *best_score)
{
    if (!best_score) return ESP_ERR_INVALID_ARG;
    *best_score = 0;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("sky_hop", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    uint8_t version = 0;
    error = nvs_get_u8(handle, "version", &version);
    if (error == ESP_OK && version == SKY_HOP_SAVE_VERSION)
        error = nvs_get_u16(handle, "best", best_score);
    nvs_close(handle);
    return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
}

esp_err_t sky_hop_save_best_score(uint16_t best_score)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open("sky_hop", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    if ((error = nvs_set_u8(handle, "version", SKY_HOP_SAVE_VERSION)) == ESP_OK)
        error = nvs_set_u16(handle, "best", best_score);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
