// All drawing goes to an off-screen canvas, then main.cpp flushes it in one go
// (no flicker). Icons are drawn from primitives, so there are no image assets.

#include "weather.h"
#include "config.h"
#include <U8g2lib.h>   // fonts only
#include <time.h>

static Arduino_GFX *g;

// ---------------------------------------------------------------- colors

struct RGB { uint8_t r, g, b; };

static uint16_t c565(RGB c) { return ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3); }
static RGB mix(RGB a, RGB b, float t) {
  return { (uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t) };
}

static const RGB WHITE_C = {255, 255, 255};
static const uint16_t WHITE  = 0xFFFF;
static const uint16_t SUN    = c565({255, 200, 60});
static const uint16_t MOON   = c565({240, 236, 210});
static const uint16_t CLOUD  = c565({236, 241, 247});
static const uint16_t CLOUD2 = c565({170, 184, 202});
static const uint16_t STORMC = c565({120, 130, 148});
static const uint16_t RAIN   = c565({120, 190, 255});
static const uint16_t BOLT   = c565({255, 214, 64});

static RGB bgTop, bgBottom;

static RGB bgAt(int y) { return mix(bgTop, bgBottom, (float)y / (SCREEN_H - 1)); }

static void drawBackground() {
  for (int y = 0; y < SCREEN_H; y++) g->drawFastHLine(0, y, SCREEN_W, c565(bgAt(y)));
}

static void pickTheme(int code, bool day) {
  if (code >= 95)                     { bgTop = {28, 30, 40};   bgBottom = {62, 66, 82}; }
  else if (!day)                      { bgTop = {10, 18, 38};   bgBottom = {36, 58, 92}; }
  else if (code <= 1)                 { bgTop = {33, 110, 220}; bgBottom = {90, 190, 240}; }
  else if (code == 2)                 { bgTop = {52, 106, 178}; bgBottom = {120, 166, 210}; }
  else if (code >= 51 && code <= 82)  { bgTop = {44, 60, 84};   bgBottom = {96, 116, 142}; }
  else                                { bgTop = {70, 88, 114};  bgBottom = {130, 150, 176}; }
}

// ---------------------------------------------------------------- text

static void text(const char *s, int x, int y, const uint8_t *font, uint16_t color, int align = -1) {
  // align: -1 left, 0 center, 1 right. y is the text baseline.
  g->setFont(font);
  g->setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, y, &x1, &y1, &w, &h);
  if (align == 0) x -= w / 2 + x1;
  else if (align == 1) x -= w + x1;
  g->setCursor(x, y);
  g->print(s);
}

static int textWidth(const char *s, const uint8_t *font) {
  g->setFont(font);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static const char *conditionText(int c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow showers";
    default: return c >= 95 ? "Thunderstorm" : "Unknown";
  }
}

// ---------------------------------------------------------------- icons

static void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) return;
  float nx = -dy / len * w / 2, ny = dx / len * w / 2;
  g->fillTriangle(x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 + nx, y1 + ny, c);
  g->fillTriangle(x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, c);
  g->fillCircle(x0, y0, w / 2, c);
  g->fillCircle(x1, y1, w / 2, c);
}

static void sun(int cx, int cy, int r) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    thickLine(cx + cosf(a) * r * 1.35f, cy + sinf(a) * r * 1.35f,
              cx + cosf(a) * r * 1.75f, cy + sinf(a) * r * 1.75f, max(2, r / 5), SUN);
  }
  g->fillCircle(cx, cy, r, SUN);
}

static void moon(int cx, int cy, int r) {
  // Crescent = disc minus an offset disc
  int ox = cx + r * 0.45f, oy = cy - r * 0.35f, r2 = r * 0.85f;
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y > r * r) continue;
      int ex = cx + x - ox, ey = cy + y - oy;
      if (ex * ex + ey * ey < r2 * r2) continue;
      g->drawPixel(cx + x, cy + y, MOON);
    }
}

// s = half width. Occupies roughly cy-0.68s .. cy+0.5s vertically.
static void cloud(int cx, int cy, int s, uint16_t c) {
  g->fillRoundRect(cx - s, cy, 2 * s, s / 2, s / 4, c);
  g->fillCircle(cx - s * 0.45f, cy + s * 0.05f, s * 0.42f, c);
  g->fillCircle(cx + s * 0.10f, cy - s * 0.10f, s * 0.58f, c);
  g->fillCircle(cx + s * 0.60f, cy + s * 0.12f, s * 0.36f, c);
}

