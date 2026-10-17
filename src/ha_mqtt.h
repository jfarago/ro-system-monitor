// Home Assistant over MQTT: discovery configs, JSON state, availability/LWT (PLAN §7).
#pragma once
#include <stdint.h>

namespace ha {

void begin();
void loop(uint32_t nowMs);  // non-blocking apart from the TCP connect attempt
bool connected();           // MQTT session is up

}  // namespace ha
