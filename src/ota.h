// Over-the-air updates (PLAN Phase 5): ArduinoOTA for `pio run -t upload`, and a
// password-protected HTTP upload page at /update for firmware or filesystem images.
#pragma once
#include <ESPAsyncWebServer.h>
#include <stdint.h>

namespace ota {

void begin(AsyncWebServer &server);  // registers /update
void loop();                         // ArduinoOTA, starts once Wi-Fi is up
bool updating();                     // an update is in progress

}  // namespace ota
