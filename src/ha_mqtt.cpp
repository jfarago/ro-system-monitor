#include "ha_mqtt.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "clock.h"
#include "config.h"
#include "counters.h"
#include "net.h"
#include "secrets.h"
#include "store.h"
#include "tds.h"

bool appFlowing();  // main.cpp

namespace ha {

static const char *TOPIC_STATE = "ro_monitor/state";
static const char *TOPIC_AVAIL = "ro_monitor/status";
static const char *DISCOVERY_PREFIX = "homeassistant";

static WiFiClient wifi;
static PubSubClient mqtt(wifi);

static uint32_t nextConnectMs = 0;
static uint32_t backoffMs = MQTT_RETRY_MIN_MS;
static uint32_t lastPublishMs = 0;
static int discoveryIdx = -1;  // -1 = not connected / done; otherwise next entity to announce
static bool stateDirty = false;
static bool lastFlowing = false;
static uint8_t lastStatus[2] = {0, 0};  // prefilter, membrane

// One row per entity. `field` is the key in the JSON state payload.
struct Entity {
  const char *component;  // sensor | binary_sensor
  const char *id;         // object id, also the state field
  const char *name;
  const char *unit;
  const char *deviceClass;
  const char *stateClass;
  const char *icon;
  bool diagnostic;
};

static const Entity ENTITIES[] = {
    {"sensor", "tds_product", "Product TDS", "ppm", nullptr, "measurement", "mdi:water-opacity", false},
    {"sensor", "rejection", "Rejection", "%", nullptr, "measurement", "mdi:filter-percent", false},

    {"sensor", "all_clean", "Clean water", "gal", "water", "total_increasing", nullptr, false},
    {"sensor", "all_total", "Total water", "gal", "water", "total_increasing", nullptr, false},
    {"sensor", "all_waste", "Waste water", "gal", "water", "total_increasing", nullptr, false},

    {"sensor", "today_clean", "Clean water today", "gal", "water", nullptr, nullptr, false},
    {"sensor", "today_waste", "Waste water today", "gal", "water", nullptr, nullptr, false},
    {"sensor", "today_total", "Total water today", "gal", "water", nullptr, nullptr, false},
    {"sensor", "week_clean", "Clean water 7 days", "gal", "water", nullptr, nullptr, false},
    {"sensor", "week_waste", "Waste water 7 days", "gal", "water", nullptr, nullptr, false},
    {"sensor", "week_total", "Total water 7 days", "gal", "water", nullptr, nullptr, false},
    {"sensor", "month_clean", "Clean water 30 days", "gal", "water", nullptr, nullptr, false},
    {"sensor", "month_waste", "Waste water 30 days", "gal", "water", nullptr, nullptr, false},
    {"sensor", "month_total", "Total water 30 days", "gal", "water", nullptr, nullptr, false},

    {"sensor", "pre_used_pct", "Prefilter used", "%", nullptr, "measurement", "mdi:filter", false},
    {"sensor", "pre_left_gal", "Prefilter life left", "gal", nullptr, "measurement", "mdi:filter", false},
    {"sensor", "pre_status", "Prefilter status", nullptr, nullptr, nullptr, "mdi:filter-check", false},
    {"sensor", "pre_days", "Days since prefilter install", "d", nullptr, nullptr, "mdi:calendar-clock", false},
    {"sensor", "mem_used_pct", "Membrane used", "%", nullptr, "measurement", "mdi:filter-variant", false},
    {"sensor", "mem_left_gal", "Membrane life left", "gal", nullptr, "measurement", "mdi:filter-variant", false},
    {"sensor", "mem_status", "Membrane status", nullptr, nullptr, nullptr, "mdi:filter-check", false},
    {"sensor", "mem_days", "Days since membrane install", "d", nullptr, nullptr, "mdi:calendar-clock", false},

    {"binary_sensor", "flowing", "Water flowing", nullptr, "running", nullptr, nullptr, false},
    {"sensor", "rssi", "Wi-Fi signal", "dBm", "signal_strength", "measurement", nullptr, true},
};
static const int NUM_ENTITIES = sizeof ENTITIES / sizeof ENTITIES[0];

static const char *STATUS_TEXT[] = {"Good", "Replace soon", "Replace now"};

// Same thresholds as the dashboard (ui/app.js): 75% soon, 90% now.
static uint8_t statusLevel(const FilterGroup &g) {
  float pct = g.ratingGal > 0 ? 100.0f * g.usedGal / g.ratingGal : 0;
  return pct >= 90 ? 2 : pct >= 75 ? 1 : 0;
}

static float round1(double v) { return roundf((float)v * 10.0f) / 10.0f; }
static float round2(double v) { return roundf((float)v * 100.0f) / 100.0f; }

static void putPeriod(JsonDocument &doc, const char *prefix, const Totals &t) {
  char k[24];
  snprintf(k, sizeof k, "%s_clean", prefix);
  doc[k] = round2(t.clean);
  snprintf(k, sizeof k, "%s_waste", prefix);
  doc[k] = round2(t.waste);
  snprintf(k, sizeof k, "%s_total", prefix);
  doc[k] = round2(t.clean + t.waste);
}

static void putFilter(JsonDocument &doc, const char *prefix, const FilterGroup &g, uint32_t today) {
  char k[24];
  float pct = g.ratingGal > 0 ? 100.0f * (float)g.usedGal / g.ratingGal : 0;
  snprintf(k, sizeof k, "%s_used_pct", prefix);
  doc[k] = round1(pct);
  snprintf(k, sizeof k, "%s_left_gal", prefix);
  doc[k] = round1(max(0.0, g.ratingGal - g.usedGal));
  snprintf(k, sizeof k, "%s_status", prefix);
  doc[k] = STATUS_TEXT[statusLevel(g)];
  if (today && g.installDate) {  // omitted (-> unknown in HA) until both dates are known
    snprintf(k, sizeof k, "%s_days", prefix);
    doc[k] = dateToDays(today) - dateToDays(g.installDate);
  }
}

static void publishState(uint32_t now) {
  JsonDocument doc;
  {
    store::Lock guard;
    Counters &c = store::counters;
    uint32_t today = clk::today();
    float ppm = tds::ppm(store::cal.kTds, store::cal.waterTempC);
    doc["tds_product"] = (int)roundf(ppm);
    doc["rejection"] = round1(tds::rejectionPct(ppm));
    putPeriod(doc, "all", c.all());
    putPeriod(doc, "today", c.today(today));
    putPeriod(doc, "week", c.week(today));
    putPeriod(doc, "month", c.month(today));
    putFilter(doc, "pre", c.pre, today);
    putFilter(doc, "mem", c.mem, today);
    lastStatus[0] = statusLevel(c.pre);
    lastStatus[1] = statusLevel(c.mem);
  }
  lastFlowing = appFlowing();
  doc["flowing"] = lastFlowing;
  doc["rssi"] = net::rssi();

  static char buf[MQTT_BUFFER_BYTES];
  size_t n = serializeJson(doc, buf, sizeof buf);
  if (n >= sizeof buf) {
    Serial.println("[mqtt] state payload too large, not published");
    return;
  }
  if (mqtt.publish(TOPIC_STATE, buf)) {
    lastPublishMs = now;
    stateDirty = false;
  }
}

static void publishDiscovery(const Entity &e) {
  JsonDocument doc;
  char topic[96], uid[48], tmpl[96];

  snprintf(uid, sizeof uid, "ro_monitor_%s", e.id);
  doc["name"] = e.name;
  doc["unique_id"] = uid;
  doc["has_entity_name"] = true;
  doc["state_topic"] = TOPIC_STATE;
  doc["availability_topic"] = TOPIC_AVAIL;
  if (!strcmp(e.component, "binary_sensor")) {
    snprintf(tmpl, sizeof tmpl, "{{ 'ON' if value_json.%s else 'OFF' }}", e.id);
  } else {
    snprintf(tmpl, sizeof tmpl, "{{ value_json.%s | default(none) }}", e.id);
  }
  doc["value_template"] = tmpl;
  if (e.unit) doc["unit_of_measurement"] = e.unit;
  if (e.deviceClass) doc["device_class"] = e.deviceClass;
  if (e.stateClass) doc["state_class"] = e.stateClass;
  if (e.icon) doc["icon"] = e.icon;
  if (e.diagnostic) doc["entity_category"] = "diagnostic";

  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"].to<JsonArray>().add("ro_monitor");
  dev["name"] = "RO Monitor";
  dev["manufacturer"] = "DIY";
  dev["model"] = "XIAO ESP32-C3 RO monitor";
  dev["configuration_url"] = "http://" MDNS_HOSTNAME ".local";

  static char buf[MQTT_BUFFER_BYTES];
  size_t n = serializeJson(doc, buf, sizeof buf);
  snprintf(topic, sizeof topic, "%s/%s/ro_monitor/%s/config", DISCOVERY_PREFIX, e.component, e.id);
  if (n >= sizeof buf || !mqtt.publish(topic, buf, true)) {
    Serial.printf("[mqtt] discovery publish failed for %s\n", e.id);
  }
}

static void tryConnect(uint32_t now) {
  Serial.printf("[mqtt] connecting to %s:%d\n", MQTT_HOST, MQTT_PORT);
  // LWT: broker marks the device offline if it drops without a clean disconnect.
  bool ok = mqtt.connect("ro-monitor", MQTT_USER, MQTT_PASSWORD, TOPIC_AVAIL, 1, true, "offline");
  if (ok) {
    Serial.println("[mqtt] connected");
    backoffMs = MQTT_RETRY_MIN_MS;
    mqtt.publish(TOPIC_AVAIL, "online", true);
    discoveryIdx = 0;  // announce entities one per loop() call, then publish state
    return;
  }
  Serial.printf("[mqtt] connect failed (rc=%d), retry in %lu s\n", mqtt.state(),
                (unsigned long)(backoffMs / 1000));
  nextConnectMs = now + backoffMs;
  backoffMs = min(backoffMs * 2, MQTT_RETRY_MAX_MS);
}

void begin() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(MQTT_BUFFER_BYTES);
  mqtt.setSocketTimeout(3);  // keeps a dead broker from stalling loop() for the 15 s default
  mqtt.setKeepAlive(30);
}

