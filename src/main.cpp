// Weather display for the Waveshare ESP32-S3-Touch-LCD-4 (touch unused).
// Data: Open-Meteo (weather + city search) and ip-api.com (location). All free, no API key.
// Settings: web page on your Wi-Fi, or hold BOOT 2 s for the board's own setup network.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "weather.h"
#include "config.h"
#include "settings.h"

// ---------------------------------------------------------------- display
// Pin map + timings for the board's ST7701 480x480 RGB panel.
static Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 42 /*CS*/, 2 /*SCK*/, 1 /*MOSI*/, GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *panel = new Arduino_ESP32RGBPanel(
    40 /*DE*/, 39 /*VSYNC*/, 38 /*HSYNC*/, 41 /*PCLK*/,
    46, 3, 8, 18, 17,        // R0-R4
    14, 13, 12, 11, 10, 9,   // G0-G5
    5, 45, 48, 47, 21,       // B0-B4
    1, 10, 8, 50,            // hsync polarity, front porch, pulse, back porch
    1, 10, 8, 20);           // vsync polarity, front porch, pulse, back porch
static Arduino_RGB_Display *lcd = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, panel, 2 /*rotation*/, true, bus, GFX_NOT_DEFINED,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));
static Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, lcd);

static void powerOnPanel() {
  // The board's I/O expander (I2C 0x24) controls LCD reset and backlight.
  Wire.begin(15 /*SDA*/, 7 /*SCL*/);
  Wire.beginTransmission(0x24); Wire.write(0x02); Wire.write(0xFF); Wire.endTransmission(); // outputs high
  Wire.beginTransmission(0x24); Wire.write(0x03); Wire.write(0x3A); Wire.endTransmission(); // direction
}

static void show() { canvas->flush(); }

// ---------------------------------------------------------------- state

static const int BOOT_BUTTON = 0;   // tap: refresh now. Hold 2 s: settings.
static const unsigned long LONG_PRESS_MS = 2000;
static const unsigned long SETUP_TIMEOUT_MS = 5 * 60000UL;   // close setup network when unused

static Weather weather;
static char city[48] = "";
static float lat = 0, lon = 0;
static bool haveLocation = false;
static long tzOffset = LONG_MIN;
static unsigned long lastFetch = 0, lastSuccess = 0;
static bool fetchedOnce = false, lastFetchOk = false, wifiFailed = false;
static bool captivePortal = false;   // network redirects us to a login page
static bool cityNotFound = false;

static bool setupMode = false;
static char setupReason[64] = "";
static unsigned long exitSetupAt = 0, restartAt = 0;

static const char *HINT = "Hold BOOT for settings - retrying in 1 min";

// ---------------------------------------------------------------- network

static bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  uiMessage("Connecting to Wi-Fi", cfg.ssid); show();
  WiFi.mode(WIFI_STA);
  if (strlen(cfg.user) > 0) {
    // WPA2-Enterprise (eduroam, university/corporate networks): PEAP + MSCHAPv2.
    // No CA certificate is set, so the RADIUS server's identity isn't verified.
    WiFi.begin(cfg.ssid, WPA2_AUTH_PEAP, cfg.user, cfg.user, cfg.pass);
  } else {
    WiFi.begin(cfg.ssid, cfg.pass);
  }
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s  MAC %s  -> settings at http://%s.local\n",
                  WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str(), HOSTNAME);
    return true;
  }
  Serial.println("[wifi] failed");
  return false;
}

// GET a URL into body. Returns the HTTP status, or a negative HTTPClient error.
static int httpGet(const char *url, bool secure, String &body) {
  WiFiClientSecure tls;
  WiFiClient plain;
  if (secure) { tls.setInsecure(); tls.setTimeout(15); }  // public data; skips certificate bundle setup
  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  unsigned long t0 = millis();
  bool ok = secure ? http.begin(tls, url) : http.begin(plain, url);
  if (!ok) { Serial.println("[http] begin failed"); return -100; }
  int code = http.GET();
  Serial.printf("[http] %s -> %d (%s) after %lu ms\n", secure ? "HTTPS" : "HTTP", code,
                code < 0 ? HTTPClient::errorToString(code).c_str() : "ok", millis() - t0);
  if (code < 0 && secure) {
    char err[128];
    int e = tls.lastError(err, sizeof err);
    Serial.printf("[http] TLS error %d: %s\n", e, err);
  }
  if (code > 0) body = http.getString();
  http.end();
  return code;
}

// One-time diagnostic: which hosts/ports does this network let us reach?
static void probeNetwork() {
  struct { const char *host; uint16_t port; } targets[] = {
    {"api.open-meteo.com", 443}, {"api.open-meteo.com", 80},
    {"www.google.com", 443},     {"www.google.com", 80},
    {"1.1.1.1", 443},
  };
  Serial.println("[probe] testing raw TCP connections (5 s timeout each)...");
  for (auto &t : targets) {
    WiFiClient c;
    unsigned long t0 = millis();
    bool ok = c.connect(t.host, t.port, 5000);
    Serial.printf("[probe] %-20s :%-3u %s (%lu ms)\n", t.host, t.port, ok ? "OPEN" : "BLOCKED/TIMEOUT", millis() - t0);
    c.stop();
  }
}