static void drops(int cx, int top, int S, uint16_t c, bool snow) {
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * S * 0.45f, y = top + (i == 0 ? S * 0.12f : 0);
    if (snow) {
      g->fillCircle(x, y + S * 0.12f, max(2, S / 9), WHITE);
      g->fillCircle(x - S * 0.2f, y + S * 0.42f, max(2, S / 11), WHITE);
    } else {
      thickLine(x, y, x - S * 0.15f, y + S * 0.38f, max(2, S / 8), c);
    }
  }
}

// Draws an icon for a WMO weather code, fitting in a box ~2S x 2S around (cx, cy).
static void icon(int code, bool day, int cx, int cy, int S) {
  if (code == 0) {
    day ? sun(cx, cy, S * 0.55f) : moon(cx, cy, S * 0.75f);
  } else if (code <= 2) {
    day ? sun(cx - S * 0.3f, cy - S * 0.3f, S * 0.42f) : moon(cx - S * 0.3f, cy - S * 0.35f, S * 0.5f);
    cloud(cx + S * 0.15f, cy + S * 0.2f, S * (code == 1 ? 0.55f : 0.72f), CLOUD);
  } else if (code == 3) {
    cloud(cx + S * 0.25f, cy - S * 0.15f, S * 0.6f, CLOUD2);
    cloud(cx - S * 0.1f, cy + S * 0.15f, S * 0.8f, CLOUD);
  } else if (code == 45 || code == 48) {
    cloud(cx, cy - S * 0.3f, S * 0.75f, CLOUD2);
    for (int i = 0; i < 3; i++) {
      float y = cy + S * (0.35f + i * 0.22f), inset = (i == 1) ? 0.2f : 0;
      thickLine(cx - S * (0.8f - inset), y, cx + S * (0.8f - inset * 2), y, max(2, S / 9), CLOUD);
    }
  } else if (code >= 95) {
    cloud(cx, cy - S * 0.25f, S * 0.85f, STORMC);
    int bx = cx, by = cy + S * 0.2f;
    g->fillTriangle(bx + S * 0.1f, by, bx - S * 0.3f, by + S * 0.4f, bx, by + S * 0.4f, BOLT);
    g->fillTriangle(bx + S * 0.15f, by + S * 0.3f, bx - S * 0.15f, by + S * 0.3f, bx - S * 0.2f, by + S * 0.85f, BOLT);
  } else {
    bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    cloud(cx, cy - S * 0.25f, S * 0.85f, CLOUD);
    drops(cx, cy + S * 0.35f, S, RAIN, snow);
  }
}

// ---------------------------------------------------------------- screens

void uiBegin(Arduino_GFX *canvas) {
  g = canvas;
  g->setUTF8Print(true);   // city names like "Zürich" / "Çankaya"
}

void uiMessage(const char *title, const char *subtitle) {
  bgTop = {16, 22, 36}; bgBottom = {40, 52, 76};
  drawBackground();
  text(title, SCREEN_W / 2, 230, u8g2_font_logisoso28_tf, WHITE, 0);
  if (subtitle && *subtitle) text(subtitle, SCREEN_W / 2, 280, u8g2_font_logisoso16_tf, c565({180, 194, 214}), 0);
}

