// Flow sensor pulse counting (ISR on FALLING edge, debounced).
#pragma once
#include <stdint.h>

namespace flow {

struct Pulses {
  uint32_t in;
  uint32_t out;
};

void begin();
// Pulses counted since the previous call.
Pulses takeDelta();

}  // namespace flow
