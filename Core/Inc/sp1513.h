#ifndef SP1513_H
#define SP1513_H

/**
 * @file sp1513.h
 * @brief Per-channel white balance + gamma for the RGB LED part.
 *
 * Ported from the "In The Water" (STM32H750) project. Original part was the
 * Amicc A-SP1513R6GHB1C; the three dice are mismatched in luminous output, so
 * "set all channels equal" does NOT read as a neutral white without balancing.
 *
 * Pipeline (perceptual in, PWM duty out):
 *   perceptual color (0..1, sRGB-ish)
 *     -> sp1513_normalize() : per-channel white balance (still perceptual)
 *     -> gamma 2.2          : perceptual -> linear PWM duty  (done in led.c)
 *     -> max_brightness + active-low invert                  (done in led.c)
 *
 * *** WHITE-BALANCE KNOB ***
 * If white looks blue-tinted, RAISE kPreGammaR / kPreGammaG.
 * If white goes pink/yellow, LOWER them.
 * These are bench-tuned for the original part/drive; re-tune for THIS board's
 * LED once you can eyeball a "white" on the bench.
 */

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float r, g, b; } sp1513_Color;

/* Pre-gamma balance factors (applied in perceptual space, before gamma 2.2).
 * 1.0 = no change on that channel. Lower = dimmer channel. */
#define SP1513_PREGAMMA_R  0.80f
#define SP1513_PREGAMMA_G  0.60f
#define SP1513_PREGAMMA_B  0.79f

/** Balance a perceptual color so equal R=G=B reads neutral. Returns perceptual
 *  values — a gamma 2.2 stage MUST follow (led.c applies it). */
static inline sp1513_Color sp1513_normalize(sp1513_Color c)
{
    c.r *= SP1513_PREGAMMA_R;
    c.g *= SP1513_PREGAMMA_G;
    c.b *= SP1513_PREGAMMA_B;
    return c;
}

/** Perceptual (0..1) -> linear duty (0..1) via gamma 2.2. */
static inline float sp1513_gamma22(float perceptual)
{
    if (perceptual <= 0.0f) return 0.0f;
    if (perceptual >= 1.0f) return 1.0f;
    return powf(perceptual, 2.2f);
}

#ifdef __cplusplus
}
#endif

#endif /* SP1513_H */
