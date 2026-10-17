# Hardware

## Parts

| Part | Qty | Link |
|---|---|---|
| Seeed XIAO ESP32-C3 | 1 | |
| Gredia water flow sensor | 2 | [Amazon](https://www.amazon.com/dp/B07RD4JXLY) |
| Keyestudio analog TDS meter | 1 | [Amazon](https://www.amazon.com/dp/B08DGLY3J2) |

## Specs

**Gredia flow sensor**
- F (Hz) = 38 × Q (L/min), ±3%
- Working range: 0.3–6 L/min
- Supply: 5–24 V DC, max 15 mA at 5 V
- Pressure ≤ 0.8 MPa; liquid temperature 0–100 °C
- Size 54 × 35 × 28 mm; 15 cm lead
- Wires: red = VCC, black = GND, yellow = signal

**Keyestudio TDS meter**
- Supply: 3.3–5.5 V DC, 3–6 mA
- Output: 0–2.3 V analog
- Range 0–1000 ppm, ±10% F.S. at 25 °C
- Module interface XH2.54-3P; electrode interface XH2.54-2P

## Wiring

| Signal | Sensor wire | ESP32-C3 pin |
|---|---|---|
| Flow IN (total feed) | yellow | GPIO2 (D0) |
| TDS (product water) | signal | GPIO3 (D1) |
| Flow OUT (clean RO) | yellow | GPIO4 (D2) |

## Power

- Flow sensors: 5 V from the XIAO 5V pin (USB), common GND.
- TDS module: **3.3 V**, so its output stays within the ADC range.

## K-factor

F = 38 × Q, with Q in L/min, so pulses per litre = 38 × 60 = **2,280**. At 3.785 L/gal that is **≈ 8,631 pulses/gal**. This is the default for both sensors, and can be calibrated per sensor.

## Cautions

1. **Signal voltage.** The sensors run at 5 V. If the signal wire is push-pull it outputs 5 V pulses, and the ESP32-C3 GPIOs are not 5 V tolerant. With no water flowing, measure the yellow wire: ~5 V means add a divider (e.g. 10 kΩ / 20 kΩ) or level shifter; floating or 0 V (open collector) means use the internal pull-up to 3.3 V.
2. **GPIO2 is a strapping pin** and must be high at boot. If the board fails to boot or flash with the sensor attached, add an external pull-up or move the sensor to D6/D7 (avoid GPIO8 and GPIO9).
3. **Flow range.** The OUT sensor is after the storage tank and sees faucet flow (~1–2 L/min), within its range.
4. **Tank timing.** Water goes IN while the tank fills and OUT when the faucet runs, so waste (in − out) is only accurate over longer windows.
5. **TDS ADC.** Use `analogReadMilliVolts()` at 11 dB attenuation. GPIO3 (ADC1) works with Wi-Fi on.
6. **No temperature sensor.** TDS assumes 25 °C unless a fixed temperature is configured.
