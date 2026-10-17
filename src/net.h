// Wi-Fi STA with reconnect backoff, fallback AP, mDNS (PLAN §4.6).
#pragma once
#include <stdint.h>

namespace net {

void begin();
void loop(uint32_t nowMs);
bool connected();  // STA has an IP
bool apActive();   // fallback AP is up
int rssi();        // dBm, 0 if not connected

}  // namespace net
