// RO system monitor firmware.
//
// Serial commands (send a line, 115200 baud):
//   s                          status
//   r                          zero the calibration pulse counters (start of a 1 gal fill)
//   g                          measured pulses/gal per sensor since 'r'
//   k in|out <pulses/gal>      set a flow K-factor (saved)
//   t <ppm>                    calibrate kTDS against a handheld pen reading (saved)
//   filter pre|mem <yyyy-mm-dd> [ratingGal]   set install date / rating (changed date resets used)
//   reset                      zero water counters (Today, 7 days, All); filters untouched
//   inject <inGal> <outGal>    TEST: add volume as if measured
//   save | reboot | wipe yes   force NVS save / restart / erase all stored data
#include <Arduino.h>
#include "clock.h"
#include "config.h"
#include "counters.h"
#include "flow.h"
#include "net.h"
#include "store.h"
#include "tds.h"
#include "web.h"

static Counters &counters = store::counters;
static Calibration &cal = store::cal;

static uint32_t lastFlowMs = 0;
static uint32_t lastPulseMs = 0;
static bool anyPulse = false;
static bool inFlowEvent = false;
static bool timeWasValid = false;
static uint32_t calPulsesIn = 0, calPulsesOut = 0;

static bool flowing(uint32_t now) { return anyPulse && now - lastPulseMs < FLOW_ACTIVE_MS; }
bool appFlowing() { return flowing(millis()); }  // used by web.cpp

static void printStatus() {
  store::Lock guard;
  uint32_t now = millis();
  uint32_t today = clk::today();
  char iso[32], since[12], preD[12], memD[12];
  clk::isoTime(iso, sizeof iso);
  clk::formatDate(counters.cnt.since, since, sizeof since);
  clk::formatDate(counters.pre.installDate, preD, sizeof preD);
  clk::formatDate(counters.mem.installDate, memD, sizeof memD);
  Totals t = counters.today(today), w = counters.week(today), m = counters.month(today), a = counters.all();
  float ppm = tds::ppm(cal.kTds, cal.waterTempC);

  Serial.printf("time %s (%s) | uptime %lu s | wifi %s%s rssi %d | flowing %d\n",
                iso[0] ? iso : "-", today ? "valid" : "NOT SYNCED", (unsigned long)(now / 1000),
                net::connected() ? "up" : "down", net::apActive() ? " +AP" : "", net::rssi(),
                flowing(now));
  Serial.printf("tds %.1f ppm (%lu mV) rejection %.1f%% | K in %.0f out %.0f kTds %.3f T %.1fC\n", ppm,
                (unsigned long)tds::medianMv(), tds::rejectionPct(ppm), cal.kIn, cal.kOut, cal.kTds,
                cal.waterTempC);
  Serial.printf("today clean %.3f waste %.3f | week clean %.3f waste %.3f | 30d clean %.3f waste %.3f | all clean %.3f waste %.3f | since %s\n",
                t.clean, t.waste, w.clean, w.waste, m.clean, m.waste, a.clean, a.waste, since[0] ? since : "(at sync)");
  Serial.printf("pending in %.3f out %.3f | allIn %.3f allOut %.3f\n", counters.days.pendIn,
                counters.days.pendOut, counters.cnt.allIn, counters.cnt.allOut);
  for (int i = 0; i < 8; i++) {  // newest 8 only; the ring holds NUM_DAYS
    const DayBucket &b = counters.days.days[i];
    Serial.printf("  day[%d] %lu in %.3f out %.3f\n", i, (unsigned long)b.date, b.inGal, b.outGal);
  }
  Serial.printf("prefilter %s %.0f gal used %.3f | membrane %s %.0f gal used %.3f (counts %s)\n",
                preD, counters.pre.ratingGal, counters.pre.usedGal, memD, counters.mem.ratingGal,
                counters.mem.usedGal, MEMBRANE_COUNTS_TOTAL ? "total" : "clean");
  Serial.printf("nvs saves since boot %lu\n", (unsigned long)store::saveCount());
}

static uint32_t parseDate(const char *s) {
  unsigned y, m, d;
  if (sscanf(s, "%u-%u-%u", &y, &m, &d) != 3) return 0;
  uint32_t v = y * 10000 + m * 100 + d;
  return isValidDate(v) ? v : 0;
}