static int weekday(const char *ymd) {   // 0 = Sunday (Sakamoto)
  int y = atoi(ymd), m = atoi(ymd + 5), d = atoi(ymd + 8);
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void uiSetup(const char *apName, const char *apPass, const char *reason, const char *lanIp) {
  const uint16_t DIM = c565({180, 194, 214});
  const uint16_t ACCENT = c565({120, 190, 255});
  bgTop = {16, 22, 36}; bgBottom = {40, 52, 76};
  drawBackground();
  const int cx = SCREEN_W / 2;
  text("Settings", cx, 62, u8g2_font_logisoso28_tf, WHITE, 0);
  if (reason && *reason) text(reason, cx, 98, u8g2_font_logisoso16_tf, c565({255, 170, 150}), 0);

  text("1. On your phone, join the Wi-Fi", cx, 150, u8g2_font_logisoso16_tf, DIM, 0);
  text(apName, cx, 186, u8g2_font_logisoso22_tf, WHITE, 0);
  char buf[48];
  snprintf(buf, sizeof buf, "Password: %s", apPass);
  text(buf, cx, 220, u8g2_font_logisoso22_tf, WHITE, 0);

  text("2. The settings page opens. If not, go to", cx, 270, u8g2_font_logisoso16_tf, DIM, 0);
  text("http://192.168.4.1", cx, 304, u8g2_font_logisoso22_tf, ACCENT, 0);

  if (lanIp && *lanIp) {
    text("Or on the same Wi-Fi as the display:", cx, 354, u8g2_font_logisoso16_tf, DIM, 0);
    snprintf(buf, sizeof buf, "http://%s.local  (%s)", HOSTNAME, lanIp);
    text(buf, cx, 382, u8g2_font_logisoso16_tf, WHITE, 0);
  }
  text("Press BOOT to go back", cx, 448, u8g2_font_logisoso16_tf, DIM, 0);
}

void uiWeather(const Weather &w, const char *city, bool stale, bool fahrenheit) {
  static const char *DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  const uint16_t DIM = c565({205, 218, 236});
  char buf[48];

  pickTheme(w.code, w.isDay);
  drawBackground();

  // --- header: city + date (left), clock (right)
  text(city, 28, 50, u8g2_font_logisoso22_tf, WHITE);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    snprintf(buf, sizeof buf, "%s %d %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    text(buf, 28, 80, u8g2_font_logisoso16_tf, DIM);
    strftime(buf, sizeof buf, "%H:%M", &t);
    text(buf, SCREEN_W - 28, 62, u8g2_font_logisoso38_tn, WHITE, 1);
  }
  if (stale) text("offline", SCREEN_W - 28, 88, u8g2_font_logisoso16_tf, c565({255, 170, 150}), 1);

  // --- hero: icon + temperature + condition
  icon(w.code, w.isDay, 128, 190, 82);

  snprintf(buf, sizeof buf, "%d", (int)lroundf(w.temp));
  int tx = 345, tw = textWidth(buf, u8g2_font_logisoso92_tn);
  text(buf, tx, 236, u8g2_font_logisoso92_tn, WHITE, 0);
  g->fillCircle(tx + tw / 2 + 16, 152, 9, WHITE);           // degree sign
  g->fillCircle(tx + tw / 2 + 16, 152, 4, c565(bgAt(152)));

  text(conditionText(w.code), tx, 280, u8g2_font_logisoso20_tf, WHITE, 0);

  // --- details row
  const char *wu = fahrenheit ? "mph" : "km/h";
  struct { const char *label; char value[16]; } det[3] = {{"Feels like"}, {"Humidity"}, {"Wind"}};
  snprintf(det[0].value, 16, "%d°", (int)lroundf(w.feels));
  snprintf(det[1].value, 16, "%d%%", w.humidity);
  snprintf(det[2].value, 16, "%d %s", (int)lroundf(w.wind), wu);
  for (int i = 0; i < 3; i++) {
    int cx = 80 + i * 160;
    text(det[i].label, cx, 322, u8g2_font_logisoso16_tf, DIM, 0);
    text(det[i].value, cx, 352, u8g2_font_logisoso22_tf, WHITE, 0);
  }

  // --- 3-day forecast cards (skip today)
  const int cw = 140, ch = 100, gap = 15, top = 368;
  int x0 = (SCREEN_W - 3 * cw - 2 * gap) / 2;
  for (int i = 1; i < FORECAST_DAYS; i++) {
    int x = x0 + (i - 1) * (cw + gap);
    g->fillRoundRect(x, top, cw, ch, 16, c565(mix(bgAt(top + ch / 2), WHITE_C, 0.16f)));
    text(i == 1 ? "Tomorrow" : DAYS[weekday(w.dDate[i])], x + 14, top + 28, u8g2_font_logisoso16_tf, WHITE);
    if (w.dRain[i] >= 20) {
      snprintf(buf, sizeof buf, "%d%%", w.dRain[i]);
      text(buf, x + cw - 12, top + 28, u8g2_font_logisoso16_tf, RAIN, 1);
    }
    icon(w.dCode[i], true, x + 34, top + 66, 20);
    snprintf(buf, sizeof buf, "%d°", (int)lroundf(w.dMax[i]));
    text(buf, x + cw - 12, top + 70, u8g2_font_logisoso22_tf, WHITE, 1);
    snprintf(buf, sizeof buf, "%d°", (int)lroundf(w.dMin[i]));
    text(buf, x + cw - 12, top + 92, u8g2_font_logisoso16_tf, DIM, 1);
  }
}
