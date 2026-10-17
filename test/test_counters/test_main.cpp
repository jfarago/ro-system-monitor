// Host tests for counters.cpp: `pio test -e native`
#include <unity.h>
#include "counters.h"

static Counters c;

void setUp() { c.setDefaults(); }
void tearDown() {}

static void test_dates() {
  TEST_ASSERT_EQUAL_INT32(0, dateToDays(19700101));
  TEST_ASSERT_EQUAL_UINT32(20261017, daysToDate(dateToDays(20261017)));
  TEST_ASSERT_EQUAL_UINT32(20240301, daysToDate(dateToDays(20240228) + 2));  // leap year
  TEST_ASSERT_EQUAL_UINT32(20250301, daysToDate(dateToDays(20250228) + 1));
  TEST_ASSERT_EQUAL_UINT32(20270101, daysToDate(dateToDays(20261231) + 1));
  TEST_ASSERT_TRUE(isValidDate(20240229));
  TEST_ASSERT_FALSE(isValidDate(20250229));
  TEST_ASSERT_FALSE(isValidDate(20261301));
  TEST_ASSERT_FALSE(isValidDate(20261000));
  TEST_ASSERT_FALSE(isValidDate(0));
}

static void test_first_sync_dates_ring_and_fills_since() {
  c.tick(20261017);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.days.days[0].date);
  TEST_ASSERT_EQUAL_UINT32(20261010, c.days.days[7].date);
  TEST_ASSERT_EQUAL_UINT32(20260918, c.days.days[NUM_DAYS - 1].date);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.cnt.since);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.pre.installDate);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.mem.installDate);
}

static void test_waste_is_in_minus_out_clamped() {
  c.addFlow(3.0, 1.0, 20261017);
  Totals t = c.today(20261017);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, t.clean);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, t.waste);
  c.addFlow(0, 5.0, 20261017);  // faucet draws from the tank: more out than in today
  t = c.today(20261017);
  TEST_ASSERT_EQUAL_DOUBLE(6.0, t.clean);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, t.waste);
}

static void test_filters_count_total() {
  c.addFlow(4.0, 1.0, 20261017);
  TEST_ASSERT_EQUAL_DOUBLE(4.0, c.pre.usedGal);
  TEST_ASSERT_EQUAL_DOUBLE(4.0, c.mem.usedGal);  // MEMBRANE_COUNTS_TOTAL = 1
}

static void test_midnight_rollover() {
  c.addFlow(2.0, 1.0, 20261017);
  uint8_t r = c.addFlow(4.0, 1.0, 20261018);
  TEST_ASSERT_TRUE(r & ROLLED_OVER);
  TEST_ASSERT_EQUAL_UINT32(20261018, c.days.days[0].date);
  TEST_ASSERT_EQUAL_DOUBLE(4.0, c.days.days[0].inGal);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.days.days[1].date);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, c.days.days[1].inGal);
  Totals w = c.week(20261018);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, w.clean);
  TEST_ASSERT_EQUAL_DOUBLE(4.0, w.waste);
  TEST_ASSERT_EQUAL_DOUBLE(6.0, c.all().clean + c.all().waste);
}

static void test_gap_fills_zero_days() {
  c.addFlow(2.0, 1.0, 20261017);
  c.tick(20261020);  // powered off for two days
  TEST_ASSERT_EQUAL_UINT32(20261020, c.days.days[0].date);
  TEST_ASSERT_EQUAL_UINT32(20261019, c.days.days[1].date);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.days.days[1].inGal);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.days.days[2].inGal);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.days.days[3].date);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, c.days.days[3].inGal);
}

static void test_week_excludes_eighth_day() {
  for (uint32_t d = 20261010; d <= 20261017; d++) c.addFlow(1.0, 1.0, d);  // 8 consecutive days
  TEST_ASSERT_EQUAL_DOUBLE(7.0, c.week(20261017).clean);
  TEST_ASSERT_EQUAL_DOUBLE(8.0, c.all().clean);
}

static void test_month_is_30_days() {
  for (int i = 0; i < 31; i++) c.addFlow(1.0, 1.0, daysToDate(dateToDays(20260901) + i));  // 09-01..10-01
  TEST_ASSERT_EQUAL_DOUBLE(30.0, c.month(20261001).clean);  // 31st-oldest day (09-01) falls out
  TEST_ASSERT_EQUAL_DOUBLE(7.0, c.week(20261001).clean);
  TEST_ASSERT_EQUAL_DOUBLE(31.0, c.all().clean);
}

static void test_migrate_v1_ring() {
  DaysStateV1 old = {};
  old.version = 1;
  for (int i = 0; i < NUM_DAYS_V1; i++) old.days[i] = {daysToDate(dateToDays(20261017) - i), 2.0, 1.0};
  old.pendIn = 0.5;
  c.migrateDaysV1(old);
  TEST_ASSERT_EQUAL_UINT16(DAYS_STATE_VERSION, c.days.version);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, c.days.days[7].outGal);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.days.days[8].outGal);
  TEST_ASSERT_EQUAL_UINT32(20260918, c.days.days[NUM_DAYS - 1].date);
  TEST_ASSERT_EQUAL_DOUBLE(0.5, c.days.pendIn);
  TEST_ASSERT_EQUAL_DOUBLE(8.0, c.month(20261017).clean);
}

static void test_long_gap_clears_ring() {
  c.addFlow(2.0, 1.0, 20261017);
  c.tick(20261217);  // 61 days
  for (int i = 0; i < NUM_DAYS; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, c.days.days[i].inGal);
  TEST_ASSERT_EQUAL_UINT32(20261118, c.days.days[NUM_DAYS - 1].date);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, c.cnt.allIn);
}

