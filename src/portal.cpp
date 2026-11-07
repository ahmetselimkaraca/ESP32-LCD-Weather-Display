// Settings web page. Reachable two ways:
//  - on your normal Wi-Fi at http://<HOSTNAME>.local (or the board's IP)
//  - on the board's own setup network (hold BOOT 2 s) at http://192.168.4.1,
//    which phones usually open by themselves as a "sign in to network" page.

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "settings.h"
#include "config.h"

static WebServer server(80);
static DNSServer dns;
static bool apOn = false;
static char apName[32];
static String networkOptions;   // <option>s for the Wi-Fi name field
static unsigned long lastActivity = 0;
static bool changed = false, changedWifi = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Weather display</title><style>
:root{color-scheme:light dark;--bg:#f4f6fa;--card:#fff;--fg:#1b2333;--dim:#5b6678;--acc:#2f6fdf;--line:#d8dee8}
@media(prefers-color-scheme:dark){:root{--bg:#10151f;--card:#1a2130;--fg:#e8edf5;--dim:#98a3b6;--acc:#6ea2ff;--line:#2c3547}}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,sans-serif}
main{max-width:480px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:8px 0 4px}.st{color:var(--dim);font-size:14px;margin:0 0 16px}
fieldset{background:var(--card);border:1px solid var(--line);border-radius:12px;margin:0 0 14px;padding:12px 14px}
legend{font-weight:600;padding:0 4px}
label{display:block;margin:10px 0 4px;font-size:14px;color:var(--dim)}
input[type=text],input[type=password],input[type=number]{box-sizing:border-box;width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);font-size:16px}
.r{display:flex;gap:16px;flex-wrap:wrap}.r label{display:flex;gap:6px;align-items:center;color:var(--fg);margin:6px 0;font-size:16px}
.row{display:flex;gap:10px}.row>div{flex:1}
button{width:100%;padding:12px;border:0;border-radius:10px;background:var(--acc);color:#fff;font-size:16px;font-weight:600}
.sec{background:none;color:var(--dim);border:1px solid var(--line);margin-top:4px}
.hint{font-size:13px;color:var(--dim);margin:6px 0 0}
.msg{padding:10px 12px;border-radius:8px;background:var(--card);border:1px solid var(--acc);margin:0 0 14px}
</style></head><body><main>
<h1>Weather display</h1><p class=st>%STATUS%</p>%MSG%
<form method=post action=/save>
<fieldset><legend>Location</legend>
<div class=r><label><input type=radio name=loc value=0 %L0%>Automatic</label><label><input type=radio name=loc value=1 %L1%>City</label><label><input type=radio name=loc value=2 %L2%>Coordinates</label></div>
<label for=city>City name</label><input type=text id=city name=city value="%CITY%" maxlength=47 placeholder="e.g. Ankara">
<p class=hint>Automatic guesses your location from your internet connection. With Coordinates, the city name is only the label on screen.</p>
<div class=row><div><label for=lat>Latitude</label><input type=number id=lat name=lat step=any min=-90 max=90 value="%LAT%"></div>
<div><label for=lon>Longitude</label><input type=number id=lon name=lon step=any min=-180 max=180 value="%LON%"></div></div>
</fieldset>
<fieldset><legend>Display</legend>
<div class=r><label><input type=radio name=units value=c %UC%>&deg;C, km/h</label><label><input type=radio name=units value=f %UF%>&deg;F, mph</label></div>
<label for=refresh>Refresh every (minutes)</label><input type=number id=refresh name=refresh min=5 max=180 value="%REFRESH%">
</fieldset>
<fieldset><legend>Wi-Fi</legend>
<label for=ssid>Network name</label><input type=text id=ssid name=ssid list=nets value="%SSID%" maxlength=32 autocapitalize=off autocorrect=off><datalist id=nets>%NETS%</datalist>
<label for=user>Username</label><input type=text id=user name=user value="%USER%" maxlength=63 autocapitalize=off autocorrect=off>
<p class=hint>Only for eduroam or work networks. Leave empty for home Wi-Fi.</p>
<label for=pass>Password</label><input type=password id=pass name=pass maxlength=63 placeholder="%PASSPH%">
<div class=r><label><input type=checkbox name=open value=1>This network has no password</label></div>
<p class=hint>2.4 GHz networks only. Changing Wi-Fi restarts the display.</p>
</fieldset>
<button>Save</button></form>
<form method=post action=/reset onsubmit="return confirm('Reset all settings to the defaults in config.h?')"><button class=sec>Reset to defaults</button></form>
</main></body></html>)HTML";

static String esc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

static bool viaSetupNetwork() { return apOn && server.client().localIP() == WiFi.softAPIP(); }

static void handleRoot() {
  lastActivity = millis();
  String m = server.arg("m"), msg;
  if (m == "saved")
    msg = viaSetupNetwork() ? "Saved. The setup network closes in a few seconds; your phone will go back to its usual Wi-Fi."
                            : "Saved. The display is updating.";
  else if (m == "wifi")   msg = "Saved. The display is restarting to join the new network.";
  else if (m == "reset")  msg = "Settings reset to the defaults.";
  else if (m == "coords") msg = "Not saved: enter a latitude (-90 to 90) and longitude (-180 to 180).";
  else if (m == "city")   msg = "Not saved: enter a city name.";
  else if (m == "ssid")   msg = "Not saved: enter a Wi-Fi network name.";

  String p = FPSTR(PAGE);
  p.replace("%STATUS%", esc(portalStatus()));
  p.replace("%MSG%", msg.length() ? "<p class=msg>" + esc(msg) + "</p>" : "");
  p.replace("%L0%", cfg.locMode == LOC_AUTO ? "checked" : "");
  p.replace("%L1%", cfg.locMode == LOC_CITY ? "checked" : "");
  p.replace("%L2%", cfg.locMode == LOC_COORDS ? "checked" : "");
  p.replace("%CITY%", esc(cfg.city));
  bool haveCoords = cfg.lat != 0 || cfg.lon != 0;
  p.replace("%LAT%", haveCoords ? String(cfg.lat, 4) : "");
  p.replace("%LON%", haveCoords ? String(cfg.lon, 4) : "");
  p.replace("%UC%", cfg.fahrenheit ? "" : "checked");
  p.replace("%UF%", cfg.fahrenheit ? "checked" : "");
  p.replace("%REFRESH%", String(cfg.refreshMin));
  p.replace("%SSID%", esc(cfg.ssid));
  p.replace("%NETS%", networkOptions);
  p.replace("%USER%", esc(cfg.user));
  p.replace("%PASSPH%", strlen(cfg.pass) ? "Saved - leave empty to keep it" : "");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", p);
}

static void redirect(const char *m) {
  server.sendHeader("Location", String("/?m=") + m);
  server.send(303, "text/plain", "");
}

static void handleSave() {
  lastActivity = millis();
  Settings old = cfg;

  String ssid = server.arg("ssid"), user = server.arg("user"), city = server.arg("city");
  ssid.trim(); user.trim(); city.trim();
  uint8_t loc = constrain(server.arg("loc").toInt(), 0, 2);
  float lat = server.arg("lat").toFloat(), lon = server.arg("lon").toFloat();

  if (ssid.isEmpty()) return redirect("ssid");
  if (loc == LOC_CITY && city.isEmpty()) return redirect("city");
  if (loc == LOC_COORDS && (fabsf(lat) > 90 || fabsf(lon) > 180 || (lat == 0 && lon == 0) ||
                            server.arg("lat").isEmpty() || server.arg("lon").isEmpty()))
    return redirect("coords");

  strlcpy(cfg.ssid, ssid.c_str(), sizeof cfg.ssid);
  strlcpy(cfg.user, user.c_str(), sizeof cfg.user);
  if (server.hasArg("open")) cfg.pass[0] = 0;
  else if (server.arg("pass").length()) strlcpy(cfg.pass, server.arg("pass").c_str(), sizeof cfg.pass);
  cfg.locMode = loc;
  strlcpy(cfg.city, city.c_str(), sizeof cfg.city);
  if (loc == LOC_COORDS) { cfg.lat = lat; cfg.lon = lon; }
  cfg.fahrenheit = server.arg("units") == "f";
  cfg.refreshMin = constrain(server.arg("refresh").toInt(), 5, 180);
  settingsSave();

  changedWifi = strcmp(old.ssid, cfg.ssid) || strcmp(old.user, cfg.user) || strcmp(old.pass, cfg.pass);
  changed = true;
  redirect(changedWifi ? "wifi" : "saved");
}

static void handleReset() {
  lastActivity = millis();
  Settings old = cfg;
  settingsReset();
  changedWifi = strcmp(old.ssid, cfg.ssid) || strcmp(old.user, cfg.user) || strcmp(old.pass, cfg.pass);
  changed = true;
  redirect(changedWifi ? "wifi" : "reset");
}

static void handleNotFound() {
  // On the setup network, send every unknown URL (phones' "is there internet?"
  // checks) to the settings page so it pops up automatically.
  if (apOn) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void portalBegin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apName, sizeof apName, "WeatherDisplay-%02X%02X", mac[4], mac[5]);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

void portalLoop() {
  if (apOn) dns.processNextRequest();
  server.handleClient();
  if (apOn && WiFi.softAPgetStationNum() > 0) lastActivity = millis();
}

void portalStartAP() {
  if (apOn) return;
  // A failing connection attempt makes the radio hop channels, which would
  // keep kicking phones off the setup network.
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();

  // List nearby networks for the Wi-Fi name field (takes ~2 s).
  networkOptions = "";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() && networkOptions.indexOf("\"" + esc(s) + "\"") < 0)
      networkOptions += "<option value=\"" + esc(s) + "\">";
  }
  WiFi.scanDelete();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName, cfg.apPass);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  lastActivity = millis();
  Serial.printf("[portal] setup network \"%s\" password \"%s\" -> http://%s\n",
                apName, cfg.apPass, WiFi.softAPIP().toString().c_str());
}

void portalStopAP() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  Serial.println("[portal] setup network closed");
}

const char *portalAPName() { return apName; }
unsigned long portalIdleMs() { return millis() - lastActivity; }

bool portalTakeChanges(bool &wifiChanged) {
  if (!changed) return false;
  wifiChanged = changedWifi;
  changed = changedWifi = false;
  return true;
}
