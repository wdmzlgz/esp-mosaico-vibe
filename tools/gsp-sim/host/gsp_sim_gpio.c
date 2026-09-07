#include "gsp_sim_gpio.h"

#include "driver/gpio.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define GPIO_PIN_COUNT 64

typedef struct {
    uint8_t level;
    uint8_t armed;
    uint8_t intr_on;
    uint8_t pull_up;
    uint8_t pull_down;
    gpio_int_type_t intr;
    gpio_isr_t isr;
    void *isr_arg;
} gpio_pin_t;

static gpio_pin_t s_pins[GPIO_PIN_COUNT];
static bool s_ready;

static void gpio_host_init(void)
{
    if (s_ready) {
        return;
    }
    memset(s_pins, 0, sizeof(s_pins));
    for (int pin = 0; pin < GPIO_PIN_COUNT; ++pin) {
        s_pins[pin].level = 1;
        s_pins[pin].pull_up = 1;
        s_pins[pin].intr_on = 1;
    }
    s_ready = true;
}

static bool valid_pin(gpio_num_t gpio_num)
{
    return gpio_num >= 0 && gpio_num < GPIO_PIN_COUNT;
}

static void arm_pin(gpio_num_t gpio_num)
{
    if (valid_pin(gpio_num)) {
        s_pins[gpio_num].armed = 1;
    }
}

static void fire_isr(gpio_num_t gpio_num, int prev, int next)
{
    gpio_pin_t *pin = &s_pins[gpio_num];
    if (!pin->intr_on || pin->isr == NULL) {
        return;
    }
    bool edge = false;
    switch (pin->intr) {
    case GPIO_INTR_POSEDGE:
        edge = prev == 0 && next == 1;
        break;
    case GPIO_INTR_NEGEDGE:
        edge = prev == 1 && next == 0;
        break;
    case GPIO_INTR_ANYEDGE:
        edge = prev != next;
        break;
    case GPIO_INTR_LOW_LEVEL:
        edge = next == 0;
        break;
    case GPIO_INTR_HIGH_LEVEL:
        edge = next == 1;
        break;
    default:
        break;
    }
    if (edge) {
        pin->isr(pin->isr_arg);
    }
}

static void write_level(gpio_num_t gpio_num, int level)
{
    gpio_pin_t *pin = &s_pins[gpio_num];
    int next = level ? 1 : 0;
    int prev = pin->level;
    pin->level = (uint8_t)next;
    if (prev != next) {
        fire_isr(gpio_num, prev, next);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE int gsp_app_sim_gpio_armed(int pin)
{
    gpio_host_init();
    if (!valid_pin((gpio_num_t)pin)) {
        return 0;
    }
    return s_pins[pin].armed ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void gsp_app_sim_gpio_set(int pin, int level)
{
    gpio_host_init();
    if (!valid_pin((gpio_num_t)pin) || !s_pins[pin].armed) {
        return;
    }
    write_level((gpio_num_t)pin, level);
}
#endif

void gsp_sim_gpio_sync_shell(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof Module.syncGpioKeys === "function") {
            Module.syncGpioKeys();
        }
    });
#endif
}

esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)
{
    gpio_host_init();
    if (pGPIOConfig == NULL || pGPIOConfig->pin_bit_mask == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int pin = 0; pin < GPIO_PIN_COUNT; ++pin) {
        if ((pGPIOConfig->pin_bit_mask & (1ULL << pin)) == 0) {
            continue;
        }
        arm_pin((gpio_num_t)pin);
        s_pins[pin].intr = pGPIOConfig->intr_type;
        s_pins[pin].pull_up = pGPIOConfig->pull_up_en ? 1 : 0;
        s_pins[pin].pull_down = pGPIOConfig->pull_down_en ? 1 : 0;
        if (s_pins[pin].pull_up) {
            s_pins[pin].level = 1;
        } else if (s_pins[pin].pull_down) {
            s_pins[pin].level = 0;
        }
        (void)pGPIOConfig->mode;
    }
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].intr = GPIO_INTR_DISABLE;
    s_pins[gpio_num].isr = NULL;
    s_pins[gpio_num].isr_arg = NULL;
    s_pins[gpio_num].pull_up = 1;
    s_pins[gpio_num].pull_down = 0;
    s_pins[gpio_num].level = 1;
    return ESP_OK;
}

esp_err_t gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    arm_pin(gpio_num);
    s_pins[gpio_num].intr = intr_type;
    return ESP_OK;
}

esp_err_t gpio_intr_enable(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    arm_pin(gpio_num);
    s_pins[gpio_num].intr_on = 1;
    return ESP_OK;
}

esp_err_t gpio_intr_disable(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].intr_on = 0;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    write_level(gpio_num, (int)level);
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return 0;
    }
    arm_pin(gpio_num);
    return s_pins[gpio_num].level;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    arm_pin(gpio_num);
    (void)mode;
    return ESP_OK;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    arm_pin(gpio_num);
    s_pins[gpio_num].pull_up =
        (pull == GPIO_PULLUP_ONLY || pull == GPIO_PULLUP_PULLDOWN);
    s_pins[gpio_num].pull_down =
        (pull == GPIO_PULLDOWN_ONLY || pull == GPIO_PULLUP_PULLDOWN);
    return ESP_OK;
}

esp_err_t gpio_pullup_en(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].pull_up = 1;
    return ESP_OK;
}

esp_err_t gpio_pullup_dis(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].pull_up = 0;
    return ESP_OK;
}

esp_err_t gpio_pulldown_en(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].pull_down = 1;
    return ESP_OK;
}

esp_err_t gpio_pulldown_dis(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].pull_down = 0;
    return ESP_OK;
}

esp_err_t gpio_install_isr_service(int intr_alloc_flags)
{
    (void)intr_alloc_flags;
    gpio_host_init();
    return ESP_OK;
}

void gpio_uninstall_isr_service(void)
{
    gpio_host_init();
}

esp_err_t gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler,
    void *args)
{
    gpio_host_init();
    if (!valid_pin(gpio_num) || isr_handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    arm_pin(gpio_num);
    s_pins[gpio_num].isr = isr_handler;
    s_pins[gpio_num].isr_arg = args;
    return ESP_OK;
}

esp_err_t gpio_isr_handler_remove(gpio_num_t gpio_num)
{
    gpio_host_init();
    if (!valid_pin(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins[gpio_num].isr = NULL;
    s_pins[gpio_num].isr_arg = NULL;
    return ESP_OK;
}
