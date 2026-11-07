#include "battery.h"
#include <Arduino.h>
#include "config.h"

namespace battery {

static volatile float smoothV = -1.0f;  // read from the web task, written from loop()
static uint32_t lastSampleMs = 0;
static uint32_t lastLogMs = 0;
static bool logged = false;

void begin() {
  pinMode(PIN_VBAT, INPUT);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);  // 0..~3.1 V at the pin; 4.2 V battery reads 2.1 V
}

static float readVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < VBAT_SAMPLES; i++) mv += analogReadMilliVolts(PIN_VBAT);
  return (float)mv / VBAT_SAMPLES / 1000.0f * VBAT_DIVIDER * VBAT_CAL;
}

void update(uint32_t now) {
  bool first = smoothV < 0;
  if (!first && now - lastSampleMs < VBAT_SAMPLE_MS) return;
  lastSampleMs = now;

  float v = readVolts();
  smoothV = first ? v : smoothV + VBAT_SMOOTH_ALPHA * (v - smoothV);

  if (!logged || now - lastLogMs >= VBAT_LOG_MS) {
    logged = true;
    lastLogMs = now;
    Serial.printf("[battery] %.3f V%s\n", (float)smoothV, present() ? "" : " (no battery detected)");
  }
}

float volts() { return smoothV; }

bool present() { return smoothV >= VBAT_PRESENT_MIN_V; }

}  // namespace battery
