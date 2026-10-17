# RO System Monitor

Firmware + web dashboard for a 4-stage under-sink RO system on a Seeed XIAO ESP32-C3 (PlatformIO, Arduino framework). Full plan, decisions and phase task list: `docs/PLAN.md`. Hardware: `docs/hardware.md`.

## Pin map
| Pin | Function |
|---|---|
| GPIO2 | Flow sensor, water IN (total feed). Strapping pin: must be high at boot |
| GPIO3 | TDS probe (product water), ADC1_CH3. Feed TDS is hardcoded (`FEED_TDS_PPM`, 180) |
| GPIO4 | Flow sensor, water OUT (clean RO) |

## Commands
- Build: `pio run 2>&1 | tail -20`
- Flash: `pio run -t upload 2>&1 | tail -20`
- Upload UI (LittleFS): `pio run -t uploadfs 2>&1 | tail -20`
- Serial monitor: `pio device monitor 2>&1 | grep -v '^$' | tail -50` (or run with a timeout)
- Always pipe build/monitor output through `tail`/`grep`.

## Layout
`src/` firmware, `include/config.h` pins/defaults, `include/secrets.h` (gitignored, copy from `secrets.example.h`), `ui/` production dashboard (Phase 3), `data/` generated gzipped UI (gitignored), `web/` design mockup (reference only), `docs/`.

## Rules
- **Waste = in − out** (GPIO2 minus GPIO4), clamped at >= 0. Total = in, clean = out.
- **Membrane counter counts total water** (same as prefilters). Switch: `MEMBRANE_COUNTS_TOTAL` in `config.h`.
- Do not read `web/vendor/` or `web/support.js` (large, minified; also deny-listed in `.claude/settings.json`). `web/Main.dc.html` is the design reference.
- Never commit `include/secrets.h`.
- Phase 1 is blocked until the flow sensor signal voltage is measured (PLAN §2.1).
