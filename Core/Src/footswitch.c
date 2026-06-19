#include "footswitch.h"

void fsw_init(Footswitch* fs, GPIO_TypeDef* port, uint16_t pin, bool active_low)
{
    fs->port           = port;
    fs->pin            = pin;
    fs->active_low     = active_low;
    fs->stable         = false;
    fs->count          = 0;
    fs->rising         = false;
    fs->falling        = false;
    fs->press_start_ms = 0;
}

void fsw_poll(Footswitch* fs, uint32_t now_ms)
{
    /* Raw pressed-state from the pin, accounting for wiring polarity. */
    bool level = (HAL_GPIO_ReadPin(fs->port, fs->pin) == GPIO_PIN_SET);
    bool raw   = fs->active_low ? !level : level;

    /* Edges only fire on the poll where a debounced transition commits. */
    fs->rising  = false;
    fs->falling = false;

    if (raw == fs->stable) {
        fs->count = 0;                       /* agrees with committed state */
        return;
    }

    if (++fs->count >= FSW_DEBOUNCE_COUNT) {  /* held long enough -> commit */
        fs->count   = 0;
        fs->stable  = raw;
        fs->rising  =  raw;
        fs->falling = !raw;
        fs->press_start_ms = raw ? now_ms : 0;
    }
}
