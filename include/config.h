#pragma once

// ---- Defaults (no changes needed) ----
// Everything here except HOSTNAME can also be changed on the display's settings
// page (see README). Settings saved there take priority over this file.

// Units: false = Celsius + km/h, true = Fahrenheit + mph
#define USE_FAHRENHEIT false

// Location. Leave LATITUDE at 0 to auto-detect from your IP address.
#define LATITUDE   34.0522
#define LONGITUDE  -118.2437
#define CITY_NAME  "Los Angeles"   // shown on screen when using a fixed location

// How often to fetch new weather (minutes). Open-Meteo updates ~every 15 min.
#define WEATHER_REFRESH_MIN 10

// Settings page address on your Wi-Fi: http://HOSTNAME.local
#define HOSTNAME "weather"
