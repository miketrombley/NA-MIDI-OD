#ifndef FOOTSWITCH_H
#define FOOTSWITCH_H

/**
 * @file footswitch.h
 * @brief Footswitch wrapper — debounced GPIO + edge events + hold timer.
 *        Ported from the "In The Water" Footswitch (which leaned on
 *        daisy::Switch for debounce; here we integrate it ourselves).
 *
 * Call fsw_poll() at a steady cadence (e.g. the 100 Hz UI tick). A state has to
 * hold for FSW_DEBOUNCE_COUNT consecutive polls before it commits, which both
 * debounces contact bounce and defines edge timing. After poll(), read:
 *   fsw_pressed()  — debounced level
 *   fsw_rising()   — true for the one poll where it became pressed
 *   fsw_falling()  — true for the one poll where it became released
 *   fsw_hold_ms()  — ms held since the last rising edge
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Consecutive same-state polls required to commit. At a 10 ms poll that's a
 * ~20 ms debounce window — comfortable for a mechanical stomp switch. */
#define FSW_DEBOUNCE_COUNT  2

typedef struct {
    /* config */
    GPIO_TypeDef* port;
    uint16_t      pin;
    bool          active_low;      /* true: pin LOW = pressed (pull-up wiring) */
    /* debounce + state */
    bool          stable;          /* committed debounced pressed-state        */
    uint8_t       count;           /* consecutive polls disagreeing with stable*/
    bool          rising;          /* became pressed this poll                 */
    bool          falling;         /* became released this poll                */
    uint32_t      press_start_ms;  /* tick at last rising edge (0 = released)  */
} Footswitch;

/** Bind to a GPIO. active_low = true for switch-to-ground + pull-up wiring. */
void fsw_init(Footswitch* fs, GPIO_TypeDef* port, uint16_t pin, bool active_low);

/** Sample + debounce + update edges/hold. Pass HAL_GetTick() as now_ms. */
void fsw_poll(Footswitch* fs, uint32_t now_ms);

static inline bool fsw_pressed(const Footswitch* fs) { return fs->stable; }
static inline bool fsw_rising (const Footswitch* fs) { return fs->rising; }
static inline bool fsw_falling(const Footswitch* fs) { return fs->falling; }

/** Milliseconds held since the last rising edge (0 when released). */
static inline uint32_t fsw_hold_ms(const Footswitch* fs, uint32_t now_ms)
{
    return fs->press_start_ms ? (now_ms - fs->press_start_ms) : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* FOOTSWITCH_H */