static void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;
  store::Lock guard;
  uint32_t today = clk::today();

  if (line == "s") {
    printStatus();
  } else if (line == "r") {
    calPulsesIn = calPulsesOut = 0;
    Serial.println("# calibration counters zeroed");
  } else if (line == "g") {
    Serial.printf("# since 'r' (for exactly 1 gal): IN %lu pulses/gal, OUT %lu pulses/gal (K: %.0f / %.0f)\n",
                  (unsigned long)calPulsesIn, (unsigned long)calPulsesOut, cal.kIn, cal.kOut);
  } else if (line.startsWith("k ")) {
    char which[8];
    float k;
    if (sscanf(line.c_str() + 2, "%7s %f", which, &k) == 2 && k > 100) {
      if (!strcmp(which, "in")) cal.kIn = k;
      else if (!strcmp(which, "out")) cal.kOut = k;
      else { Serial.println("# usage: k in|out <pulses/gal>"); return; }
      store::markDirty(DIRTY_CAL);
      store::requestSave();
      Serial.printf("# K %s = %.0f\n", which, k);
    } else {
      Serial.println("# usage: k in|out <pulses/gal>");
    }
  } else if (line.startsWith("t ")) {
    float ref = line.substring(2).toFloat();
    float raw = tds::rawPpm(cal.waterTempC);
    if (ref > 0 && raw > 0) {
      cal.kTds = ref / raw;
      store::markDirty(DIRTY_CAL);
      store::requestSave();
      Serial.printf("# pen=%.0f ppm, raw=%.1f ppm -> kTDS = %.3f (saved)\n", ref, raw, cal.kTds);
    } else {
      Serial.println("# need a positive pen reading and nonzero probe signal");
    }
  } else if (line.startsWith("filter ")) {
    char which[8], date[16];
    float rating = 0;
    int n = sscanf(line.c_str() + 7, "%7s %15s %f", which, date, &rating);
    uint32_t d = n >= 2 ? parseDate(date) : 0;
    bool isPre = !strcmp(which, "pre"), isMem = !strcmp(which, "mem");
    if (n < 2 || !d || (!isPre && !isMem)) {
      Serial.println("# usage: filter pre|mem <yyyy-mm-dd> [ratingGal]");
      return;
    }
    store::markDirty(counters.setFilter(isPre ? FilterId::Prefilter : FilterId::Membrane, d, rating));
    store::requestSave();
    Serial.println("# filter updated");
  } else if (line == "reset") {
    store::markDirty(counters.resetCounters(today));
    store::requestSave();
    Serial.println("# water counters reset");
  } else if (line.startsWith("inject ")) {
    double in = 0, out = 0;
    if (sscanf(line.c_str() + 7, "%lf %lf", &in, &out) == 2 && in >= 0 && out >= 0) {
      store::markDirty(counters.addFlow(in, out, today));
      Serial.printf("# injected in %.3f out %.3f gal (%s)\n", in, out, today ? "today" : "pending");
    } else {
      Serial.println("# usage: inject <inGal> <outGal>");
    }
  } else if (line == "save") {
    store::saveNow();
  } else if (line == "reboot") {
    store::saveNow();
    Serial.flush();
    ESP.restart();
  } else if (line == "wipe yes") {
    store::wipe();
    Serial.println("# NVS wiped, defaults loaded");
  } else {
    Serial.println("# commands: s | r | g | k in|out <ppg> | t <ppm> | filter pre|mem <date> [gal] | "
                   "reset | inject <in> <out> | save | reboot | wipe yes");
  }
}

static void pollSerial() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (line.length()) handleCommand(line);
      line = "";
    } else if (line.length() < 80) {
      line += ch;
    }
  }
}

static void updateFlow(uint32_t now, uint32_t today) {
  flow::Pulses d = flow::takeDelta();
  calPulsesIn += d.in;
  calPulsesOut += d.out;
  if (d.in || d.out) {
    lastPulseMs = now;
    anyPulse = true;
    inFlowEvent = true;
    store::markDirty(counters.addFlow(d.in / cal.kIn, d.out / cal.kOut, today));
  } else if (inFlowEvent && now - lastPulseMs >= FLOW_EVENT_END_MS) {
    inFlowEvent = false;
    store::requestSave();
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  Serial.println("\n=== RO monitor ===");
  Serial.printf("reset reason: %d (1=power-on, 3=sw, 4=panic, 7/8=wdt)\n", (int)esp_reset_reason());

  clk::begin();
  store::begin();
  tds::begin();
  flow::begin();
  net::begin();
  web::begin();
  printStatus();
}

void loop() {
  uint32_t now = millis();
  tds::update(now);
  net::loop(now);
  pollSerial();

  if (now - lastFlowMs >= FLOW_SAMPLE_MS) {
    lastFlowMs = now;
    store::Lock guard;
    uint32_t today = clk::today();
    if (today && !timeWasValid) {
      char iso[32];
      clk::isoTime(iso, sizeof iso);
      Serial.printf("[clock] time valid: %s\n", iso);
    }
    timeWasValid = today != 0;
    // Roll the day / merge pending before adding this tick's flow.
    store::markDirty(counters.tick(today));
    updateFlow(now, today);
  }

  store::Lock guard;
  store::loop(now);
}
