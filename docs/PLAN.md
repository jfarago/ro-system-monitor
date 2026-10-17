# RO System Monitor: Project Plan

Firmware and web dashboard for a 4-stage under-sink RO system, running on a Seeed XIAO ESP32-C3. The device joins home Wi-Fi, serves the mobile dashboard from flash, keeps the time with NTP, and publishes its readings to Home Assistant.

Source material: `docs/project-overview.md`, `docs/hardware.md`, and the design mockup in `web/`.

---

## 1. Decisions that override the original firmware brief

The firmware brief in `project-overview.md` was written before the hardware was finalized. Where it disagrees with the real build, the build wins:

| Topic | Brief said | Actual build / decision |
|---|---|---|
| Flow sensors | One on the product line, one on the drain line; total = clean + waste | **GPIO2 = water IN (total feed)**, **GPIO3 = water OUT (clean RO)**. **Waste = in − out**, clamped at ≥ 0 |
| TDS probes | Two probes (feed + product) | **One probe on GPIO4** (product/clean water). **Feed TDS is hardcoded at 180 ppm** (configurable constant) |
| Membrane counter | Counts clean water only | **Counts total water**, the same as the prefilters. Keep a compile-time/config switch so it can be flipped back to clean-only |
| Network mode | AP or STA, undecided | **STA: joins "Hots for Tots"**. AP mode is only a fallback for recovery when Wi-Fi fails |
| Time source | NTP, RTC, or phone time | **NTP over Wi-Fi**, in US Eastern time (`EST5EDT,M3.2.0,M11.1.0`, North Carolina) for local midnight and DST |
| Fonts | Bundle or fall back | Phones are on home Wi-Fi with internet, so **link IBM Plex from Google Fonts with a `system-ui` / `monospace` fallback**. In AP fallback mode the page still renders with system fonts |
| Live updates | Polling, WebSocket, or SSE | **Poll `GET /api/status` every 2 s**. It's the simplest option and drives the "updated Ns ago" text directly |
| Home Assistant | Not in brief | **MQTT discovery** (see §7) |

The filter card subtitle in the mockup ("Stage 4 · counts clean water") changes to "counts total water" to match.

---

## 2. Hardware review: check these before writing much firmware

These come from the parts in `hardware.md` and could change the wiring:

1. **Flow sensor signal voltage (high risk).** The Gredia sensors need **5–24 V** to run. If the yellow signal wire is push-pull to VCC, it outputs 5 V pulses, and the ESP32-C3's GPIOs are **not 5 V tolerant**. With the sensor powered at 5 V and no water flowing, measure the signal wire with a multimeter:
   - If it reads ~5 V, add a voltage divider (e.g. 10 kΩ / 20 kΩ) or a level shifter on each signal line.
   - If it floats or reads 0 V (open collector), use the internal pull-up to 3.3 V. No divider is needed.
2. **GPIO2 is a strapping pin on the ESP32-C3.** It has to be high at boot, which works with a pulled-up, idle-high flow signal. If the board ever fails to boot or flash with the sensor attached, this pin is the cause. Fixes are an external pull-up, or moving the sensor to a free GPIO such as D6/D7 (avoid GPIO8 and GPIO9, which are also strapping pins).
3. **Flow sensor minimum flow.** The sensors are rated for **0.3–6 L/min**. RO membrane production (~0.1–0.3 L/min) would sit at or below that floor, but **the OUT sensor is after the storage tank**, so it sees faucet flow (~1–2 L/min), which is well within range. ✅ Resolved.
4. **Storage tank timing effect.** Water goes IN while the tank fills, and comes OUT when the faucet runs, often hours apart. Because waste = in − out, **waste is only accurate over longer windows**. For example, "Today" right after a tank refill will show high waste. This is expected behavior. The UI and HA show what is measured, and the plan documents the caveat rather than trying to correct for it.
5. **TDS module output (0–2.3 V)** fits the ESP32-C3 ADC at 11 dB attenuation (~0–2.5 V). Use `analogReadMilliVolts()`, which applies the factory calibration. GPIO4 is ADC1_CH4, which works while Wi-Fi is on. Power the module from **3.3 V**.
6. **No temperature sensor.** TDS compensation assumes 25 °C, with a configurable fixed water temperature. A DS18B20 can be added later on a spare pin.

