# ESP8266 Stock Ticker

A real-time stock price display built with an **ESP8266** and an **8-panel MAX7219 LED matrix**. Prices are fetched from Yahoo Finance — no API key required. Wi-Fi credentials and stock tickers are configured through a built-in captive-portal web page; no re-flashing needed to change settings.

---

## Demo

```
AAPL $213.45   →  swoops out  →  AAPL +1.32%   →  swoops out  →  TSLA $287.11  →  ...
```

Each statistic smoothly rotates in from the top and out to the bottom before the next one arrives.

---

## Features

| Feature | Details |
|---------|---------|
| **Captive-portal setup** | Connect to `StockTicker-Setup` hotspot; scan for Wi-Fi networks, enter your password, and type up to 5 ticker symbols — all from a phone browser |
| **Double-reset reconfigure** | Press the reset button twice within 3 seconds to re-launch the setup portal without re-flashing |
| **Live prices** | Fetched from Yahoo Finance every 60 s during market hours (5 min pre-market, 10 min after-hours) |
| **Smooth vertical animation** | Each stat swoops in from the top, holds for ~4.5 s, then swoops out to the bottom as the next one arrives |
| **Market open/close warnings** | Scrolls "5 MINUTES UNTIL MARKET OPEN/CLOSE" at 9:25 AM and 3:55 PM ET |
| **Hourly DOW banner** | Shows Dow Jones % change vs. previous close for 30 s at the top of every hour |
| **Multi-stock cycling** | Cycles through your stocks (30 s each): price sub-frame, then % change sub-frame |
| **After-close summary** | Cycles stock % change and DOW; every 10 min blinks each stock's last price |
| **Accurate % change** | Uses `regularMarketOpen` from Yahoo's API — matches Yahoo Finance's displayed headline figure |
| **DST-aware clock** | Eastern Time (EST/EDT) handled automatically via POSIX TZ rule; no manual adjustment needed |
| **Wi-Fi fault recovery** | After 5 consecutive Wi-Fi failures the device clears credentials and re-launches the portal |

---

## Parts List

| Qty | Part | Notes |
|-----|------|-------|
| 1 | **ESP8266 NodeMCU v1.0** (ESP-12E) | Any ESP8266 board works; NodeMCU has a USB-serial chip built in |
| 1 | **MAX7219 8-panel LED matrix** (FC-16 style) | The long 32×8 LED bar with 8 chained 8×8 modules; widely available on Amazon/AliExpress as "MAX7219 dot matrix module 8 in 1" |
| 1 | **Micro-USB cable** | For programming and power |
| 1 | **5 V power supply ≥ 1 A** | USB wall adapter or bench supply; 8 panels at full brightness draw ~800 mA |
| — | **Jumper wires** (female–female) | 5 wires needed |
| — | *(Optional)* **3D-printed enclosure** | Any project box that fits a 32×8 LED panel |

> **Why 5 V for the matrix?** The MAX7219 runs on 5 V but its data lines are 3.3 V-tolerant, so the ESP8266's 3.3 V SPI signals drive it directly without a level shifter.

---

## Wiring

Connect the 5-pin header on the LED matrix to the ESP8266:

| MAX7219 pin | ESP8266 GPIO | NodeMCU label | Notes |
|-------------|-------------|---------------|-------|
| **VCC** | — | **Vin** (or external 5 V) | **Must be 5 V**, not 3.3 V |
| **GND** | GND | **GND** | Common ground |
| **DIN** | GPIO 13 | **D7** | SPI MOSI |
| **CLK** | GPIO 14 | **D5** | SPI SCK |
| **CS** | GPIO 15 | **D8** | SPI chip-select |

```
NodeMCU          MAX7219 Matrix
   Vin  ─────────  VCC
   GND  ─────────  GND
    D7  ─────────  DIN
    D5  ─────────  CLK
    D8  ─────────  CS
```

> ⚠️ **Power:** Do **not** power 8 panels from the ESP8266's 3.3 V pin — it cannot supply enough current. Use the `Vin` pin (which passes through 5 V from USB) or a separate 5 V supply. For permanent installations use a dedicated 5 V / 2 A adapter.

---

## Software Prerequisites

### 1 — Arduino IDE

