// NTP + local time zone. Local dates are yyyymmdd; 0 while the clock is not valid.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace clk {

void begin();     // set TZ (works with RTC time that survived a soft reset)
void startNtp();  // call once the network is up
bool valid();
uint32_t today();                      // yyyymmdd local, 0 if not valid
void isoTime(char *buf, size_t len);   // "2026-10-17T14:32:05-04:00", "" if not valid
void formatDate(uint32_t yyyymmdd, char *buf, size_t len);  // "2026-10-17", "" for 0

}  // namespace clk