**K-factor from the spec:** F (Hz) = 38 × Q (L/min), so 38 × 60 = **2,280 pulses/L**, or **≈ 8,631 pulses/gal**. This is the default for both sensors. It is stored per sensor in NVS so each one can be calibrated separately (±3% spec plus install effects).

---

## 3. Target architecture

```
                 ┌──────────────── XIAO ESP32-C3 ────────────────┐
 Flow IN  (GPIO2)┤ ISR pulse counter ┐                            │
 Flow OUT (GPIO3)┤ ISR pulse counter ┼─► Accumulator (gal) ─► Store (NVS)
 TDS      (GPIO4)┤ ADC sampler ──────┘     │  daily buckets        │
                 │                         ▼                       │
                 │  NTP clock ──►   State model  ──► REST API ─────┼─► Phone browser
                 │                         │       (LittleFS UI)   │   (ro-monitor.local)
                 │                         └────► MQTT client ─────┼─► Home Assistant
                 └────────────────────────────────────────────────┘
```

**Stack**

- PlatformIO, `board = seeed_xiao_esp32c3`, `framework = arduino`
- Libraries:
  - `ESP32Async/ESPAsyncWebServer` + `AsyncTCP` for the web server
  - `bblanchon/ArduinoJson` v7
  - `knolleary/PubSubClient` or `marvinroger/AsyncMqttClient` for MQTT
  - `ElegantOTA` or ArduinoOTA for over-the-air updates (the board lives under a sink)
- Storage: **NVS (Preferences)** for settings, counters and buckets. **LittleFS** holds only the gzipped web assets.
- mDNS hostname: `ro-monitor.local`

**Proposed repo layout**

```
ro-system-monitor/
├── platformio.ini
├── partitions.csv            # app + OTA + LittleFS
├── include/
│   ├── config.h              # pins, defaults, feed ppm, TZ, intervals
│   └── secrets.example.h     # template; real secrets.h is gitignored
├── src/
│   ├── main.cpp
│   ├── flow.{h,cpp}          # ISRs, pulse → gallons
│   ├── tds.{h,cpp}           # ADC sampling, median filter, ppm conversion
│   ├── clock.{h,cpp}         # NTP, TZ, midnight rollover
│   ├── store.{h,cpp}         # NVS persistence, batched writes
│   ├── counters.{h,cpp}      # periods, 7-day ring buffer, filter life
│   ├── web.{h,cpp}           # REST API + static files
│   └── ha_mqtt.{h,cpp}       # HA MQTT discovery + state publishing
├── web/                      # design mockup (reference only, untouched)
├── ui/                       # production dashboard source (vanilla HTML/CSS/JS)
│   ├── index.html
│   ├── app.css
│   └── app.js
├── data/                     # build output: gzipped ui/ files for LittleFS
├── scripts/build_ui.py       # PlatformIO pre-script: ui/ → data/*.gz
└── docs/
```

---

## 4. Firmware design details

### 4.1 Flow counting
- Use `attachInterrupt(FALLING)` on each pin with an `IRAM_ATTR` ISR that increments a `volatile uint32_t`. A pulse-width guard (ignore edges < ~2 ms apart) filters out noise.
- The main loop takes a snapshot of the counters every 1 s (briefly disabling interrupts), converts the delta pulses to gallons using the K-factors, and adds them to the accumulators.
- Waste delta = max(0, in_delta − out_delta), computed on accumulated totals rather than per tick, so sensor timing jitter doesn't clip the result. All-time waste is stored as `all_in − all_out`.
- "Flowing" = any pulses in the last 3 s. A flow event ends after 10 s with no pulses, which triggers a save.

### 4.2 TDS
- Sample every 40 ms into a 30-sample buffer, then take the median and convert:
  - `V` in volts → `comp = V / (1 + 0.02 × (T − 25))`
  - `ppm = (133.42·c³ − 255.86·c² + 857.39·c) × 0.5 × kTDS`
