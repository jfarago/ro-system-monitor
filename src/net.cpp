#include "net.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "clock.h"
#include "config.h"
#include "secrets.h"

#ifndef FALLBACK_AP_PASSWORD
#define FALLBACK_AP_PASSWORD ""
#endif

namespace net {

static bool wasConnected = false;
static bool everConnected = false;
static bool ap = false;
static bool mdnsUp = false;
static uint32_t bootMs = 0;
static uint32_t nextAttemptMs = 0;
static uint32_t backoffMs = WIFI_RETRY_MIN_MS;

static void startMdns() {
  if (mdnsUp) return;
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
    Serial.printf("[net] mDNS %s.local\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[net] mDNS start failed");
  }
}

static void attempt(uint32_t now) {
  Serial.printf("[net] connecting to \"%s\" (next retry in %lu s)\n", WIFI_SSID,
                (unsigned long)(backoffMs / 1000));
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  nextAttemptMs = now + backoffMs;
  backoffMs = min(backoffMs * 2, WIFI_RETRY_MAX_MS);
}

static void startAp() {
  WiFi.mode(WIFI_AP_STA);  // keep trying STA in the background
  bool ok = WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASSWORD);
  ap = ok;
  Serial.printf("[net] fallback AP \"%s\" %s, IP %s\n", FALLBACK_AP_SSID, ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  startMdns();
}

static void stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
  Serial.println("[net] fallback AP stopped");
}

void begin() {
  bootMs = millis();
  WiFi.persistent(false);  // credentials are compile-time; don't wear flash
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.setAutoReconnect(false);  // reconnects are driven by loop() with backoff
  attempt(bootMs);
}

void loop(uint32_t now) {
  bool up = WiFi.status() == WL_CONNECTED;

  if (up && !wasConnected) {
    Serial.printf("[net] connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    everConnected = true;
    backoffMs = WIFI_RETRY_MIN_MS;
    clk::startNtp();
    startMdns();
    if (ap) stopAp();
  } else if (!up && wasConnected) {
    Serial.println("[net] Wi-Fi lost");
    nextAttemptMs = now + 1000;  // first retry quickly, then back off
  }
  wasConnected = up;
  if (up) return;

  if ((int32_t)(now - nextAttemptMs) >= 0) attempt(now);

  // Fallback AP only if we never got on the home network since boot (PLAN §4.6).
  if (!everConnected && !ap && now - bootMs >= WIFI_FALLBACK_AP_AFTER_MS) startAp();
}

bool connected() { return wasConnected; }
bool apActive() { return ap; }
int rssi() { return wasConnected ? WiFi.RSSI() : 0; }

}  // namespace net
