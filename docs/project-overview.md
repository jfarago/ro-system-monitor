Create a plan for this project. We need to initalize this as a platform io project. I have an esp32c3 from xiao and im building a water purification system monitor. I created a web app design with claude design its in the web folder. GPIO2 is the water in flow sensor, GPIO3 is the water out flow sensor, GPIO4 is the TDS meter. I added some more details about the hardware in '/Users/jaredfarago/Projects/ro-system-monitor/docs/hardware.md' I got the web design, the hardware is connected. we need to Initialize the project, cleanup the hardware md file, itialize as a github project. The upstream remote project is here, i just created it. [https://github.com/jfarago/ro-system-monitor.git](https://github.com/jfarago/ro-system-monitor.git).  

The water in is the total water used, the water out is clean ro water used. the difference is the waste water. you can reference the web design for what needs to be created. The Feed ppm can be hardcoded at 180. It should join my wifi, its called Hots for Tots. it should sync the date and time from wifi. the ro membranes gallon rating should be total water. i also want it to integrate with my home assistant. Im going to add some widgets to my home automation dashboard.  

here are some more details taht i had cluade spit out. It was from our conversation while building the ui.

# RO Purification Dashboard: Firmware Brief

Context for building the ESP32 firmware behind a mobile web dashboard. The ESP32 serves the web app itself, and phones connect to it in a browser.

## 1. The system

- **Under-sink reverse osmosis (RO) system with 4 stages:**
  - **Stages 1–3:** sediment and carbon prefilters. All three are replaced together, as one group. Default rated life: **1,500 gallons**.
  - **Stage 4:** RO membrane, replaced on its own schedule. Default rated life: **3,000 gallons**.
- **Both ratings must be user-configurable.**
- **Units:** gallons everywhere in the UI.

## 2. What the device measures and shows

<table>
  <tr><th>Value</th><th>Source</th><th>Notes</th></tr>
  <tr><td>Product TDS (ppm)</td><td>TDS probe on clean RO output</td><td>Main reading on the dashboard</td></tr>
  <tr><td>Feed TDS (ppm)</td><td>TDS probe on the inlet</td><td>Currently ~180 ppm on my system</td></tr>
  <tr><td>Rejection %</td><td>Calculated</td><td>(1 − product / feed) × 100</td></tr>
  <tr><td>Clean RO water (gal)</td><td>Flow sensor on the product line</td><td>Running total</td></tr>
  <tr><td>Waste water (gal)</td><td>Flow sensor on the drain/brine line</td><td>Running total</td></tr>
  <tr><td>Total water (gal)</td><td>Calculated</td><td>Clean + waste. It is not measured separately</td></tr>
  <tr><td>Waste-to-clean ratio</td><td>Calculated</td><td>waste / clean, shown as &quot;X.X : 1&quot;</td></tr>
  <tr><td>Clean % / Waste %</td><td>Calculated</td><td>Share of the total, used for the split bar</td></tr>
</table>

Flow *rates* (gal/min) are **not** shown in the UI. Only volumes are.

### Time periods for volumes

The volume cards switch between three periods: **Today**, **7 days** and **All**. The firmware needs to track volume per period:

- **Today:** since local midnight.
- **7 days:** a rolling 7-day window. Daily buckets would work, e.g. a ring buffer of 7 days of clean and waste totals.
- **All:** since the last manual counter reset.

## 3. TDS quality scale (UI)

The scale runs linearly from 0 to 200+ ppm, with a marker at the current product TDS. The bands are:

<table>
  <tr><th>Band</th><th>Range</th></tr>
  <tr><td>Excellent (green)</td><td>0–50 ppm</td></tr>
  <tr><td>Good (yellow-green)</td><td>50–100 ppm</td></tr>
  <tr><td>Fair (amber)</td><td>100–170 ppm</td></tr>
  <tr><td>Poor (red)</td><td>170–200+ ppm</td></tr>
</table>

The marker is clamped at 200. The status label next to the reading comes from the same bands.

## 4. Filter life tracking

There are two counters, each with an **install date** and a **rated capacity**:

<table>
  <tr><th>Group</th><th>Counts</th><th>Default rating</th></tr>
  <tr><td>Sediment + carbon (stages 1–3)</td><td>Total water (clean + waste), since all feed water passes through them</td><td>1,500 gal</td></tr>
  <tr><td>RO membrane (stage 4)</td><td>Clean water only</td><td>3,000 gal</td></tr>
</table>

The membrane counting only clean water is an assumption. It depends on how the membrane's rating is specified, so make it easy to switch to total water.

For each group, the UI shows:

- Gallons used / rated gallons, percent used, and gallons left
- Install date, plus days since install (needs real time; see §7)
- Status: **Good** below 75% used, **Replace soon** from 75%, **Replace now** from 90%

**Behavior rule:** saving a *new* install date for a group resets that group's used-gallons counter to 0, because a new date means a fresh filter. Changing only the rated capacity does **not** reset the counter.

## 5. Settings menu (the only place to change settings)

It opens from the settings button in the top-right of the app. It is a modal with:

1. **Stages 1–3:** install date and rated capacity (gal)
2. **Stage 4, RO membrane:** install date and rated capacity (gal)
3. **Cancel / Save:** Save applies the dates and ratings.
4. **Water counters:** a "Reset water counters" button.

   - It clears clean, waste and total for **every period** (Today, 7 days and All).
   - It does **not** affect the filter-life counters.
   - It requires a confirmation step first: "Reset all water counters? This can't be undone." with "Keep counters" or "Yes, reset".
   - It takes effect immediately on confirm, separate from Save.

The dashboard footer shows "Counting since [date]", meaning the date of the last water-counter reset. The firmware should store that date.

## 6. Data the firmware must persist (survive power loss)

- Clean and waste totals: all-time since reset, plus the daily buckets for Today and 7 days
- Date of the last water-counter reset
- Prefilter group: install date, rated gallons, used gallons
- Membrane: install date, rated gallons, used gallons
- Any sensor calibration values (flow K-factors, TDS calibration)

Storage to consider: NVS (Preferences) or LittleFS. Flash has limited write cycles, so don't write on every flow pulse. Batch the writes, e.g. every N seconds or every 0.1 gal, plus at the end of a flow event.

## 7. Implementation considerations

- **Time source:** "Today", "7 days" and "days since install" all need wall-clock time. Options are NTP (if the device is on home Wi-Fi), a battery-backed RTC, or the phone sending its time when the page loads. Decide how midnight rollover works.
- **Network mode:** the device can run as its own access point (AP) or join home Wi-Fi (STA); decide which. In AP mode the phone has no internet, so **no CDN or Google Fonts**. The mockup uses IBM Plex Sans and IBM Plex Mono, so either bundle the fonts in flash or fall back to system fonts. All HTML, CSS and JS should be served from the ESP32, ideally gzipped.
- **TDS probes:** readings drift with water temperature. Consider a temperature sensor for compensation, or at least read only while water is flowing and average several samples. Two probes in the same water can interfere with each other, so check the probe or isolation design.
- **Flow sensors:** if they're hall-effect pulse sensors, count pulses with interrupts and convert using a calibrated pulses-per-gallon (K-factor). Make the K-factor configurable for calibration.
- **Live updates:** the UI shows "Connected · updated Ns ago". Use polling or WebSocket/SSE to push new values.

## 8. Suggested API shape (a starting point only, not decided)

- `GET /api/status`: TDS product and feed, rejection, volumes for each period (clean and waste), counting-since date, filter groups (install date, rating, used), server time
- `POST /api/settings`: `{ prefilter: { installDate, ratingGal }, membrane: { installDate, ratingGal } }`. A changed install date resets that group's used counter.
- `POST /api/reset-counters`: clears the water volumes for all periods and sets counting-since to now

## 9. Dashboard layout (top to bottom, phone width ~390px, dark theme)

1. Header: "RO Monitor", connection status line, settings button
2. Product TDS card: big ppm number, quality pill, Feed TDS and Rejection %, quality scale bar
3. "Water volume" heading with a Today / 7 days / All toggle
4. Clean RO and Waste cards, side by side (gallons)
5. Total card: total gallons ("Clean + waste"), clean/waste split bar with percentages, waste-to-clean ratio
6. Filter life card: two rows (prefilters and membrane) with progress bars and status
7. Footer: "Counting since [date]"

Colors: background `#0A1317`, cards `#111F27`, borders `#1F3441`, text `#E6F1F5`, muted text `#9AB2BE`. Clean water is blue `#7FDBFF`, waste is orange `#E08A34`. Filter status colors are green `#3FCF8E`, amber `#E8B04B` and red `#E0684B`.
