// SPDX-License-Identifier: Apache-2.0
#include "sub_board_pixels.h"
#include <math.h>

#define HSV_SEXTANT_STEPS 256U
#define HSV_CIRCLE_STEPS  (6U * HSV_SEXTANT_STEPS)
#define BREATH_PERIOD_TICKS 100U
#define HUE_PERIOD_TICKS    300U

static void hsv_to_rgb(unsigned hue, uint8_t saturation, uint8_t value,
                       uint8_t rgb[3])
{
    const unsigned region = (hue / HSV_SEXTANT_STEPS) % 6U;
    const unsigned fraction = hue % HSV_SEXTANT_STEPS;
    const uint8_t p = (uint8_t)((value * (255U - saturation)) / 255U);
    const uint8_t q = (uint8_t)(
        (value * (255U - ((saturation * fraction) / 255U))) / 255U);
    const uint8_t t = (uint8_t)(
        (value * (255U - ((saturation * (255U - fraction)) / 255U))) /
        255U);

    switch (region) {
    case 0:
        rgb[0] = value; rgb[1] = t;     rgb[2] = p;
        break;
    case 1:
        rgb[0] = q;     rgb[1] = value; rgb[2] = p;
        break;
    case 2:
        rgb[0] = p;     rgb[1] = value; rgb[2] = t;
        break;
    case 3:
        rgb[0] = p;     rgb[1] = q;     rgb[2] = value;
        break;
    case 4:
        rgb[0] = t;     rgb[1] = p;     rgb[2] = value;
        break;
    default:
        rgb[0] = value; rgb[1] = p;     rgb[2] = q;
        break;
    }
}

void sb_color(unsigned tick, unsigned pixel, uint8_t rgb[3])
{
    /* A three-second raised-cosine breath over a slowly rotating rainbow.
     * The same HSV-to-RGB result feeds the browser and the physical strip. */
    const float level =
        (1.0f - cosf((tick % BREATH_PERIOD_TICKS) * 6.283185307f /
                     BREATH_PERIOD_TICKS)) *
        0.5f;
    const uint8_t value = (uint8_t)(
        SB_LED_MIN_BRIGHTNESS +
        level * (SB_LED_MAX_BRIGHTNESS - SB_LED_MIN_BRIGHTNESS) + 0.5f);
    const unsigned spatial_hue =
        (pixel % 64U) * HSV_CIRCLE_STEPS / 64U;
    const unsigned animated_hue =
        (tick % HUE_PERIOD_TICKS) * HSV_CIRCLE_STEPS / HUE_PERIOD_TICKS;

    hsv_to_rgb((spatial_hue + animated_hue) % HSV_CIRCLE_STEPS,
               255U, value, rgb);
}
unsigned sb_matrix_index(unsigned side, unsigned pixel) { return side?63-pixel:pixel; }
static uint8_t clamp(int x) { return x<0?0:x>255?255:x; }
bool sb_crop_uyvy(const uint8_t *src, size_t size, unsigned width,
                  unsigned height, size_t stride, uint16_t *out)
{
    if (!src || !out || width<480 || height<480 || (width&1) ||
        stride/2<width || size/stride<height) return false;
    unsigned x0=((width-480)/2)&~1U, y0=(height-480)/2;
    for (unsigned y=0;y<480;y++) {
        const uint8_t *row=src+(y+y0)*stride+x0*2;
        for (unsigned x=0;x<480;x++) {
            const uint8_t *pair=row+(x&~1U)*2;
            int u=pair[0]-128,v=pair[2]-128,c=pair[(x&1)?3:1]-16;
            uint8_t r=clamp((298*c+409*v+128)>>8);
            uint8_t g=clamp((298*c-100*u-208*v+128)>>8);
            uint8_t b=clamp((298*c+516*u+128)>>8);
            out[y*480+x]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
        }
    }
    return true;
}
