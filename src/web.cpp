#include "web.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "clock.h"
#include "config.h"
#include "net.h"
#include "store.h"
#include "tds.h"

bool appFlowing();  // main.cpp

namespace web {

static AsyncWebServer server(80);

static void dateStr(uint32_t d, char *buf, size_t len) { clk::formatDate(d, buf, len); }

static void putFilter(JsonObject o, const FilterGroup &g, bool countsTotal) {
  char d[12];
  dateStr(g.installDate, d, sizeof d);
  if (d[0]) o["installDate"] = d;
  else o["installDate"] = nullptr;
  o["ratingGal"] = g.ratingGal;
  o["usedGal"] = g.usedGal;
  o["counts"] = countsTotal ? "total" : "clean";
}

static void putTotals(JsonObject o, const Totals &t) {
  o["clean"] = t.clean;
  o["waste"] = t.waste;
}

static void sendStatus(AsyncWebServerRequest *req) {
  JsonDocument doc;
  {
    store::Lock guard;
    Counters &c = store::counters;
    uint32_t today = clk::today();
    char buf[32];
    clk::isoTime(buf, sizeof buf);
    doc["time"] = buf;
    doc["timeValid"] = today != 0;

    float ppm = tds::ppm(store::cal.kTds, store::cal.waterTempC);
    JsonObject t = doc["tds"].to<JsonObject>();
    t["product"] = (int)roundf(ppm);
    t["feed"] = (int)FEED_TDS_PPM;
    t["rejection"] = roundf(tds::rejectionPct(ppm) * 10.0f) / 10.0f;

    JsonObject v = doc["volume"].to<JsonObject>();
    putTotals(v["today"].to<JsonObject>(), c.today(today));
    putTotals(v["week"].to<JsonObject>(), c.week(today));
    putTotals(v["month"].to<JsonObject>(), c.month(today));
    putTotals(v["all"].to<JsonObject>(), c.all());

    clk::formatDate(c.cnt.since, buf, sizeof buf);
    if (buf[0]) doc["countingSince"] = buf;
    else doc["countingSince"] = nullptr;

    JsonObject f = doc["filters"].to<JsonObject>();
    putFilter(f["prefilter"].to<JsonObject>(), c.pre, true);
    putFilter(f["membrane"].to<JsonObject>(), c.mem, MEMBRANE_COUNTS_TOTAL);
  }
  doc["flowing"] = appFlowing();
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = net::rssi();

  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *r = req->beginResponse(200, "application/json", out);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

static void sendJson(AsyncWebServerRequest *req, int code, const char *body) {
  AsyncWebServerResponse *r = req->beginResponse(code, "application/json", body);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

static void sendError(AsyncWebServerRequest *req, const char *msg) {
  JsonDocument d;
  d["error"] = msg;
  String out;
  serializeJson(d, out);
  sendJson(req, 400, out.c_str());
}

static void sendOk(AsyncWebServerRequest *req) { sendJson(req, 200, "{\"ok\":true}"); }

// POST bodies arrive in chunks; collect them in the request until complete.
static const size_t MAX_BODY = 512;

static void collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
                        size_t total) {
  if (total > MAX_BODY) return;
  if (index == 0) req->_tempObject = new String();
  String *s = (String *)req->_tempObject;
  if (s) s->concat((const char *)data, len);
}

// Run `fn` with the parsed JSON body (or send 400).
template <typename Fn>
static void withJson(AsyncWebServerRequest *req, Fn fn) {
  String *body = (String *)req->_tempObject;
  JsonDocument doc;
  if (!body || deserializeJson(doc, *body) || !doc.is<JsonObject>()) {
    sendError(req, "expected a JSON object body");
    return;
  }
  fn(doc.as<JsonObject>());
}

// Parse "yyyy-mm-dd"; 0 if malformed or not a real date.
static uint32_t parseDate(const char *s) {
  unsigned y, m, d;
  char tail;
  if (!s || sscanf(s, "%4u-%2u-%2u%c", &y, &m, &d, &tail) != 3) return 0;
  uint32_t v = y * 10000 + m * 100 + d;
  return isValidDate(v) ? v : 0;
}

// Validated filter update; sets *err and returns false on bad input, without changing anything.
static bool applyFilter(JsonVariantConst v, FilterId id, uint8_t &dirty, const char *&err) {
  if (v.isNull()) return true;
  if (!v.is<JsonObjectConst>()) { err = "filter settings must be an object"; return false; }
  Counters &c = store::counters;
  FilterGroup &g = id == FilterId::Prefilter ? c.pre : c.mem;
  uint8_t bit = id == FilterId::Prefilter ? DIRTY_PRE : DIRTY_MEM;

  uint32_t date = 0;
  if (!v["installDate"].isNull()) {
    date = parseDate(v["installDate"] | "");
    if (!date) { err = "installDate must be yyyy-mm-dd"; return false; }
  }
  float rating = 0;
  if (!v["ratingGal"].isNull()) {
    rating = v["ratingGal"] | 0.0f;
    if (!(rating >= 1 && rating <= 100000)) { err = "ratingGal must be 1..100000"; return false; }
  }
  double used = -1;
  if (!v["usedGal"].isNull()) {
    used = v["usedGal"] | -1.0;
    if (!(used >= 0 && used <= 1000000)) { err = "usedGal must be 0..1000000"; return false; }
  }
  if (date) dirty |= c.setFilter(id, date, rating);
  else if (rating > 0 && rating != g.ratingGal) {  // rating only (also works before the first sync)
    g.ratingGal = rating;
    dirty |= bit;
  }
  if (used >= 0) dirty |= c.setUsed(id, used);  // after the date, so date + usedGal sets the new value
  return true;
}

static void handleSettings(AsyncWebServerRequest *req) {
  withJson(req, [&](JsonObject o) {
    store::Lock guard;
    // Validate both groups before applying either by working on a copy of the state.
    Counters backup = store::counters;
    uint8_t dirty = 0;
    const char *err = nullptr;
    if (!applyFilter(o["prefilter"], FilterId::Prefilter, dirty, err) ||
        !applyFilter(o["membrane"], FilterId::Membrane, dirty, err)) {
      store::counters = backup;
      sendError(req, err);
      return;
    }
    store::markDirty(dirty);
    store::requestSave();
    sendOk(req);
  });
}

static void handleReset(AsyncWebServerRequest *req) {
  store::Lock guard;
  store::markDirty(store::counters.resetCounters(clk::today()));
  store::requestSave();
  sendOk(req);
}

static void sendCalibration(AsyncWebServerRequest *req) {
  JsonDocument doc;
  {
    store::Lock guard;
    doc["kIn"] = store::cal.kIn;
    doc["kOut"] = store::cal.kOut;
    doc["kTds"] = store::cal.kTds;
    doc["waterTempC"] = store::cal.waterTempC;
  }
  String out;
  serializeJson(doc, out);
  sendJson(req, 200, out.c_str());
}

static void handleCalibration(AsyncWebServerRequest *req) {
  withJson(req, [&](JsonObject o) {
    store::Lock guard;
    Calibration n = store::cal;
    struct Field { const char *key; float *dst; float lo, hi; };
    const Field fields[] = {
        {"kIn", &n.kIn, 100, 100000},
        {"kOut", &n.kOut, 100, 100000},
        {"kTds", &n.kTds, 0.1f, 10},
        {"waterTempC", &n.waterTempC, 0, 100},
    };
    for (const Field &f : fields) {
      if (o[f.key].isNull()) continue;
      float v = o[f.key] | NAN;
      if (!(v >= f.lo && v <= f.hi)) { sendError(req, f.key); return; }
      *f.dst = v;
    }
    store::cal = n;
    store::markDirty(DIRTY_CAL);
    store::requestSave();
    sendOk(req);
  });
}

void begin() {
  if (!LittleFS.begin(false)) Serial.println("[web] LittleFS mount failed (run uploadfs)");

  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/calibration", HTTP_GET, sendCalibration);
  server.on("/api/settings", HTTP_POST, handleSettings, nullptr, collectBody);
  server.on("/api/calibration", HTTP_POST, handleCalibration, nullptr, collectBody);
  server.on("/api/reset-counters", HTTP_POST, handleReset);

  // Gzipped assets: the server serves /x.gz for /x and adds Content-Encoding itself.
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");

  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.println("[web] server on :80");
}

}  // namespace web
