// LiPo battery voltage via a 1:2 resistor divider (2 x 220k) on PIN_VBAT.
#pragma once
#include <stdint.h>

namespace battery {

void begin();
void update(uint32_t nowMs);  // call every loop; samples ~1/s, logs once a minute
float volts();                // smoothed battery voltage; < 0 until the first sample
bool present();               // false if the divider reads ~0 V (no battery / divider)

}  // namespace battery
