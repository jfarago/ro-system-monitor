#include "ota.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "config.h"
#include "net.h"
#include "secrets.h"

void feedLoopWDT();  // Arduino core

namespace ota {

static bool started = false;
static volatile bool busy = false;
static bool rebootPending = false;
static uint32_t rebootAtMs = 0;

static const char PAGE[] =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>RO Monitor update</title><body style='font:16px system-ui;margin:2em'>"
    "<h2>Firmware update</h2>"
    "<form method=POST enctype=multipart/form-data action='/update'>"
    "<p>Firmware (firmware.bin)<br><input type=file name=fw accept='.bin'>"
    "<p><button>Upload firmware</button></form>"
    "<form method=POST enctype=multipart/form-data action='/update?type=fs'>"
    "<p>Filesystem (littlefs.bin)<br><input type=file name=fs accept='.bin'>"
    "<p><button>Upload filesystem</button></form>";

static bool authed(AsyncWebServerRequest *req) { return req->authenticate("admin", OTA_PASSWORD); }

static void handleUpload(AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data,
                         size_t len, bool final) {
  if (!authed(req)) return;
  if (index == 0) {
    int cmd = req->hasParam("type") && req->getParam("type")->value() == "fs" ? U_SPIFFS : U_FLASH;
    Serial.printf("[ota] http upload %s (%s)\n", filename.c_str(), cmd == U_FLASH ? "firmware" : "fs");
    busy = true;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      Update.printError(Serial);
      busy = false;
      return;
    }
  }
  if (!Update.isRunning()) return;
  if (Update.write(data, len) != len) {
    Update.printError(Serial);
    Update.abort();
    busy = false;
    return;
  }
  if (final) {
    if (Update.end(true)) Serial.printf("[ota] http upload done, %u bytes\n", (unsigned)(index + len));
    else Update.printError(Serial);
  }
}

static void handleDone(AsyncWebServerRequest *req) {
  if (!authed(req)) return req->requestAuthentication();
  bool ok = !Update.hasError() && busy;
  busy = false;
  AsyncWebServerResponse *r = req->beginResponse(ok ? 200 : 500, "text/plain",
                                                 ok ? "Update OK, rebooting" : "Update failed");
  r->addHeader("Connection", "close");
  req->send(r);
  if (ok) {
    rebootPending = true;
    rebootAtMs = millis() + 1000;
  }
}

void begin(AsyncWebServer &server) {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!authed(req)) return req->requestAuthentication();
    req->send(200, "text/html", PAGE);
  });
  server.on("/update", HTTP_POST, handleDone, handleUpload);
}

static void startArduinoOta() {
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);  // net.cpp owns mDNS
  ArduinoOTA.onStart([]() { busy = true; Serial.println("[ota] start"); });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) { feedLoopWDT(); });  // handle() blocks loop()
  ArduinoOTA.onEnd([]() { Serial.println("[ota] done"); });
  ArduinoOTA.onError([](ota_error_t e) { busy = false; Serial.printf("[ota] error %u\n", (unsigned)e); });
  ArduinoOTA.begin();
  started = true;
}

void loop() {
  if (rebootPending && (int32_t)(millis() - rebootAtMs) >= 0) ESP.restart();
  if (!started) {
    if (net::connected()) startArduinoOta();
    return;
  }
  ArduinoOTA.handle();
}

bool updating() { return busy; }

}  // namespace ota