- `kTDS` is a calibration factor, default 1.0, set against a reference solution or a handheld meter.
- Rejection % = (1 − product / 180) × 100, clamped to 0–100.
- Optional: only update the displayed value while water is flowing, or within N minutes of flow, since a stagnant probe reads high. Add this as a config flag if the readings creep while the system sits idle.

### 4.3 Time and periods
- Call `configTzTime(TZ, "pool.ntp.org", "time.google.com")`. The TZ string in `config.h` is `EST5EDT,M3.2.0,M11.1.0` (US Eastern, North Carolina).
- **Daily buckets:** a ring of 8 `{ yyyymmdd, in_gal, out_gal }` entries (today + 7 previous days).
  - **Today** = the current bucket.
  - **7 days** = the sum of today + the previous 6.
  - **All** = all-time accumulators since the last reset.
- **Midnight rollover:** the loop compares the current local date with the newest bucket's date. When the date changes, it opens a new bucket and drops the oldest. Missing days, for example while the board was powered off, are filled with zero buckets.
- **Before NTP sync** (on boot with no Wi-Fi), volume still goes into the all-time totals and filter counters, and is held in a pending bucket that goes into "Today" once the time is valid. The UI shows "Time not synced" in place of dates.

### 4.4 Filter life
- Two groups `{ installDate (yyyy-mm-dd), ratingGal, usedGal }`:
  - Prefilters: default 1,500 gal, counts **total (in)**.
  - Membrane: default 3,000 gal, counts **total (in)**. Switch with `MEMBRANE_COUNTS = TOTAL | CLEAN`.
- Saving a **changed** install date resets that group's `usedGal` to 0. Changing only the rating does not reset it.
- The firmware returns the raw numbers. The UI computes the status thresholds (75% / 90%), and they are also published to HA.

### 4.5 Persistence (NVS)
- Keys: `cnt` (all-time in/out + resetDate), `days` (bucket ring as a blob), `pre` and `mem` (filter groups), `cal` (K-in, K-out, kTDS, waterTemp).
- **Write policy:** save when something has changed and either (a) 60 s have passed, (b) a flow event has ended, (c) the day rolled over, or (d) a setting was saved or counters were reset. This keeps flash wear very low (NVS also wear-levels).
- Maximum loss on a sudden power cut is about 60 s of flow.

### 4.6 Wi-Fi
- STA connects to **Hots for Tots**. The SSID and password go in `include/secrets.h`, which is **gitignored**. The repo only includes `secrets.example.h`.
- Reconnect with backoff. If the board can't connect for 2 minutes at boot, it starts the fallback AP `RO-Monitor-Setup`, which serves the same UI so you can recover. Credentials stay compile-time for v1. A captive-portal Wi-Fi setup screen can be added later.

---

## 5. HTTP API

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Full dashboard state (below) |
| POST | `/api/settings` | `{ prefilter:{installDate,ratingGal}, membrane:{installDate,ratingGal} }`; changed date resets that group's used counter |
| POST | `/api/reset-counters` | Zero every water period (Today, 7 days, All), set `countingSince = now`; filter counters untouched |
| GET/POST | `/api/calibration` | `{ kIn, kOut, kTds, waterTempC }`. Not in the UI; for setup/curl |
| GET | `/` and static | Gzipped UI from LittleFS (`Content-Encoding: gzip`) |
| — | `/update` | OTA firmware/filesystem upload (password protected) |

`/api/status` example:
```json
{
  "time": "2026-10-17T14:32:05-04:00", "timeValid": true,
  "tds": { "product": 12, "feed": 180, "rejection": 93.3 },
  "volume": {
    "today": { "clean": 4.9,   "waste": 10.9 },
    "week":  { "clean": 33.5,  "waste": 76.5 },
    "all":   { "clean": 1271.3,"waste": 2915.7 }
  },
  "countingSince": "2026-01-15",
  "filters": {
    "prefilter": { "installDate": "2026-06-02", "ratingGal": 1500, "usedGal": 912, "counts": "total" },
    "membrane":  { "installDate": "2026-01-15", "ratingGal": 3000, "usedGal": 1046, "counts": "total" }
  },
  "flowing": false, "uptime": 86400, "rssi": -61
}
```

