#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#define SCREEN_W 480
#define SCREEN_H 480
#define FORECAST_DAYS 4   // today + 3

struct Weather {
  bool  valid = false;
  float temp = 0, feels = 0, wind = 0;
  int   humidity = 0, code = 0;
  bool  isDay = true;

  int   dCode[FORECAST_DAYS] = {};
  float dMax[FORECAST_DAYS] = {}, dMin[FORECAST_DAYS] = {};
  int   dRain[FORECAST_DAYS] = {};          // precipitation probability %
  char  dDate[FORECAST_DAYS][11] = {};      // "YYYY-MM-DD"
};

// ui.cpp
void uiBegin(Arduino_GFX *canvas);
void uiMessage(const char *title, const char *subtitle);
void uiWeather(const Weather &w, const char *city, bool stale, bool fahrenheit);
// Settings mode: how to reach the settings page. lanIp empty = not on Wi-Fi.
void uiSetup(const char *apName, const char *apPass, const char *reason, const char *lanIp);
