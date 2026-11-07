# ESP32 LCD Weather Display

Current weather, a clock and a 3-day forecast on the **Waveshare ESP32-S3-Touch-LCD-4** (480×480).

- Weather comes from [Open-Meteo](https://open-meteo.com) and your location from [ip-api.com](https://ip-api.com). Both are free and need no account or API key.
- The timezone is detected automatically.
- The only thing you set up is your Wi-Fi.

## Setup

1. Install [VS Code](https://code.visualstudio.com) and the **pioarduino IDE** extension. Don't install the regular PlatformIO extension alongside it; the two conflict.
2. Open this folder in VS Code.
3. Plug the board in over USB-C and click **Upload** (→) in the bottom status bar.
   The first build downloads the toolchain, which takes several minutes.
4. On first start the display shows a setup screen. Join the Wi-Fi network it names with your phone, using the password on screen. The settings page opens by itself; if it doesn't, go to `http://192.168.4.1`. Enter your Wi-Fi and press **Save**. The board supports 2.4 GHz networks only.

You can also put your Wi-Fi in `include/secrets.h` (copy `include/secrets.example.h`) so it's set before the first start.

If the upload can't find the board, put it in download mode: hold **BOOT**, tap **RESET**, release **BOOT**, then upload again. Press **RESET** when the upload finishes.

## Usage

- The weather refreshes every 10 minutes. Tap **BOOT** to refresh right away.
- To change settings (location, units, refresh interval, Wi-Fi), open `http://weather.local` from a device on the same Wi-Fi. If that doesn't load (common on eduroam and other shared networks), **hold BOOT for 2 seconds**. The display then opens its own setup network, as in step 4. It closes again after you save, after 5 minutes unused, or when you press BOOT.
- Location can be automatic (from your IP address), a city name, or exact coordinates.
- Settings saved on the page are kept when you re-upload the code, and they take priority over `include/config.h`. **Reset to defaults** on the page goes back to `config.h`.
- To see logs, open the Serial Monitor (🔌) at 115200 baud.

Anyone on the same Wi-Fi can open the settings page. It never shows your saved Wi-Fi password.

## Code

| File | What it does |
|---|---|
| `src/main.cpp` | Display pins, Wi-Fi, API calls, main loop |
| `src/portal.cpp` | Settings web page, setup network, `weather.local` |
| `src/settings.cpp` | Saves settings in flash, falls back to `config.h` defaults |
| `src/ui.cpp` | Layout, color themes, weather icons drawn from shapes (no image files) |
| `include/config.h` | User settings |
