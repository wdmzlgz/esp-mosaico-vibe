#include "mosaico_game_assets.h"
#include <string.h>
#include "esp_mmap_assets.h"

static mmap_assets_handle_t s_store;
#define EMBEDDED_ASSET_CAPACITY 16
static mosaico_asset_view_t s_embedded[EMBEDDED_ASSET_CAPACITY];
static size_t s_embedded_count;

const mosaico_asset_t *mosaico_game_asset_find(const mosaico_asset_t *assets,size_t count,mosaico_asset_id_t id){
    if(!assets)return NULL;
    for(size_t i=0;i<count;++i)if(assets[i].id==id)return &assets[i];
    return NULL;
}

mosaico_asset_id_t mosaico_game_asset_id(const char *name)
{
    uint32_t hash = 2166136261U;
    if (!name) return 0;
    while (*name) hash = (hash ^ (uint8_t)*name++) * 16777619U;
    return hash;
}

esp_err_t mosaico_game_assets_mount(const mosaico_asset_store_config_t *config)
{
    if (!config || !config->partition_label || config->max_files <= 0 || s_store)
        return ESP_ERR_INVALID_ARG;
    mmap_assets_config_t mmap_config = {
        .partition_label = config->partition_label,
        .max_files = config->max_files,
        .checksum = config->checksum,
        .flags = {
            .mmap_enable = config->mmap_enable,
            .use_fs = false,
            .app_bin_check = true,
        },
    };
    return mmap_assets_new(&mmap_config, &s_store);
}

esp_err_t mosaico_game_asset_register_memory(const char *name,
                                              const void *data, size_t size)
{
    if (!name || !data || !size) return ESP_ERR_INVALID_ARG;
    mosaico_asset_id_t id=mosaico_game_asset_id(name);
    for(size_t i=0;i<s_embedded_count;++i)
        if(s_embedded[i].id==id)return ESP_ERR_INVALID_STATE;
    if(s_embedded_count>=EMBEDDED_ASSET_CAPACITY)return ESP_ERR_NO_MEM;
    s_embedded[s_embedded_count++]=(mosaico_asset_view_t){
        .id=id,.name=name,.data=data,.size=size};
    return ESP_OK;
}

void mosaico_game_assets_unmount(void)
{
    if (s_store) mmap_assets_del(s_store);
    s_store = NULL;
}

bool mosaico_game_assets_is_mounted(void) { return s_store != NULL; }

static esp_err_t open_index(int index, mosaico_asset_view_t *out)
{
    if (!s_store || !out || index < 0) return ESP_ERR_INVALID_ARG;
    const char *name = mmap_assets_get_name(s_store, index);
    const void *data = mmap_assets_get_mem(s_store, index);
    int size = mmap_assets_get_size(s_store, index);
    if (!name || !data || size <= 0) return ESP_ERR_NOT_FOUND;
    *out = (mosaico_asset_view_t){
        .id = mosaico_game_asset_id(name), .name = name,
        .data = data, .size = (size_t)size,
    };
    return ESP_OK;
}

esp_err_t mosaico_game_asset_open(const char *name, mosaico_asset_view_t *out)
{
    if (!name || !out) return ESP_ERR_INVALID_ARG;
    if(s_store){
        int count = mmap_assets_get_stored_files(s_store);
        for (int i = 0; i < count; ++i) {
            const char *candidate = mmap_assets_get_name(s_store, i);
            if (candidate && strcmp(candidate, name) == 0) return open_index(i, out);
        }
    }
    for(size_t i=0;i<s_embedded_count;++i)
        if(strcmp(s_embedded[i].name,name)==0){*out=s_embedded[i];return ESP_OK;}
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mosaico_game_asset_open_id(mosaico_asset_id_t id,
                                     mosaico_asset_view_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if(s_store){
        int count = mmap_assets_get_stored_files(s_store);
        for (int i = 0; i < count; ++i) {
            const char *name = mmap_assets_get_name(s_store, i);
            if (name && mosaico_game_asset_id(name) == id) return open_index(i, out);
        }
    }
    for(size_t i=0;i<s_embedded_count;++i)
        if(s_embedded[i].id==id){*out=s_embedded[i];return ESP_OK;}
    return ESP_ERR_NOT_FOUND;
}