// HTTPS GET with a plain-HTTP fallback.
// Some networks block or intercept HTTPS from unknown devices. The data is
// public, so falling back to plain HTTP is acceptable.
static int getPublic(const char *url, String &body) {
  int code = httpGet(url, true, body);
  if (code >= 0) return code;
  static bool probed = false;
  if (!probed) { probed = true; probeNetwork(); }
  String plain = String(url);
  plain.replace("https://", "http://");
  Serial.println("[http] HTTPS failed, retrying over plain HTTP");
  code = httpGet(plain.c_str(), false, body);
  // HTTPS blocked + HTTP redirected = the network wants a browser login first.
  captivePortal = (code >= 300 && code < 400);
  if (captivePortal)
    Serial.printf("[http] Captive portal detected. Register this device's MAC (%s) with the network provider.\n",
                  WiFi.macAddress().c_str());
  return code;
}

static bool fetchIpLocation() {
  // ip-api.com free tier is HTTP only; fine for a coarse location lookup.
  String body;
  int code = httpGet("http://ip-api.com/json/?fields=status,city,lat,lon", false, body);
  if (code != 200) { Serial.printf("[geo] HTTP %d\n", code); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, body) || doc["status"] != "success") return false;
  lat = doc["lat"]; lon = doc["lon"];
  strlcpy(city, doc["city"] | "", sizeof city);
  Serial.printf("[geo] %s (%.3f, %.3f)\n", city, lat, lon);
  return true;
}

