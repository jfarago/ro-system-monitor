// NVS persistence with batched writes (PLAN §4.5).
// Changes are held in RAM and written when something is dirty and either
// SAVE_INTERVAL_MS has passed since the first unsaved change, or a save was
// requested (flow event ended, day rolled over, setting saved, counters reset).
#pragma once
#include <stdint.h>
#include "counters.h"

struct Calibration {  // NVS key "cal"
  uint16_t version;
  float kIn;   // pulses/gal
  float kOut;  // pulses/gal
  float kTds;
  float waterTempC;
};
constexpr uint16_t CALIBRATION_VERSION = 1;
constexpr uint8_t DIRTY_CAL = 1 << 4;

namespace store {

extern Counters counters;
extern Calibration cal;

void begin();                 // load everything (defaults for missing/mismatched blobs)
void markDirty(uint8_t bits); // DIRTY_* bits; ROLLED_OVER also requests a save
void requestSave();
void loop(uint32_t nowMs);
void saveNow();
void wipe();                  // erase the namespace and reload defaults
uint32_t saveCount();         // NVS writes since boot (diagnostics)

// Recursive mutex guarding `counters`, `cal` and the dirty state. The web handlers run on the
// async_tcp task, so anything touching them outside loop() must hold it (loop() holds it too).
void lock();
void unlock();
struct Lock {
  Lock() { lock(); }
  ~Lock() { unlock(); }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};

}  // namespace store
