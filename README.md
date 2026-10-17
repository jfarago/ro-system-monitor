# RO System Monitor

Monitors a 4-stage under-sink reverse osmosis system with a Seeed XIAO ESP32-C3. It counts water in and out with two flow sensors, reads product TDS, tracks filter life, serves a mobile dashboard from flash at `ro-monitor.local`, and publishes to Home Assistant over MQTT.

Status: Phase 2 (core logic: Wi-Fi, NTP, counters, filter life, NVS). No web UI yet; use the serial console. See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## Wiring

| Part | Wire | XIAO ESP32-C3 |
|---|---|---|
| Flow sensor IN | red / black / yellow | 5 V / GND / GPIO2 (D0) |
| TDS module | VCC / GND / signal | 3V3 / GND / GPIO3 (D1) |
| Flow sensor OUT | red / black / yellow | 5 V / GND / GPIO4 (D2) |

The flow sensors may output 5 V pulses, which the ESP32-C3 cannot tolerate. Measure the signal wire first and add a divider if needed (see [docs/hardware.md](docs/hardware.md)).

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```sh
cp include/secrets.example.h include/secrets.h   # then edit Wi-Fi / MQTT credentials
pio run                  # build
pio run -t upload        # flash firmware
pio run -t uploadfs      # upload web UI (from Phase 3)
pio device monitor       # serial at 115200
pio test -e native       # host unit tests for the counter logic
```

Serial console commands (one per line): `s` status, `r`/`g` 1-gal calibration fill, `k in|out <pulses/gal>`, `t <ppm>` TDS calibration, `filter pre|mem <yyyy-mm-dd> [ratingGal]`, `reset` water counters, `inject <inGal> <outGal>` (test), `save`, `reboot`, `wipe yes`. See the header of `src/main.cpp`.

## Layout

`src/` firmware · `include/` config and secrets · `ui/` dashboard source · `web/` design mockup · `docs/` plan and hardware notes
