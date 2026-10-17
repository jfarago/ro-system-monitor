#pragma once
#include <stdint.h>

// Pins (XIAO ESP32-C3: GPIO2 = D0, GPIO3 = D1, GPIO4 = D2)
constexpr int PIN_FLOW_IN = 2;   // water IN (total feed); strapping pin, must be high at boot
constexpr int PIN_TDS = 3;       // product TDS probe, ADC1_CH3
constexpr int PIN_FLOW_OUT = 4;  // water OUT (clean RO)

// Flow calibration: F(Hz) = 38 * Q(L/min) -> 2280 pulses/L
constexpr float K_FLOW_IN_PER_GAL = 8631.0f;
constexpr float K_FLOW_OUT_PER_GAL = 8389.0f;  // measured over a 1 gal fill
constexpr uint32_t FLOW_DEBOUNCE_US = 2000;

// TDS
constexpr float FEED_TDS_PPM = 180.0f;
constexpr float K_TDS = 1.0f;
constexpr float WATER_TEMP_C = 25.0f;

// Filter life defaults
constexpr float PREFILTER_RATING_GAL = 1500.0f;
constexpr float MEMBRANE_RATING_GAL = 3000.0f;
#define MEMBRANE_COUNTS_TOTAL 1  // 1 = total (in), 0 = clean (out) only

// Time (US Eastern, North Carolina)
#define TZ_INFO "EST5EDT,M3.2.0,M11.1.0"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"

// Network
#define MDNS_HOSTNAME "ro-monitor"
#define FALLBACK_AP_SSID "RO-Monitor-Setup"
constexpr uint32_t WIFI_FALLBACK_AP_AFTER_MS = 120000;

// Fallback AP password: define FALLBACK_AP_PASSWORD (8+ chars) in secrets.h for WPA2; open otherwise.
constexpr uint32_t WIFI_RETRY_MIN_MS = 10000;  // reconnect backoff 10 s, 20 s, 40 s, capped at 60 s
constexpr uint32_t WIFI_RETRY_MAX_MS = 60000;

// Intervals
constexpr uint32_t FLOW_SAMPLE_MS = 1000;
constexpr uint32_t FLOW_ACTIVE_MS = 3000;      // "flowing" = pulses within this window
constexpr uint32_t FLOW_EVENT_END_MS = 10000;  // no pulses this long ends a flow event (triggers a save)
constexpr uint32_t TDS_SAMPLE_MS = 40;
constexpr int TDS_SAMPLES = 30;
constexpr uint32_t SAVE_INTERVAL_MS = 60000;   // max time unsaved changes are held in RAM
constexpr uint32_t MQTT_PUBLISH_MS = 30000;
