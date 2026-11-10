#include "hal.h"
#include <Arduino.h>

// WEAK default: does nothing. Lets the project build and run before the panel
// backlight API is wired up. Deliberately does NOT touch LEDC/GPIO2 -- the
// panel driver owns that pin.
//
// No extern "C": hal.h declares normal C++ linkage, so the definition must
// match or your override in the .ino would never take effect.
__attribute__((weak))
void hal_set_backlight(uint8_t percent) {
    (void)percent;
}
