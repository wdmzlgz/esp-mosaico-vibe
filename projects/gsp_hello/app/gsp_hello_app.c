// SPDX-License-Identifier: Apache-2.0

#include "gsp_hello_app.h"

#include "driver/gpio.h"

/* Matches generated hello_binds.h (alphabetical bind names). */
#define GSP_BIND_LOAD  1
#define GSP_BIND_PRESS 2

#define GPIO7_PIN GPIO_NUM_7

static esp_gsp_handle_t s_ui;
static int32_t s_press;
static volatile int s_gpio7_clicks;
static bool s_use_isr;
static int s_prev_level = 1;

static void bump_press(esp_gsp_handle_t ui)
{
    if (s_press >= 100) {
        s_press = 0;
    } else {
        s_press += 10;
    }
    (void)esp_gsp_set_value(ui, GSP_BIND_PRESS, s_press);
}

static void gpio7_isr(void *arg)
{
    (void)arg;
    s_gpio7_clicks++;
#if !defined(ESP_PLATFORM)
    if (s_ui != NULL) {
        int n = s_gpio7_clicks;
        s_gpio7_clicks = 0;
        while (n-- > 0) {
            bump_press(s_ui);
        }
    }
#endif
}

static void feed_load(esp_gsp_handle_t ui, void *user_ctx)
{
    static int32_t load;
    (void)user_ctx;
    load = (load + 5) % 101;
    (void)esp_gsp_set_value(ui, GSP_BIND_LOAD, load);
}

static void poll_gpio7(esp_gsp_handle_t ui, void *user_ctx)
{
    (void)user_ctx;
    if (s_use_isr) {
        int n = s_gpio7_clicks;
        s_gpio7_clicks = 0;
        while (n-- > 0) {
            bump_press(ui);
        }
        return;
    }
    int level = gpio_get_level(GPIO7_PIN);
    if (s_prev_level != 0 && level == 0) {
        bump_press(ui);
    }
    s_prev_level = level;
}

static esp_err_t setup_gpio7(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << GPIO7_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    s_prev_level = gpio_get_level(GPIO7_PIN);
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    if (gpio_isr_handler_add(GPIO7_PIN, gpio7_isr, NULL) == ESP_OK) {
        s_use_isr = true;
    }
    return ESP_OK;
}

esp_err_t gsp_app_start(esp_gsp_handle_t ui)
{
    s_ui = ui;
    if (setup_gpio7() != ESP_OK) {
        return ESP_FAIL;
    }
    if (esp_gsp_timer_create(ui, 250, feed_load, NULL) == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (esp_gsp_timer_create(ui, 20, poll_gpio7, NULL) == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