static String urlEncode(const char *s) {
  String o;
  for (; *s; s++) {
    uint8_t c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

static bool geocodeCity() {
  // Open-Meteo's search wants just the place name, so drop ", Country" etc.
  String q = cfg.city;
  int comma = q.indexOf(',');
  if (comma > 0) q = q.substring(0, comma);
  q.trim();
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" + urlEncode(q.c_str());
  String body;
  int code = getPublic(url.c_str(), body);
  if (code != 200) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObject r = doc["results"][0];
  if (r.isNull()) { cityNotFound = true; Serial.printf("[geo] no match for \"%s\"\n", q.c_str()); return false; }
  lat = r["latitude"]; lon = r["longitude"];
  strlcpy(city, r["name"] | cfg.city, sizeof city);
  Serial.printf("[geo] %s, %s (%.3f, %.3f)\n", city, (const char *)(r["country"] | ""), lat, lon);
  return true;
}

static bool resolveLocation() {
  cityNotFound = false;
  switch (cfg.locMode) {
    case LOC_CITY: return geocodeCity();
    case LOC_COORDS:
      lat = cfg.lat; lon = cfg.lon;
      if (strlen(cfg.city)) strlcpy(city, cfg.city, sizeof city);
      else snprintf(city, sizeof city, "%.2f, %.2f", lat, lon);
      return true;
    default: return fetchIpLocation();
  }
}

static bool fetchWeather() {
  char url[512];
  snprintf(url, sizeof url,
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&timezone=auto&forecast_days=%d%s",
    lat, lon, FORECAST_DAYS, cfg.fahrenheit ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");

  Serial.printf("[weather] GET %s\n", url);
  Serial.printf("[weather] IP %s  DNS %s  RSSI %d dBm  heap %u  largest block %u\n",
                WiFi.localIP().toString().c_str(), WiFi.dnsIP().toString().c_str(), WiFi.RSSI(),
                ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  String body;
  int code = getPublic(url, body);
  if (code != 200) {
    if (code > 0) Serial.printf("[weather] body: %.300s\n", body.c_str());
    return false;
  }
  Serial.printf("[weather] %u bytes received\n", body.length());
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[weather] JSON: %s\n[weather] body: %.300s\n", err.c_str(), body.c_str());
    return false;
  }

  JsonObject cur = doc["current"];
  weather.temp     = cur["temperature_2m"];
  weather.feels    = cur["apparent_temperature"];
  weather.humidity = cur["relative_humidity_2m"];
  weather.wind     = cur["wind_speed_10m"];
  weather.code     = cur["weather_code"];
  weather.isDay    = cur["is_day"] == 1;

  JsonObject d = doc["daily"];
  for (int i = 0; i < FORECAST_DAYS; i++) {
    weather.dCode[i] = d["weather_code"][i];
    weather.dMax[i]  = d["temperature_2m_max"][i];
    weather.dMin[i]  = d["temperature_2m_min"][i];
    weather.dRain[i] = d["precipitation_probability_max"][i] | 0;
    strlcpy(weather.dDate[i], d["time"][i] | "", sizeof weather.dDate[i]);
  }
  weather.valid = true;

  // Open-Meteo tells us the local UTC offset, so no timezone setup is needed.
  long offset = doc["utc_offset_seconds"] | 0;
  if (offset != tzOffset) {
    tzOffset = offset;
    configTime(offset, 0, "pool.ntp.org", "time.google.com");
  }
  Serial.printf("[weather] %.1f, code %d\n", weather.temp, weather.code);
  return true;
}

static void refresh() {
  lastFetch = millis();
  fetchedOnce = true;
  lastFetchOk = false;
  wifiFailed = !connectWifi();
  if (wifiFailed) return;   // loop() opens the setup network if there's no weather yet
  if (!haveLocation) {
    if (!weather.valid) { uiMessage("Finding location", cfg.locMode == LOC_CITY ? cfg.city : ""); show(); }
    haveLocation = resolveLocation();
    if (!haveLocation) {
      if (!weather.valid) {
        uiMessage(cityNotFound ? "City not found" : "Location lookup failed", HINT);
        show();
      }
      return;
    }
  }
  if (!weather.valid) { uiMessage("Loading weather", city); show(); }
  lastFetchOk = fetchWeather();
  if (lastFetchOk) lastSuccess = millis();
  else if (!weather.valid && captivePortal) {
    String sub = "Register MAC " + WiFi.macAddress() + " with your provider";
    uiMessage("Wi-Fi needs a login page", sub.c_str()); show();
  }
  else if (!weather.valid) { uiMessage("Weather download failed", HINT); show(); }
}

static void render() {
  if (!weather.valid) return;
  bool stale = millis() - lastSuccess > 3UL * cfg.refreshMin * 60000UL;
  uiWeather(weather, city, stale, cfg.fahrenheit);
  show();
}

// ---------------------------------------------------------------- settings mode

static void drawSetup() {
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  uiSetup(portalAPName(), cfg.apPass, setupReason, ip.c_str());
  show();
}

static void enterSetup(const char *reason) {
  setupMode = true;
  strlcpy(setupReason, reason, sizeof setupReason);
  uiMessage("Opening settings", ""); show();
  portalStartAP();
  drawSetup();
}

static void exitSetup() {
  setupMode = false;
  exitSetupAt = 0;
  portalStopAP();
  if (weather.valid) render();
  else fetchedOnce = false;   // try again right away
}

// 0 = not pressed, 1 = tap, 2 = held for LONG_PRESS_MS
static int readButton() {
  if (digitalRead(BOOT_BUTTON) == HIGH) return 0;
  unsigned long t0 = millis();
  bool hinted = false;
  while (digitalRead(BOOT_BUTTON) == LOW) {
    if (!hinted && !setupMode && millis() - t0 >= LONG_PRESS_MS) {
      hinted = true;
      uiMessage("Release for settings", ""); show();
    }
    delay(10);
  }
  return millis() - t0 >= LONG_PRESS_MS ? 2 : 1;
}

String portalStatus() {
  String s = weather.valid
    ? "Showing " + String(city) + ", updated " + String((millis() - lastSuccess) / 60000) + " min ago"
    : String("No weather yet");
  if (WiFi.status() == WL_CONNECTED) s += " - on " + WiFi.SSID() + " as " + WiFi.localIP().toString();
  else s += " - not connected to Wi-Fi";
  return s;
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  powerOnPanel();
  if (!lcd->begin() || !canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    Serial.println("[lcd] init failed (PSRAM?)");
    while (true) delay(1000);
  }
  uiBegin(canvas);

  settingsLoad();
  WiFi.persistent(false);   // credentials live in our own settings, not the Wi-Fi driver's
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  portalBegin();
}

void loop() {
  portalLoop();

  bool wifiChanged;
  if (portalTakeChanges(wifiChanged)) {
    if (wifiChanged) {
      restartAt = millis() + 2000;   // let the browser get its reply first
    } else {
      haveLocation = false;          // location or units may have changed
      weather.valid = false;
      fetchedOnce = false;
      if (setupMode) exitSetupAt = millis() + 3000;
    }
  }
  if (restartAt && (long)(millis() - restartAt) >= 0) ESP.restart();
  if (exitSetupAt && (long)(millis() - exitSetupAt) >= 0) exitSetup();

  int button = readButton();

  if (setupMode) {
    if (button || (!exitSetupAt && !restartAt && portalIdleMs() > SETUP_TIMEOUT_MS)) exitSetup();
    delay(5);
    return;
  }
  if (restartAt) { delay(5); return; }

  if (button == 2) { enterSetup(""); return; }
  if (strlen(cfg.ssid) == 0) { enterSetup("Wi-Fi is not set up yet"); return; }

  unsigned long interval = lastFetchOk ? cfg.refreshMin * 60000UL : 60000UL;
  if (!fetchedOnce || millis() - lastFetch > interval || button == 1) {
    if (button == 1 && weather.valid) { uiMessage("Refreshing...", ""); show(); }
    refresh();
    if (wifiFailed && !weather.valid) {
      char reason[64];
      snprintf(reason, sizeof reason, "Couldn't connect to %s", cfg.ssid);
      enterSetup(reason);
      return;
    }
    render();
  }

  // Redraw when the minute changes so the clock stays current.
  static int lastMinute = -1;
  struct tm t;
  if (getLocalTime(&t, 0) && t.tm_min != lastMinute) {
    lastMinute = t.tm_min;
    render();
  }
  delay(5);
}
