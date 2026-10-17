# Home Assistant setup

The firmware publishes over MQTT with HA discovery (PLAN §7). Requires the Mosquitto broker add-on and the MQTT integration.

## 1. Broker user

In HA: Settings → People → Users → add a user for the device (e.g. `ro-monitor`, not an admin). Put its name and password, plus the broker host, in `include/secrets.h`:

```c
#define MQTT_HOST "homeassistant.local"   // or the HA IP if mDNS resolution fails on the ESP
#define MQTT_PORT 1883
#define MQTT_USER "ro-monitor"
#define MQTT_PASSWORD "..."
```

Flash, then watch the serial log for `[mqtt] connected` and `[mqtt] announced 24 entities`.

## 2. Topics

| Topic | Content |
|---|---|
| `ro_monitor/status` | `online` / `offline` (retained; `offline` is the LWT) |
| `ro_monitor/state` | one JSON payload, every 30 s and right away when flowing or a filter status changes |
| `homeassistant/<component>/ro_monitor/<id>/config` | retained discovery configs |

Check from a terminal: `mosquitto_sub -h homeassistant.local -u ro-monitor -P <pw> -t 'ro_monitor/#' -v`

## 3. Entities

All appear under one device, **RO Monitor** (Settings → Devices → MQTT). Entity ids look like `sensor.ro_monitor_clean_water`.

- Product TDS, Rejection
- Clean / Total / Waste water: all-time (`total_increasing`, device class `water`), today, 7 days, 30 days
- Prefilter / Membrane: used %, life left (gal), status (`Good` / `Replace soon` / `Replace now`), days since install
- Water flowing (binary sensor), Wi-Fi signal (diagnostic)

Days-since sensors stay unknown until the device has synced its clock. The reset-counters action is intentionally not exposed to HA (PLAN §9).

## 4. Energy dashboard

Settings → Dashboards → Energy → Water consumption → Add water source. Pick **Total water** (`sensor.ro_monitor_total_water`) for the feed, or **Clean water** for what you drink. Units are gallons; HA converts to your system's unit.

Waste is in minus out, so over short windows it is skewed by the tank filling (PLAN §2.4). Long-run totals are accurate.

## 5. Dashboard cards

Paste into a dashboard's raw configuration editor (adjust entity ids if yours differ).

```yaml
type: vertical-stack
cards:
  - type: gauge
    entity: sensor.ro_monitor_product_tds
    name: Product TDS
    min: 0
    max: 100
    needle: true
    severity:
      green: 0
      yellow: 30
      red: 60
  - type: entities
    title: Water
    entities:
      - entity: sensor.ro_monitor_clean_water_today
      - entity: sensor.ro_monitor_waste_water_today
      - entity: sensor.ro_monitor_clean_water_7_days
      - entity: sensor.ro_monitor_clean_water_30_days
      - entity: binary_sensor.ro_monitor_water_flowing
  - type: horizontal-stack
    cards:
      - type: gauge
        entity: sensor.ro_monitor_prefilter_used
        name: Prefilter
        min: 0
        max: 100
        severity: { green: 0, yellow: 75, red: 90 }
      - type: gauge
        entity: sensor.ro_monitor_membrane_used
        name: Membrane
        min: 0
        max: 100
        severity: { green: 0, yellow: 75, red: 90 }
```

## 6. Replace-filter notification

Settings → Automations → Create → Edit in YAML. Replace `notify.notify` with your phone's `notify.mobile_app_<device>` service.

```yaml
alias: RO filter needs replacing
mode: queued
triggers:
  - trigger: state
    entity_id:
      - sensor.ro_monitor_prefilter_status
      - sensor.ro_monitor_membrane_status
    to:
      - Replace soon
      - Replace now
actions:
  - action: notify.notify
    data:
      title: RO system
      message: >-
        {{ trigger.to_state.name }}: {{ trigger.to_state.state }}
        ({{ states('sensor.ro_monitor_prefilter_used') }}% prefilter,
        {{ states('sensor.ro_monitor_membrane_used') }}% membrane used)
```

A second automation on `binary_sensor`/availability going `unavailable` for 10 minutes is a cheap way to learn the board dropped off Wi-Fi.
