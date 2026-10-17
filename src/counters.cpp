#include "counters.h"
#include "config.h"

// --- dates ---------------------------------------------------------------
// Howard Hinnant's days_from_civil / civil_from_days.

static int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

int32_t dateToDays(uint32_t ymd) {
  return daysFromCivil((int32_t)(ymd / 10000), (ymd / 100) % 100, ymd % 100);
}

uint32_t daysToDate(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = (int32_t)yoe + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  return (uint32_t)(y + (m <= 2)) * 10000 + m * 100 + d;
}

bool isValidDate(uint32_t ymd) {
  uint32_t y = ymd / 10000, m = (ymd / 100) % 100, d = ymd % 100;
  if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1) return false;
  return daysToDate(dateToDays(ymd)) == ymd;  // rejects Feb 30 etc.
}

static double clampWaste(double in, double out) { return in > out ? in - out : 0.0; }

// --- counters ------------------------------------------------------------

void Counters::defaultCount() {
  cnt = {};
  cnt.version = COUNT_STATE_VERSION;
}

void Counters::defaultDays() {
  days = {};
  days.version = DAYS_STATE_VERSION;
}

void Counters::defaultFilter(FilterId id) {
  FilterGroup &g = id == FilterId::Prefilter ? pre : mem;
  g = {};
  g.version = FILTER_GROUP_VERSION;
  g.ratingGal = id == FilterId::Prefilter ? PREFILTER_RATING_GAL : MEMBRANE_RATING_GAL;
}

void Counters::setDefaults() {
  defaultCount();
  defaultDays();
  defaultFilter(FilterId::Prefilter);
  defaultFilter(FilterId::Membrane);
}

uint8_t Counters::addFlow(double inGal, double outGal, uint32_t today) {
  if (inGal <= 0 && outGal <= 0) return 0;
  uint8_t dirty = DIRTY_CNT | DIRTY_DAYS | DIRTY_PRE | DIRTY_MEM;

  cnt.allIn += inGal;
  cnt.allOut += outGal;
  pre.usedGal += inGal;
#if MEMBRANE_COUNTS_TOTAL
  mem.usedGal += inGal;
#else
  mem.usedGal += outGal;
#endif

  if (today == 0) {
    days.pendIn += inGal;
    days.pendOut += outGal;
  } else {
    dirty |= rollTo(today);
    days.days[0].inGal += inGal;
    days.days[0].outGal += outGal;
  }
  return dirty;
}

uint8_t Counters::tick(uint32_t today) {
  if (today == 0) return 0;
  return rollTo(today);
}

uint8_t Counters::rollTo(uint32_t today) {
  uint8_t dirty = 0;
  DayBucket *d = days.days;

  if (d[0].date == 0 || !isValidDate(d[0].date)) {
    // First valid time ever (or a corrupt ring): date the ring, keep any values.
    for (int i = 0; i < NUM_DAYS; i++) d[i].date = daysToDate(dateToDays(today) - i);
    dirty |= DIRTY_DAYS;
  } else if (today > d[0].date) {
    int32_t gap = dateToDays(today) - dateToDays(d[0].date);
    if (gap >= NUM_DAYS) {
      for (int i = 0; i < NUM_DAYS; i++) d[i] = {};
    } else {
      for (int i = NUM_DAYS - 1; i >= gap; i--) d[i] = d[i - gap];
      for (int i = 0; i < gap; i++) d[i] = {};
    }
    for (int i = 0; i < NUM_DAYS; i++) d[i].date = daysToDate(dateToDays(today) - i);
    dirty |= DIRTY_DAYS | ROLLED_OVER;
  }
  // today < d[0].date: the clock went backwards (bad NTP answer). Keep adding to the
  // newest bucket rather than rewriting history; it self-heals when time moves forward.

  if (days.pendIn > 0 || days.pendOut > 0) {
    d[0].inGal += days.pendIn;
    d[0].outGal += days.pendOut;
    days.pendIn = days.pendOut = 0;
    dirty |= DIRTY_DAYS | ROLLED_OVER;  // save promptly once pre-sync water is placed
  }

  if (cnt.since == 0) {
    cnt.since = today;
    dirty |= DIRTY_CNT;
  }
  if (pre.installDate == 0) {
    pre.installDate = today;
    dirty |= DIRTY_PRE;
  }
  if (mem.installDate == 0) {
    mem.installDate = today;
    dirty |= DIRTY_MEM;
  }
  return dirty;
}

uint8_t Counters::resetCounters(uint32_t today) {
  cnt.allIn = cnt.allOut = 0;
  cnt.since = today;  // 0 if not synced: filled in at the next tick with a valid date
  for (auto &b : days.days) b.inGal = b.outGal = 0;
  days.pendIn = days.pendOut = 0;
  return DIRTY_CNT | DIRTY_DAYS;
}

uint8_t Counters::setFilter(FilterId id, uint32_t installDate, float ratingGal) {
  FilterGroup &g = id == FilterId::Prefilter ? pre : mem;
  uint8_t bit = id == FilterId::Prefilter ? DIRTY_PRE : DIRTY_MEM;
  uint8_t dirty = 0;
  if (installDate != g.installDate) {
    g.installDate = installDate;
    g.usedGal = 0;
    dirty = bit;
  }
  if (ratingGal > 0 && ratingGal != g.ratingGal) {
    g.ratingGal = ratingGal;
    dirty = bit;
  }
  return dirty;
}

Totals Counters::today(uint32_t today) const {
  if (today == 0) return {days.pendOut, clampWaste(days.pendIn, days.pendOut)};
  const DayBucket &b = days.days[0];
  return {b.outGal, clampWaste(b.inGal, b.outGal)};
}

Totals Counters::week(uint32_t today) const { return lastDays(7, today); }
Totals Counters::month(uint32_t today) const { return lastDays(30, today); }

void Counters::migrateDaysV1(const DaysStateV1 &old) {
  defaultDays();
  for (int i = 0; i < NUM_DAYS_V1; i++) days.days[i] = old.days[i];
  days.pendIn = old.pendIn;
  days.pendOut = old.pendOut;
  if (isValidDate(days.days[0].date)) {
    for (int i = NUM_DAYS_V1; i < NUM_DAYS; i++)
      days.days[i].date = daysToDate(dateToDays(days.days[0].date) - i);
  }
}

Totals Counters::lastDays(int n, uint32_t today) const {
  double in = 0, out = 0;
  for (int i = 0; i < n && i < NUM_DAYS; i++) {
    in += days.days[i].inGal;
    out += days.days[i].outGal;
  }
  if (today == 0) {
    in += days.pendIn;
    out += days.pendOut;
  }
  return {out, clampWaste(in, out)};
}

Totals Counters::all() const { return {cnt.allOut, clampWaste(cnt.allIn, cnt.allOut)}; }