Download from [arduino.cc](https://www.arduino.cc/en/software). Version 1.8+ or 2.x both work.

### 2 — ESP8266 Board Support

1. Open **File → Preferences** and paste this URL into *Additional Boards Manager URLs*:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
2. Go to **Tools → Board → Boards Manager**, search for **esp8266**, and install **"esp8266 by ESP8266 Community"**.

### 3 — Board Settings

| Setting | Value |
|---------|-------|
| Board | `NodeMCU 1.0 (ESP-12E Module)` |
| Flash Size | `4MB (FS: 2MB, OTA: ~1019KB)` ← **important** — LittleFS needs the FS partition |
| Upload Speed | `921600` (or `115200` if uploads fail) |
| CPU Frequency | `80 MHz` |

### 4 — Required Libraries

Install all three via **Tools → Manage Libraries**:

| Library | Author | Min version |
|---------|--------|-------------|
| **MD_Parola** | MajicDesigns | 3.6 |
| **MD_MAX72XX** | MajicDesigns | 3.3 |
| **ArduinoJson** | Benoit Blanchon | 6.x *(not v5 or v7)* |

> All other dependencies (`ESP8266WiFi`, `LittleFS`, `DNSServer`, `ESP8266WebServer`, `ESP8266HTTPClient`, `WiFiClientSecureBearSSL`) ship with the ESP8266 core — nothing extra to install.

---

## Quick Start

### 1 — Get the code

```bash
git clone https://github.com/dbzx6r/StockTicker.git StockTicker
```

> The folder **must** be named `StockTicker` (matching the `.ino` filename) for the Arduino IDE to open it correctly.

### 2 — Open in Arduino IDE

Open `StockTicker/StockTicker.ino`.

### 3 — (Optional) Tweak `config.h`

All user-facing settings live in `config.h`:

| Constant | Default | Description |
|----------|---------|-------------|
| `DISPLAY_INTENSITY` | `4` | Brightness 0 (min) – 15 (max) |
| `SCROLL_SPEED_MS` | `50` | ms per scroll step — lower = faster horizontal scroll |
| `VERTICAL_SPEED_MS` | `25` | ms per step for the vertical swipe animation |
| `SUBFRAME_MS` | `4500` | How long each stat (price / % change) stays on screen (ms) |
| `STOCK_CYCLE_MS` | `30000` | How long each stock slot lasts before cycling to the next (ms) |
| `MAX_HARDWARE_TYPE` | `FC16_HW` | Change to `PAROLA_HW`, `ICSTATION_HW`, or `GENERIC_HW` if the display looks garbled |

### 4 — Upload

Select the correct **Port** under *Tools → Port*, then click **Upload**.

### 5 — First-time Wi-Fi + stock setup

1. After flashing, the display scrolls **"CONNECT TO StockTicker-Setup"**.
2. On your phone or laptop, join the Wi-Fi network **`StockTicker-Setup`** (open, no password).
3. A setup page opens automatically (captive portal). If it doesn't, navigate to **`http://192.168.4.1`**.
4. Select your home Wi-Fi from the dropdown, enter the password, and type up to **5 ticker symbols** (e.g. `AAPL`, `TSLA`, `BLK`). Use the same symbols shown on [Yahoo Finance](https://finance.yahoo.com).
5. Tap **Save & Connect**. The device reboots and starts displaying prices.

---

## Changing Settings Later (Double-Reset)

You don't need to re-flash to change your Wi-Fi or stock list:

1. **Press the reset button twice within 3 seconds.**
2. The display scrolls **"SETUP – CONNECT TO StockTicker-Setup"**.
3. Join `StockTicker-Setup` and update your settings as above.

---

## Display Schedule

```
┌─ Weekday, before 9:25 AM ET ──────────────────────────────────┐
│  DOW % change vs. previous close (static)                      │
└────────────────────────────────────────────────────────────────┘

┌─ 9:25 – 9:30 AM ET ───────────────────────────────────────────┐
│  "5 MINUTES UNTIL MARKET OPEN"  (scrolling)                    │
└────────────────────────────────────────────────────────────────┘

┌─ 9:30 AM – 3:55 PM ET  (market hours) ────────────────────────┐
│  Stock 1: price (4.5 s) → % change (4.5 s) → ...  ×30 s       │
│  Stock 2: same pattern ×30 s                                   │
│  ...                                                           │
│  Top of every hour → DOW % change vs. prev close  (30 s)       │
└────────────────────────────────────────────────────────────────┘

┌─ 3:55 – 4:00 PM ET ───────────────────────────────────────────┐
│  "5 MINUTES UNTIL MARKET CLOSE"  (scrolling)                   │
└────────────────────────────────────────────────────────────────┘

┌─ After 4:00 PM ET / weekends ─────────────────────────────────┐
│  Stock 1 price → % change ×30 s → Stock 2 … → DOW ×30 s       │
│  Every :00 :10 :20 :30 :40 :50 → each stock price blinks 30 s │
└────────────────────────────────────────────────────────────────┘
```

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Display garbled / mirrored | Change `MAX_HARDWARE_TYPE` in `config.h` (`PAROLA_HW`, `ICSTATION_HW`, `GENERIC_HW`) |
| Display very dim | Increase `DISPLAY_INTENSITY` (max 15); check 5 V power supply |
| "NO WIFI" on display | Wrong password — double-reset to re-enter credentials |
| Prices never update | Yahoo Finance endpoint may be temporarily rate-limiting; prices will resume within a few minutes |
| Setup portal page doesn't open | Navigate manually to `http://192.168.4.1` |
| Sketch won't upload | Try lowering upload speed to `115200`; check USB cable and port |

---

## Known Limitations

- **Market holidays** are not detected; the device will attempt fetches on US holidays.
- **Yahoo Finance API** is an unofficial endpoint and may change without notice.
- **Pre-market / after-hours prices** are not shown; the last regular-session close is used outside market hours.
- **TLS certificate pinning** is disabled (`setInsecure()`) to handle Yahoo's rotating certificates.

---

## License

MIT — see [LICENSE](LICENSE).
