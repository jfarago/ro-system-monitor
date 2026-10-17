#include "clock.h"
#include <Arduino.h>
#include <time.h>
#include "config.h"

namespace clk {

// Anything before 2025-01-01 is the unsynced RTC counting up from 1970.
constexpr time_t MIN_VALID_EPOCH = 1735689600;

static bool ntpStarted = false;

void begin() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
}

void startNtp() {
  if (ntpStarted) return;
  ntpStarted = true;
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);  // SNTP keeps resyncing on its own
}

bool valid() { return time(nullptr) >= MIN_VALID_EPOCH; }

uint32_t today() {
  time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH) return 0;
  struct tm t;
  localtime_r(&now, &t);
  return (uint32_t)(t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

void isoTime(char *buf, size_t len) {
  time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH) { buf[0] = 0; return; }
  struct tm t;
  localtime_r(&now, &t);
  size_t n = strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", &t);  // ...-0400
  // Insert the colon in the offset: -0400 -> -04:00
  if (n >= 5 && n + 1 < len) {
    memmove(buf + n - 1, buf + n - 2, 3);
    buf[n - 2] = ':';
  }
}

void formatDate(uint32_t d, char *buf, size_t len) {
  if (d == 0) { buf[0] = 0; return; }
  snprintf(buf, len, "%04lu-%02lu-%02lu", (unsigned long)(d / 10000),
           (unsigned long)((d / 100) % 100), (unsigned long)(d % 100));
}

}  // namespace clk
