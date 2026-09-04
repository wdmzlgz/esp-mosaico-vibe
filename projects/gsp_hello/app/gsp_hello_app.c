// SPDX-License-Identifier: Apache-2.0

#include "gsp_hello_app.h"

/* Matches `"bind": "load"` / generated GSP_BIND_LOAD in hello_binds.h. */
#define GSP_BIND_LOAD 1

static void feed_load(esp_gsp_handle_t ui, void *user_ctx)
{
    static int32_t load;
    (void)user_ctx;
    load = (load + 5) % 101;
    (void)esp_gsp_set_value(ui, GSP_BIND_LOAD, load);
}

esp_err_t gsp_app_start(esp_gsp_handle_t ui)
{
    void *load_timer = esp_gsp_timer_create(ui, 250, feed_load, NULL);
    return load_timer == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}
