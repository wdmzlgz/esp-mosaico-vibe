#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SB_LED_MIN_BRIGHTNESS 0U
#define SB_LED_MAX_BRIGHTNESS 96U

void sb_color(unsigned tick, unsigned pixel, uint8_t rgb[3]);
unsigned sb_matrix_index(unsigned side, unsigned pixel);
bool sb_crop_uyvy(const uint8_t *src, size_t size, unsigned width,
                  unsigned height, size_t stride, uint16_t *out);