---

## 6. Web dashboard

- Rebuild the mockup in `web/Main.dc.html` as **plain HTML/CSS/JS** in `ui/`. The mockup's React runtime (~190 KB) isn't needed for one screen.
- Match the mockup's exact values: colors, radii, type sizes, spacing, the TDS scale grid (`5fr 5fr 7fr 3fr`), the filter pill colors, and the settings modal with the reset confirmation.
- Replace the hardcoded state in the mockup's `Component` class with data from `/api/status`. The formatting logic (`fmt`, `pretty`, `daysSince`, filter status levels) carries over almost unchanged.
- Header status: green dot + "Connected · updated Ns ago". After 10 s without a successful poll, switch to an amber or red "Disconnected" state.
- The layout is responsive rather than fixed at 390 px, since the mockup is a phone artboard.
- `scripts/build_ui.py` gzips `ui/*` into `data/`. Upload with `pio run -t uploadfs`. The target size is < 30 KB gzipped.
- For development without hardware, `ui/` can be served locally against a mock `/api/status` (small Python stub or a JSON file).

---

## 7. Home Assistant integration

**Approach: MQTT with HA discovery.** The device announces itself, and the entities appear automatically under one "RO Monitor" device. Uses the Mosquitto broker add-on already installed in HA. Broker host, user and password go in `secrets.h`.

Entities (`homeassistant/<component>/ro_monitor/<object>/config`):

| Entity | Type | Unit / class | Notes |
|---|---|---|---|
| Product TDS | sensor | ppm | `state_class: measurement` |
| Rejection | sensor | % | |
| Clean water (all-time) | sensor | gal, `device_class: water`, `total_increasing` | Usable in the **Energy → Water** dashboard |
| Total water (all-time) | sensor | gal, `water`, `total_increasing` | |
| Waste water (all-time) | sensor | gal, `water`, `total_increasing` | |
| Clean / Waste / Total today | sensor | gal | |
| Clean / Waste / Total 7 days | sensor | gal | |
| Prefilter used / life left | sensor | % / gal | |
| Membrane used / life left | sensor | % / gal | |
| Prefilter / Membrane status | sensor | text | Good / Replace soon / Replace now |
| Days since prefilter / membrane install | sensor | d | |
| Water flowing | binary_sensor | `running` | |
| Wi-Fi signal | sensor | dBm, diagnostic | |

- State is published as one JSON payload on `ro_monitor/state` every 30 s and on any significant change. Each entity uses a `value_template` to pick its field.
- An availability topic with an LWT shows the device as offline in HA when it drops.
- With these entities, dashboard widgets are straightforward: a gauge for TDS, entity cards for today's water, bar gauges for filter life, and an automation to notify when a filter hits "Replace soon".

*Alternative if you'd rather not run MQTT:* HA's `rest` integration can poll `/api/status`. It needs no broker but you configure it by hand in YAML. MQTT discovery is recommended.

---

## 8. Phased task list

**Workflow:** use one fresh Claude Code session per phase. Start each one with "Read `docs/PLAN.md` and do Phase N". Set the model with `/model` first. At the end of the phase, tick its checkboxes, commit, then `/clear`. Use `/compact` if a phase runs long.

### Phase 0: Repository & project setup
> **Model: Haiku or Sonnet.** Mechanical scaffolding.

- [x] `git init`, default branch `main`
- [x] Create `CLAUDE.md` (board, pin map, build/flash/uploadfs commands, repo layout, pointer to `docs/PLAN.md`, the "waste = in − out" and "membrane counts total" rules, "don't read `web/vendor/` or `web/support.js`", "pipe build/monitor output through `tail`/`grep`")
- [x] Create `.claude/settings.json` with deny rules so these large minified files are never read:
  ```json
  {
    "permissions": {
      "deny": [
        "Read(./web/vendor/**)",
        "Read(./web/support.js)",
        "Read(./.pio/**)"
      ]
    }
  }
  ```
