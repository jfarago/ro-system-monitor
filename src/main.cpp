// Phase 1: sensor bring-up. Serial output only.
//
// Serial commands (send a line, 115200 baud):
//   r        zero the pulse/gallon counters (start of a calibration fill)
//   g        print measured pulses/gal for each sensor since the last 'r'
//            (use after filling exactly 1 gal)
//   t <ppm>  enter the reading from a handheld TDS pen; prints the kTDS to use
#include <Arduino.h>
#include "config.h"

constexpr uint32_t TDS_SAMPLE_MS = 40;
constexpr int TDS_SAMPLES = 30;

struct FlowCounter {
  volatile uint32_t pulses = 0;
  volatile uint32_t lastEdgeUs = 0;
  uint32_t lastSnapshot = 0;
};

static FlowCounter flowIn, flowOut;
static portMUX_TYPE flowMux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR onEdge(FlowCounter &f) {
  uint32_t now = micros();
  if (now - f.lastEdgeUs < FLOW_DEBOUNCE_US) return;
  f.lastEdgeUs = now;
  f.pulses++;
}
static void IRAM_ATTR isrIn() { onEdge(flowIn); }
static void IRAM_ATTR isrOut() { onEdge(flowOut); }

static uint32_t readPulses(const FlowCounter &f) {
  portENTER_CRITICAL(&flowMux);
  uint32_t p = f.pulses;
  portEXIT_CRITICAL(&flowMux);
  return p;
}

static uint32_t tdsBuf[TDS_SAMPLES];
static int tdsIdx = 0;
static bool tdsFull = false;
static float kTds = K_TDS;
static uint32_t lastTdsSampleMs = 0;
static uint32_t lastReportMs = 0;

static uint32_t median(uint32_t *src, int n) {
  uint32_t tmp[TDS_SAMPLES];
  memcpy(tmp, src, n * sizeof(uint32_t));
  for (int i = 1; i < n; i++) {
    uint32_t v = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = v;
  }
  return tmp[n / 2];
}

// DFRobot-style TDS curve, mV in, ppm out (before kTDS).
static float mvToPpm(float mv) {
  float v = mv / 1000.0f;
  float c = v / (1.0f + 0.02f * (WATER_TEMP_C - 25.0f));
  return (133.42f * c * c * c - 255.86f * c * c + 857.39f * c) * 0.5f;
}

static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line == "r") {
    portENTER_CRITICAL(&flowMux);
    flowIn.pulses = 0;
    flowOut.pulses = 0;
    portEXIT_CRITICAL(&flowMux);
    flowIn.lastSnapshot = flowOut.lastSnapshot = 0;
    Serial.println("# counters zeroed");
  } else if (line == "g") {
    Serial.printf("# measured since 'r' (for exactly 1 gal): IN %lu pulses/gal, OUT %lu pulses/gal "
                  "(config: %.0f / %.0f)\n",
                  (unsigned long)readPulses(flowIn), (unsigned long)readPulses(flowOut),
                  K_FLOW_IN_PER_GAL, K_FLOW_OUT_PER_GAL);
  } else if (line.startsWith("t ")) {
    float ref = line.substring(2).toFloat();
    uint32_t mv = median(tdsBuf, tdsFull ? TDS_SAMPLES : max(tdsIdx, 1));
    float raw = mvToPpm(mv);
    if (ref > 0 && raw > 0) {
      kTds = ref / raw;
      Serial.printf("# pen=%.0f ppm, raw=%.1f ppm at %lu mV -> set K_TDS = %.3f in config.h "
                    "(applied for this session)\n", ref, raw, (unsigned long)mv, kTds);
    } else {
      Serial.println("# need a positive pen reading and nonzero probe signal");
    }
  } else {
    Serial.println("# commands: r | g | t <ppm>");
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  // Flow sensors: internal pull-ups are harmless with a divider and needed if open-collector.
  pinMode(PIN_FLOW_IN, INPUT_PULLUP);
  pinMode(PIN_FLOW_OUT, INPUT_PULLUP);
  analogSetPinAttenuation(PIN_TDS, ADC_11db);

  Serial.println("\n=== RO monitor: Phase 1 sensor bring-up ===");
  Serial.printf("reset reason: %d (1=power-on, 3=sw, 4=panic, 7/8=wdt)\n", (int)esp_reset_reason());
  Serial.printf("idle level at boot: FLOW_IN(GPIO%d)=%d  FLOW_OUT(GPIO%d)=%d  (expect 1 with no flow if signal idles high)\n",
                PIN_FLOW_IN, digitalRead(PIN_FLOW_IN), PIN_FLOW_OUT, digitalRead(PIN_FLOW_OUT));
  Serial.printf("K: IN=%.0f OUT=%.0f pulses/gal, kTDS=%.3f, feed=%.0f ppm\n",
                K_FLOW_IN_PER_GAL, K_FLOW_OUT_PER_GAL, kTds, FEED_TDS_PPM);
  Serial.println("commands: r (zero counters) | g (pulses/gal for a 1 gal fill) | t <ppm> (calibrate TDS)");

  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_IN), isrIn, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_OUT), isrOut, FALLING);
}

void loop() {
  uint32_t now = millis();

  if (now - lastTdsSampleMs >= TDS_SAMPLE_MS) {
    lastTdsSampleMs = now;
    tdsBuf[tdsIdx] = analogReadMilliVolts(PIN_TDS);
    tdsIdx = (tdsIdx + 1) % TDS_SAMPLES;
    if (tdsIdx == 0) tdsFull = true;
  }

  handleSerial();

  if (now - lastReportMs >= FLOW_SAMPLE_MS) {
    float dt = (now - lastReportMs) / 1000.0f;
    lastReportMs = now;

    uint32_t pIn = readPulses(flowIn), pOut = readPulses(flowOut);
    uint32_t dIn = pIn - flowIn.lastSnapshot, dOut = pOut - flowOut.lastSnapshot;
    flowIn.lastSnapshot = pIn;
    flowOut.lastSnapshot = pOut;

    float galIn = pIn / K_FLOW_IN_PER_GAL, galOut = pOut / K_FLOW_OUT_PER_GAL;
    float hzIn = dIn / dt, hzOut = dOut / dt;
    float lpmIn = hzIn / 38.0f, lpmOut = hzOut / 38.0f;  // F = 38 * Q
    float galWaste = max(0.0f, galIn - galOut);

    uint32_t mv = median(tdsBuf, tdsFull ? TDS_SAMPLES : max(tdsIdx, 1));
    float rawPpm = mvToPpm(mv);
    float ppm = rawPpm * kTds;
    float rej = constrain((1.0f - ppm / FEED_TDS_PPM) * 100.0f, 0.0f, 100.0f);

    Serial.printf("IN  d=%3lu %5.1fHz %4.2fL/m tot=%7lu %7.3fgal | OUT d=%3lu %5.1fHz %4.2fL/m tot=%7lu %7.3fgal | waste=%7.3fgal\n",
                  (unsigned long)dIn, hzIn, lpmIn, (unsigned long)pIn, galIn,
                  (unsigned long)dOut, hzOut, lpmOut, (unsigned long)pOut, galOut, galWaste);
    Serial.printf("TDS %4lumV raw=%6.1fppm cal=%6.1fppm rej=%5.1f%% | pins IN=%d OUT=%d\n",
                  (unsigned long)mv, rawPpm, ppm, rej,
                  digitalRead(PIN_FLOW_IN), digitalRead(PIN_FLOW_OUT));
  }
}