void loop(uint32_t now) {
  if (!net::connected()) {
    if (mqtt.connected()) mqtt.disconnect();
    discoveryIdx = -1;
    return;
  }

  if (!mqtt.connected()) {
    discoveryIdx = -1;
    if ((int32_t)(now - nextConnectMs) >= 0) tryConnect(now);
    return;
  }
  mqtt.loop();

  if (discoveryIdx >= 0) {
    if (discoveryIdx < NUM_ENTITIES) {
      publishDiscovery(ENTITIES[discoveryIdx++]);
    } else {
      discoveryIdx = -1;
      Serial.printf("[mqtt] announced %d entities\n", NUM_ENTITIES);
      publishState(now);
    }
    return;
  }

  // Significant changes publish right away (rate limited); otherwise every MQTT_PUBLISH_MS.
  if (!stateDirty) {
    bool flowing = appFlowing();
    uint8_t s0, s1;
    {
      store::Lock guard;
      s0 = statusLevel(store::counters.pre);
      s1 = statusLevel(store::counters.mem);
    }
    stateDirty = flowing != lastFlowing || s0 != lastStatus[0] || s1 != lastStatus[1];
  }
  uint32_t since = now - lastPublishMs;
  if (since >= MQTT_PUBLISH_MS || (stateDirty && since >= MQTT_MIN_GAP_MS)) publishState(now);
}

bool connected() { return mqtt.connected(); }

}  // namespace ha