static void test_rollover_across_month_and_dst() {
  c.addFlow(1.0, 0, 20261031);
  c.tick(20261101);  // DST ends 2026-11-01; dates are local so nothing special
  TEST_ASSERT_EQUAL_UINT32(20261101, c.days.days[0].date);
  TEST_ASSERT_EQUAL_UINT32(20261031, c.days.days[1].date);
}

static void test_pending_before_sync() {
  c.addFlow(3.0, 1.0, 0);  // first boot, no Wi-Fi
  TEST_ASSERT_EQUAL_DOUBLE(3.0, c.cnt.allIn);
  TEST_ASSERT_EQUAL_DOUBLE(3.0, c.pre.usedGal);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, c.today(0).clean);
  TEST_ASSERT_EQUAL_UINT32(0, c.days.days[0].date);
  uint8_t r = c.tick(20261017);
  TEST_ASSERT_TRUE(r & ROLLED_OVER);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.days.pendIn);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, c.today(20261017).clean);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, c.today(20261017).waste);
  TEST_ASSERT_EQUAL_DOUBLE(3.0, c.cnt.allIn);  // not double counted
}

static void test_pending_after_reboot_goes_to_new_day() {
  c.addFlow(1.0, 1.0, 20261017);
  c.addFlow(2.0, 2.0, 0);  // rebooted on the 18th with no Wi-Fi
  c.tick(20261018);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, c.days.days[1].outGal);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, c.days.days[0].outGal);
}

static void test_clock_backwards_keeps_newest_bucket() {
  c.addFlow(1.0, 1.0, 20261017);
  uint8_t r = c.addFlow(1.0, 1.0, 20261015);
  TEST_ASSERT_FALSE(r & ROLLED_OVER);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.days.days[0].date);
  TEST_ASSERT_EQUAL_DOUBLE(2.0, c.days.days[0].outGal);
}

static void test_reset_counters_keeps_filters() {
  c.addFlow(5.0, 2.0, 20261017);
  c.addFlow(1.0, 0, 0);
  c.resetCounters(20261018);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.all().clean);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.week(20261017).clean + c.week(20261017).waste);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.days.pendIn);
  TEST_ASSERT_EQUAL_UINT32(20261018, c.cnt.since);
  TEST_ASSERT_EQUAL_DOUBLE(6.0, c.pre.usedGal);
}

static void test_reset_before_sync_fills_since_later() {
  c.addFlow(1.0, 0, 20261017);
  c.resetCounters(0);
  TEST_ASSERT_EQUAL_UINT32(0, c.cnt.since);
  c.tick(20261017);
  TEST_ASSERT_EQUAL_UINT32(20261017, c.cnt.since);
}

static void test_install_date_rule() {
  c.tick(20261017);
  c.addFlow(10.0, 3.0, 20261017);
  // rating only: no reset
  uint8_t r = c.setFilter(FilterId::Prefilter, 20261017, 2000);
  TEST_ASSERT_EQUAL_UINT8(DIRTY_PRE, r);
  TEST_ASSERT_EQUAL_FLOAT(2000, c.pre.ratingGal);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, c.pre.usedGal);
  // same values: nothing changes
  TEST_ASSERT_EQUAL_UINT8(0, c.setFilter(FilterId::Prefilter, 20261017, 2000));
  // changed date: reset that group only
  r = c.setFilter(FilterId::Prefilter, 20261016, 0);
  TEST_ASSERT_EQUAL_UINT8(DIRTY_PRE, r);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, c.pre.usedGal);
  TEST_ASSERT_EQUAL_FLOAT(2000, c.pre.ratingGal);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, c.mem.usedGal);
}

static void test_set_used() {
  c.tick(20261017);
  c.addFlow(10.0, 3.0, 20261017);
  TEST_ASSERT_EQUAL_UINT8(DIRTY_MEM, c.setUsed(FilterId::Membrane, 1248));
  TEST_ASSERT_EQUAL_DOUBLE(1248, c.mem.usedGal);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, c.pre.usedGal);
  TEST_ASSERT_EQUAL_UINT8(0, c.setUsed(FilterId::Membrane, 1248));  // unchanged
  TEST_ASSERT_EQUAL_UINT8(0, c.setUsed(FilterId::Membrane, -1));    // rejected
  TEST_ASSERT_EQUAL_DOUBLE(1248, c.mem.usedGal);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dates);
  RUN_TEST(test_first_sync_dates_ring_and_fills_since);
  RUN_TEST(test_waste_is_in_minus_out_clamped);
  RUN_TEST(test_filters_count_total);
  RUN_TEST(test_set_used);
  RUN_TEST(test_midnight_rollover);
  RUN_TEST(test_gap_fills_zero_days);
  RUN_TEST(test_week_excludes_eighth_day);
  RUN_TEST(test_month_is_30_days);
  RUN_TEST(test_migrate_v1_ring);
  RUN_TEST(test_long_gap_clears_ring);
  RUN_TEST(test_rollover_across_month_and_dst);
  RUN_TEST(test_pending_before_sync);
  RUN_TEST(test_pending_after_reboot_goes_to_new_day);
  RUN_TEST(test_clock_backwards_keeps_newest_bucket);
  RUN_TEST(test_reset_counters_keeps_filters);
  RUN_TEST(test_reset_before_sync_fills_since_later);
  RUN_TEST(test_install_date_rule);
  return UNITY_END();
}
