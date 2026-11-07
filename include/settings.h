#pragma once
#include <Arduino.h>

// Runtime settings. Defaults come from config.h / secrets.h; anything saved
// from the settings web page is kept in flash (NVS) and wins over the defaults.

enum LocationMode : uint8_t { LOC_AUTO = 0, LOC_CITY = 1, LOC_COORDS = 2 };

struct Settings {
  char ssid[33], user[64], pass[64];   // user non-empty = WPA2-Enterprise (eduroam)
  uint8_t locMode;
  char city[48];                       // LOC_CITY: what to look up. LOC_COORDS: label only.
  float lat, lon;
  bool fahrenheit;
  uint16_t refreshMin;
  char apPass[16];                     // password of the device's own setup network
};

extern Settings cfg;

// settings.cpp
void settingsLoad();
void settingsSave();
void settingsReset();   // back to config.h defaults (keeps the setup-network password)

// portal.cpp: settings web page, setup network, weather.local
void portalBegin();                      // call once, after WiFi.mode()
void portalLoop();                       // call often from loop()
void portalStartAP();
void portalStopAP();
const char *portalAPName();
unsigned long portalIdleMs();            // time since the last page request / AP client
bool portalTakeChanges(bool &wifiChanged);   // true once after each save or reset

// main.cpp: one-line status shown at the top of the settings page
String portalStatus();
