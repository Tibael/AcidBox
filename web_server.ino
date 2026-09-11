// =============================================================================
//  Phase 1 — Interface web minimale (ESPAsyncWebServer, Core 1, priorite idle)
//
//  Contrainte absolue (SPEC §0):
//    * Aucune task dans le chemin audio. Le serveur est asynchrone (ESPAsyncWebServer)
//      et tourne sur son event loop, epinglé sur Core 1 a priorite 0 (idle).
//    * La communication web <-> audio utilise UNIQUEMENT les flags atomiques:
//        volatile bool audible_synth1/2/drums  (ecrits icis, lus par mixer())
//      Sans mutex, sans lock bloquant.
//    * Le core 0 (et audio_task1) n'y est JAMAIS touche.
//
//  Routes:
//    GET  /              → index.html (LittleFS /web/)
//    GET  /app.js        → app.js
//    GET  /style.css     → style.css
//    GET  /api/state     → { synth1:{mute,solo,audible}, synth2:{...}, drums:{...} }
//    POST /api/mute      → { instrument, muted }
//    POST /api/solo      → { instrument, solo }
// =============================================================================
#ifdef WEB_SERVER_ENABLED

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "net_config.h"
#include "config.h"

extern SynthVoice Synth1, Synth2;
extern Sampler Drums;

// Etat mute/solo — definis dans AcidBox.ino (globaux, ecrits ici, lus par
// recalc_audible()). Externs uniquement : ne PAS stater des copies locales.
extern bool mute_synth1, mute_synth2, mute_drums;
extern bool solo_synth1, solo_synth2, solo_drums;

// Flags atomiques lus par mixer() — declares dans AcidBox.ino, visibles ici.
extern volatile bool audible_synth1, audible_synth2, audible_drums;

// Recalcul des flags audibles — defini dans AcidBox.ino (C++, pas extern "C").
extern void recalc_audible();

static AsyncWebServer server(WEB_SERVER_PORT);

// ---------------------------------------------------------------- JSON helpers
static void sendState(AsyncWebServerRequest *req) {
  StaticJsonDocument<384> doc;
  JsonObject s1  = doc.createNestedObject("synth1");
  JsonObject s2  = doc.createNestedObject("synth2");
  JsonObject d   = doc.createNestedObject("drums");

  s1["mute"]    = mute_synth1;
  s1["solo"]    = solo_synth1;
  s1["audible"] = audible_synth1;

  s2["mute"]    = mute_synth2;
  s2["solo"]    = solo_synth2;
  s2["audible"] = audible_synth2;

  d["mute"]     = mute_drums;
  d["solo"]     = solo_drums;
  d["audible"]  = audible_drums;

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// -------------------------------------------------------- static file serving
static void serveFile(AsyncWebServerRequest *req, const char *path, const char *mime) {
  if (!LittleFS.exists(path)) {
    req->send(404, "text/plain", String("Not found: ") + path);
    return;
  }
  AsyncWebServerResponse *resp = req->beginResponse(LittleFS, path, mime);
  req->send(resp);
}

static void handleIndex(AsyncWebServerRequest *req) {
  serveFile(req, "/web/index.html", "text/html");
}
static void handleAppJs(AsyncWebServerRequest *req) {
  serveFile(req, "/web/app.js", "application/javascript");
}
static void handleStyleCss(AsyncWebServerRequest *req) {
  serveFile(req, "/web/style.css", "text/css");
}

// --------------------------------------------------------------- REST handlers
static void handleState(AsyncWebServerRequest *req) {
  sendState(req);
}

// POST /api/mute   body: { "instrument": "synth1"|"synth2"|"drums", "muted": bool }
static void handleMute(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const char *inst = obj["instrument"];
  if (!inst) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"no-instrument\"}");
    return;
  }
  const bool muted = obj["muted"];
  bool ok = false;
  if      ( strcmp(inst, "synth1") == 0 ) { mute_synth1 = muted; ok = true; }
  else if ( strcmp(inst, "synth2") == 0 ) { mute_synth2 = muted; ok = true; }
  else if ( strcmp(inst, "drums")  == 0 ) { mute_drums  = muted; ok = true; }
  recalc_audible();
  if (ok)
    req->send(200, "application/json", "{\"ok\":true}");
  else
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown-instrument\"}");
}

// POST /api/solo   body: { "instrument": "synth1"|"synth2"|"drums", "solo": bool }
static void handleSolo(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const char *inst = obj["instrument"];
  if (!inst) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"no-instrument\"}");
    return;
  }
  const bool solo = obj["solo"];
  bool ok = false;
  if      ( strcmp(inst, "synth1") == 0 ) { solo_synth1 = solo; ok = true; }
  else if ( strcmp(inst, "synth2") == 0 ) { solo_synth2 = solo; ok = true; }
  else if ( strcmp(inst, "drums")  == 0 ) { solo_drums  = solo; ok = true; }
  recalc_audible();
  if (ok)
    req->send(200, "application/json", "{\"ok\":true}");
  else
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown-instrument\"}");
}

// ------------------------------------------------------------------ AP + task
static void initWifiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                    IPAddress(255,255,255,0));
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  DEBF("WiFi AP: SSID=%s chan=%d IP=%s\n",
       WIFI_SSID, WIFI_CHANNEL, WiFi.softAPIP().toString().c_str());
}

void setupWebServer() {
  initWifiAP();
  server.on("/",          HTTP_GET,  handleIndex);
  server.on("/app.js",     HTTP_GET,  handleAppJs);
  server.on("/style.css",  HTTP_GET,  handleStyleCss);
  server.on("/api/state",  HTTP_GET,  handleState);
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/mute", handleMute));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/solo", handleSolo));
  server.begin();
  DEBF("WebServer: port=%d\n", WEB_SERVER_PORT);
}

#else
void setupWebServer() {}
#endif