- [x] `.gitignore`: `.pio/`, `.vscode/` (except recommended extensions), `include/secrets.h`, `data/`, `.DS_Store`
- [x] Initialize PlatformIO project (`pio project init --board seeed_xiao_esp32c3`), `platformio.ini` with libraries, `monitor_speed = 115200`, USB CDC on boot flags, LittleFS, custom partitions
- [x] `include/config.h` (pins GPIO2/3/4, feed 180 ppm, defaults, TZ `EST5EDT,M3.2.0,M11.1.0`) and `include/secrets.example.h`
- [ ] Blink/serial "hello" build flashes to the XIAO (builds OK; flash not yet tested, no board connected)
- [x] `README.md` (overview, wiring, build/flash steps)
- [x] Clean up `docs/hardware.md`: parts table, specs, pinout/wiring table, power notes, K-factor derivation, the §2 cautions
- [x] `git remote add origin https://github.com/jfarago/ro-system-monitor.git`, first commit, push

### Phase 1: Sensor bring-up (serial output only)
> **Model: Sonnet.** Switch to Opus if readings act strangely and need deeper debugging.

- [ ] Verify the flow signal voltage (§2.1) and add dividers if needed
- [ ] Flow ISRs; print pulses and gallons for IN and OUT; sanity-check by filling a known 1-gal container from the faucet
- [ ] TDS sampling and conversion; compare against a handheld TDS pen and set `kTDS`
- [ ] Confirm GPIO2 boots and flashes with the sensor attached

### Phase 2: Core logic
> **Model: Opus.** Counter, rollover, persistence and pre-sync edge cases are easy to get subtly wrong.

- [ ] Wi-Fi STA + reconnect + fallback AP; mDNS `ro-monitor.local`
- [ ] NTP + TZ; time-valid flag
- [ ] Counters: all-time, 8-day bucket ring, midnight rollover, pre-sync pending bucket
- [ ] Filter-life groups with install-date reset rule
- [ ] NVS store with batched writes; power-cycle tests (counts survive, nothing lost beyond ~60 s)

### Phase 3: API + dashboard
> **Model: Sonnet.** The UI port from the mockup is mostly translation and is a good fit for a Sonnet subagent.

- [ ] Async web server, `/api/status`, `/api/settings`, `/api/reset-counters`, `/api/calibration`
- [ ] Build `ui/` from the mockup, wire to API, polling + connection state
- [ ] Settings modal: dates/ratings save, reset confirmation flow
- [ ] UI build script → gzipped LittleFS; `uploadfs`
- [ ] Test on phone over home Wi-Fi

### Phase 4: Home Assistant
> **Model: Sonnet.**

- [ ] MQTT client + discovery configs + availability/LWT
- [ ] Verify entities appear in HA; add water sensors to Energy dashboard
- [ ] Build HA dashboard widgets; "replace filter" notification automation

### Phase 5: Hardening
> **Model: Sonnet** for OTA and the watchdog. **Opus** for reboot-loop or recovery debugging.

- [ ] OTA updates (password protected)
- [ ] Hardware watchdog; recover from Wi-Fi/MQTT loss without reboot loops
- [ ] Optional: calibrate K-factors in place (measure 1 gal at faucet, adjust `kOut`; compare long-run in/out ratio against the system's expected ~1:3 to sanity-check `kIn`)
- [ ] Optional: DS18B20 temperature compensation for TDS
- [ ] Tag `v1.0.0`

---

## 9. Decisions log

Resolved 2026-10-17:

1. **Time zone:** US Eastern (North Carolina), `EST5EDT,M3.2.0,M11.1.0`.
2. **Flow sensor placement:** the OUT sensor is after the storage tank, so faucet flow is within the sensor's range.
3. **Home Assistant:** the Mosquitto add-on is installed, so the plan uses MQTT discovery.
4. **Reset water counters:** web UI only. It is **not** exposed to Home Assistant.

Still pending:

- **Flow signal voltage:** Jared will measure the signal wire before connecting the sensors to the ESP32 (§2.1). Phase 1 is blocked until this is checked.
