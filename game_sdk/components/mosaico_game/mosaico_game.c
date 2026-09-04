// SPDX-License-Identifier: Apache-2.0
#include "mosaico_game.h"

#include <stdlib.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_iris_system_inventory.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "psa/crypto.h"

#define EVENT_QUEUE_LENGTH CONFIG_MOSAICO_GAME_EVENT_QUEUE_LENGTH
#define INVENTORY_HASH_CHUNK_BYTES 1024U
#define SYSTEM_METADATA_MAGIC 0x49535953U
#define SYSTEM_METADATA_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t operation_id[ESP_IRIS_SYSTEM_OPERATION_ID_BYTES];
    int32_t result;
    uint8_t reserved[36];
} mosaico_system_metadata_t;

static const char *TAG = "mosaico_game";
static mosaico_game_config_t s_config;
static mosaico_game_stats_t s_stats;
static QueueHandle_t s_events;
static int64_t s_stats_started_us;
static int64_t s_last_frame_us;
static uint32_t s_window_frames;

static esp_err_t inventory_hash_flash(uint32_t address, size_t size,
                                      uint8_t output[32])
{
    uint8_t *buffer = heap_caps_malloc(INVENTORY_HASH_CHUNK_BYTES,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) return ESP_ERR_NO_MEM;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    esp_err_t err = ESP_OK;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        free(buffer);
        return ESP_FAIL;
    }
    for (size_t offset = 0; offset < size;) {
        size_t chunk = size - offset;
        if (chunk > INVENTORY_HASH_CHUNK_BYTES) chunk = INVENTORY_HASH_CHUNK_BYTES;
        if (esp_flash_read(NULL, buffer, address + offset, chunk) != ESP_OK ||
            psa_hash_update(&hash, buffer, chunk) != PSA_SUCCESS) {
            err = ESP_FAIL;
            break;
        }
        offset += chunk;
    }
    size_t written = 0;
    if (err == ESP_OK &&
        (psa_hash_finish(&hash, output, 32, &written) != PSA_SUCCESS ||
         written != 32)) err = ESP_FAIL;
    if (err != ESP_OK) (void)psa_hash_abort(&hash);
    free(buffer);
    return err;
}

static esp_err_t game_inventory_get(esp_iris_system_inventory_t *inventory,
                                    void *user_ctx)
{
    (void)user_ctx;
    if (!inventory) return ESP_ERR_INVALID_ARG;
    memset(inventory, 0, sizeof(*inventory));
    inventory->layout_version = 3;
    esp_err_t err = inventory_hash_flash(CONFIG_BOOTLOADER_OFFSET_IN_FLASH,
        CONFIG_PARTITION_TABLE_OFFSET - CONFIG_BOOTLOADER_OFFSET_IN_FLASH,
        inventory->bootloader_sha256);
    if (err != ESP_OK) return err;
    inventory->flags |= ESP_IRIS_SYSTEM_INVENTORY_BOOTLOADER_SHA256;
    err = inventory_hash_flash(CONFIG_PARTITION_TABLE_OFFSET, 0x1000,
                               inventory->partition_table_sha256);
    if (err != ESP_OK) return err;
    inventory->flags |= ESP_IRIS_SYSTEM_INVENTORY_PARTITION_TABLE_SHA256;
    const esp_partition_t *sysmeta = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "sysmeta");
    mosaico_system_metadata_t record;
    if (sysmeta && sysmeta->size >= sizeof(record) &&
        esp_partition_read(sysmeta, 0, &record, sizeof(record)) == ESP_OK &&
        record.magic == SYSTEM_METADATA_MAGIC &&
        record.version == SYSTEM_METADATA_VERSION) {
        memcpy(inventory->last_operation_id, record.operation_id,
               sizeof(inventory->last_operation_id));
        inventory->last_result = record.result;
        inventory->flags |= ESP_IRIS_SYSTEM_INVENTORY_LAST_OPERATION;
    }
    return ESP_OK;
}

esp_err_t MosaicoGameRegisterSystemInventory(void)
{
    const esp_iris_system_inventory_provider_t provider = {
        .get_inventory = game_inventory_get,
        .user_ctx = NULL,
    };
    return esp_iris_system_inventory_register(&provider);
}

esp_err_t MosaicoGameInit(const mosaico_game_config_t *config)
{
    if (s_events) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = config ? *config : (mosaico_game_config_t)MOSAICO_GAME_CONFIG_DEFAULT();
    if (s_config.width != MOSAICO_GAME_WIDTH ||
            s_config.height != MOSAICO_GAME_HEIGHT ||
            s_config.target_fps <= 0 || s_config.game_task_stack == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_events = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(mosaico_device_event_t));
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats_started_us = esp_timer_get_time();
    s_last_frame_us = 0;
    s_window_frames = 0;
    ESP_LOGI(TAG, "platform ready: %dx%d @ %d fps", s_config.width,
             s_config.height, s_config.target_fps);
    return ESP_OK;
}

void MosaicoGameShutdown(void)
{
    if (s_events) {
        vQueueDelete(s_events);
        s_events = NULL;
    }
    memset(&s_stats, 0, sizeof(s_stats));
    s_last_frame_us = 0;
    s_window_frames = 0;
}

bool MosaicoGamePollDeviceEvent(mosaico_device_event_t *event)
{
    return event && s_events && xQueueReceive(s_events, event, 0) == pdTRUE;
}

bool MosaicoGamePostDeviceEvent(const mosaico_device_event_t *event)
{
    return event && s_events && xQueueSend(s_events, event, 0) == pdTRUE;
}

void MosaicoGameRecordFrame(uint32_t update_us, uint32_t render_us,
                            uint32_t present_us, bool dropped)
{
    ++s_stats.frames;
    s_stats.dropped_frames += dropped ? 1U : 0U;
    s_stats.update_us = update_us;
    s_stats.render_us = render_us;
    s_stats.present_us = present_us;
    int64_t now = esp_timer_get_time();
    if (s_last_frame_us && now - s_last_frame_us > 250000) {
        s_stats_started_us = now;
        s_window_frames = 0;
    }
    s_last_frame_us = now;
    ++s_window_frames;
    int64_t elapsed = now - s_stats_started_us;
    if (elapsed >= 1000000) {
        s_stats.fps = (float)s_window_frames * 1000000.0f / (float)elapsed;
        s_stats_started_us = now;
        s_window_frames = 0;
    }
}

void MosaicoGameRecordTiming(uint32_t update_us, uint32_t render_us)
{
    s_stats.update_us = update_us;
    s_stats.render_us = render_us;
}

void MosaicoGameGetStats(mosaico_game_stats_t *stats)
{
    if (!stats) return;
    *stats = s_stats;
    stats->free_internal_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    stats->free_psram_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

const mosaico_game_config_t *MosaicoGameGetConfig(void)
{
    return s_events ? &s_config : NULL;
}
