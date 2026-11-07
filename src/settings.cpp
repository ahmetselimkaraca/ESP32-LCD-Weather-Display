#include <Preferences.h>
#include "settings.h"
#include "config.h"

// secrets.h is optional now: Wi-Fi can also be set from the settings page.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD ""
#endif
#ifndef WIFI_USERNAME
  #define WIFI_USERNAME ""
#endif

Settings cfg;
static Preferences prefs;

static void getStr(const char *key, char *dst, size_t n, const char *def) {
  strlcpy(dst, prefs.getString(key, def).c_str(), n);
}

void settingsLoad() {
  prefs.begin("weather", false);
  getStr("ssid", cfg.ssid, sizeof cfg.ssid, WIFI_SSID);
  getStr("user", cfg.user, sizeof cfg.user, WIFI_USERNAME);
  getStr("pass", cfg.pass, sizeof cfg.pass, WIFI_PASSWORD);
  cfg.locMode    = prefs.getUChar("loc", LATITUDE != 0.0 ? LOC_COORDS : LOC_AUTO);
  getStr("city", cfg.city, sizeof cfg.city, CITY_NAME);
  cfg.lat        = prefs.getFloat("lat", LATITUDE);
  cfg.lon        = prefs.getFloat("lon", LONGITUDE);
  cfg.fahrenheit = prefs.getBool("fahr", USE_FAHRENHEIT);
  cfg.refreshMin = prefs.getUShort("refresh", WEATHER_REFRESH_MIN);

  // Each board gets its own random setup-network password on first boot.
  getStr("appass", cfg.apPass, sizeof cfg.apPass, "");
  if (strlen(cfg.apPass) < 8) {
    static const char chars[] = "abcdefghjkmnpqrstuvwxyz23456789";   // no look-alikes (l/1, o/0)
    for (int i = 0; i < 10; i++) cfg.apPass[i] = chars[esp_random() % (sizeof chars - 1)];
    cfg.apPass[10] = 0;
    prefs.putString("appass", cfg.apPass);
  }
  prefs.end();
}

void settingsSave() {
  prefs.begin("weather", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("user", cfg.user);
  prefs.putString("pass", cfg.pass);
  prefs.putUChar("loc", cfg.locMode);
  prefs.putString("city", cfg.city);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  prefs.putBool("fahr", cfg.fahrenheit);
  prefs.putUShort("refresh", cfg.refreshMin);
  prefs.putString("appass", cfg.apPass);
  prefs.end();
}

void settingsReset() {
  prefs.begin("weather", false);
  prefs.clear();
  prefs.putString("appass", cfg.apPass);   // so a phone on the setup network stays connected
  prefs.end();
  settingsLoad();
}
