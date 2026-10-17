#include "tds.h"
#include <Arduino.h>
#include "config.h"

namespace tds {

static uint32_t buf[TDS_SAMPLES];
static int idx = 0;
static bool full = false;
static uint32_t lastSampleMs = 0;
static uint32_t lastSmoothMs = 0;
static float smoothMv = 0;  // exponential moving average of the windowed median
static bool smoothInit = false;

uint32_t medianMv();

void begin() { analogSetPinAttenuation(PIN_TDS, ADC_11db); }

void update(uint32_t now) {
  if (now - lastSampleMs < TDS_SAMPLE_MS) return;
  lastSampleMs = now;
  buf[idx] = analogReadMilliVolts(PIN_TDS);
  idx = (idx + 1) % TDS_SAMPLES;
  if (idx == 0) full = true;

  if (now - lastSmoothMs >= TDS_SMOOTH_TICK_MS && (full || idx > 0)) {
    lastSmoothMs = now;
    float mv = (float)medianMv();
    smoothMv = smoothInit ? smoothMv + TDS_SMOOTH_ALPHA * (mv - smoothMv) : mv;
    smoothInit = true;
  }
}

uint32_t medianMv() {
  int n = full ? TDS_SAMPLES : idx;
  if (n == 0) return 0;
  uint32_t tmp[TDS_SAMPLES];
  memcpy(tmp, buf, n * sizeof(uint32_t));
  for (int i = 1; i < n; i++) {
    uint32_t v = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = v;
  }
  return tmp[n / 2];
}

float rawPpm(float waterTempC) {
  float v = (smoothInit ? smoothMv : (float)medianMv()) / 1000.0f;
  float c = v / (1.0f + 0.02f * (waterTempC - 25.0f));
  return (133.42f * c * c * c - 255.86f * c * c + 857.39f * c) * 0.5f;
}

float ppm(float kTds, float waterTempC) { return rawPpm(waterTempC) * kTds; }

float rejectionPct(float productPpm) {
  return constrain((1.0f - productPpm / FEED_TDS_PPM) * 100.0f, 0.0f, 100.0f);
}

}  // namespace tds
