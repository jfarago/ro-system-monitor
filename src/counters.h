// Water counters, 8-day bucket ring, pre-sync pending bucket, filter life.
// Pure C++ (no Arduino) so it can be unit tested on the host: `pio test -e native`.
// Dates are local calendar dates as yyyymmdd; 0 means "unknown / time not synced".
#pragma once
#include <stdint.h>

constexpr int NUM_DAYS = 8;  // today + 7 previous days

// Persisted structs (NVS blobs). Bump the version when a layout changes; a
// mismatched blob is discarded and defaults are used.
struct DayBucket {
  uint32_t date;  // yyyymmdd, 0 = unset
  double inGal;
  double outGal;
};

struct CountState {  // NVS key "cnt"
  uint16_t version;
  double allIn;
  double allOut;
  uint32_t since;  // yyyymmdd of the last reset (countingSince); 0 = fill at next time sync
};

struct DaysState {  // NVS key "days"
  uint16_t version;
  DayBucket days[NUM_DAYS];  // [0] = newest (today once time is valid)
  double pendIn;             // flow measured while the clock was not valid
  double pendOut;
};

struct FilterGroup {  // NVS keys "pre", "mem"
  uint16_t version;
  uint32_t installDate;  // yyyymmdd; 0 = fill at next time sync
  float ratingGal;
  double usedGal;
};

constexpr uint16_t COUNT_STATE_VERSION = 1;
constexpr uint16_t DAYS_STATE_VERSION = 1;
constexpr uint16_t FILTER_GROUP_VERSION = 1;

struct Totals {
  double clean;
  double waste;
};

// Bits returned by mutators so the caller knows which NVS keys to rewrite.
enum : uint8_t {
  DIRTY_CNT = 1 << 0,
  DIRTY_DAYS = 1 << 1,
  DIRTY_PRE = 1 << 2,
  DIRTY_MEM = 1 << 3,
  ROLLED_OVER = 1 << 7,  // a new day was opened (save now)
};

enum class FilterId { Prefilter, Membrane };

// Date helpers (proleptic Gregorian).
bool isValidDate(uint32_t yyyymmdd);
int32_t dateToDays(uint32_t yyyymmdd);  // days since 1970-01-01
uint32_t daysToDate(int32_t days);

class Counters {
 public:
  CountState cnt;
  DaysState days;
  FilterGroup pre;
  FilterGroup mem;

  Counters() { setDefaults(); }
  void setDefaults();
  void defaultCount();
  void defaultDays();
  void defaultFilter(FilterId id);

  // Add measured flow. today = current local date, or 0 if the clock is not valid.
  uint8_t addFlow(double inGal, double outGal, uint32_t today);

  // Call regularly with the current date (0 if not valid). Handles first-time
  // init, midnight rollover, gap filling and merging the pending bucket.
  uint8_t tick(uint32_t today);

  // Zero every water period and set countingSince = today. Filters untouched.
  uint8_t resetCounters(uint32_t today);

  // A changed install date resets that group's used counter; a rating change alone does not.
  // installDate must be a valid date (check with isValidDate). ratingGal <= 0 keeps the current rating.
  uint8_t setFilter(FilterId id, uint32_t installDate, float ratingGal);

  // Periods. With today == 0 (clock not valid) "today" is the pending bucket only
  // and "week" is the stored ring plus pending (approximate until sync).
  Totals today(uint32_t today) const;
  Totals week(uint32_t today) const;
  Totals all() const;

 private:
  uint8_t rollTo(uint32_t today);
};
