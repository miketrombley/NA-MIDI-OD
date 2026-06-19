#include "led_demo.h"
#include <math.h>

/* HSV (all 0..1) -> RGB (0..1). Lets us sweep a clean rainbow by walking hue. */
static void hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b)
{
    h -= floorf(h);                 /* wrap hue into 0..1 (handles negatives)  */
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (((int)i) % 6) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;  /* case 5 */
    }
}

void led_demo_rainbow(LedRgb* left, LedRgb* right, uint32_t now_ms)
{
    const uint32_t cycle_ms = 4000;   /* seconds per full rainbow rotation     */
    const float    lag      = 0.12f;  /* hue offset = apparent travel distance  */

    float hue = (float)(now_ms % cycle_ms) / (float)cycle_ms;  /* 0..1 */

    float r, g, b;
    hsv_to_rgb(hue,        1.0f, 1.0f, &r, &g, &b);  /* left leads  */
    ledrgb_set(left, r, g, b);
    hsv_to_rgb(hue - lag,  1.0f, 1.0f, &r, &g, &b);  /* right lags -> flows L->R */
    ledrgb_set(right, r, g, b);
}
