#include "store.h"
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

namespace store {

Counters counters;
Calibration cal;

static Preferences prefs;
static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
static uint8_t dirty = 0;
static bool saveRequested = false;
static uint32_t dirtySinceMs = 0;
static uint32_t saves = 0;

static void defaultCal() {
  cal = {CALIBRATION_VERSION, K_FLOW_IN_PER_GAL, K_FLOW_OUT_PER_GAL, K_TDS, WATER_TEMP_C};
}

// Load a versioned blob; false (and leave the struct alone) if missing or a different layout.
template <typename T>
static bool loadBlob(const char *key, T &out, uint16_t version) {
  if (prefs.getBytesLength(key) != sizeof(T)) return false;
  T tmp;
  prefs.getBytes(key, &tmp, sizeof(T));
  if (tmp.version != version) return false;
  out = tmp;
  return true;
}

template <typename T>
static bool saveBlob(const char *key, const T &v) {
  return prefs.putBytes(key, &v, sizeof(T)) == sizeof(T);
}

void begin() {
  prefs.begin("ro", false);
  Counters &c = counters;
  c.setDefaults();
  defaultCal();
  bool okCnt = loadBlob("cnt", c.cnt, COUNT_STATE_VERSION);
  bool okDays = loadBlob("days", c.days, DAYS_STATE_VERSION);
  bool migrated = false;
  if (!okDays && prefs.getBytesLength("days") == sizeof(DaysStateV1)) {  // 8-day ring from before 30 days
    DaysStateV1 old;
    prefs.getBytes("days", &old, sizeof old);
    if (old.version == 1) {
      c.migrateDaysV1(old);
      migrated = true;
    }
  }
  bool okPre = loadBlob("pre", c.pre, FILTER_GROUP_VERSION);
  bool okMem = loadBlob("mem", c.mem, FILTER_GROUP_VERSION);
  bool okCal = loadBlob("cal", cal, CALIBRATION_VERSION);
  Serial.printf("[store] loaded cnt=%d days=%d%s pre=%d mem=%d cal=%d (0 = defaults)\n",
                okCnt, okDays, migrated ? " (migrated from v1)" : "", okPre, okMem, okCal);
  // Write defaults for anything missing so a fresh board has a full set of keys.
  dirty = (okCnt ? 0 : DIRTY_CNT) | ((okDays && !migrated) ? 0 : DIRTY_DAYS) | (okPre ? 0 : DIRTY_PRE) |
          (okMem ? 0 : DIRTY_MEM) | (okCal ? 0 : DIRTY_CAL);
  if (dirty) saveNow();
}

void markDirty(uint8_t bits) {
  if (bits & ROLLED_OVER) saveRequested = true;
  bits &= ~ROLLED_OVER;
  if (!bits) return;
  if (!dirty) dirtySinceMs = millis();
  dirty |= bits;
}

void requestSave() { saveRequested = true; }

void saveNow() {
  saveRequested = false;
  if (!dirty) return;
  uint32_t t0 = millis();
  bool ok = true;
  // Only rewrite the keys that changed.
  if (dirty & DIRTY_CNT) ok &= saveBlob("cnt", counters.cnt);
  if (dirty & DIRTY_DAYS) ok &= saveBlob("days", counters.days);
  if (dirty & DIRTY_PRE) ok &= saveBlob("pre", counters.pre);
  if (dirty & DIRTY_MEM) ok &= saveBlob("mem", counters.mem);
  if (dirty & DIRTY_CAL) ok &= saveBlob("cal", cal);
  saves++;
  Serial.printf("[store] saved 0x%02x in %lu ms%s\n", dirty, (unsigned long)(millis() - t0),
                ok ? "" : " (WRITE FAILED)");
  if (ok) dirty = 0;
  else dirtySinceMs = millis();  // retry after another interval
}

void loop(uint32_t now) {
  if (!dirty) {
    saveRequested = false;
    return;
  }
  if (saveRequested || now - dirtySinceMs >= SAVE_INTERVAL_MS) saveNow();
}

void wipe() {
  prefs.clear();
  dirty = 0;
  saveRequested = false;
  prefs.end();
  begin();
}

uint32_t saveCount() { return saves; }

void lock() { xSemaphoreTakeRecursive(mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGiveRecursive(mutex); }

}  // namespace store
