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
//    GET  /config        → config.html (Config triggers GPIO)
//    GET  /api/triggers  → [ {id,channel,note,velocity} x4 ]
//    POST /api/trigger   → { id, channel, note, velocity } – maj + persistance
//    POST /api/test      → { id } – fireTrigger(id)
// =============================================================================
#if WEB_SERVER_ENABLED

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "net_config.h"
#include "config.h"
#include "presets.h"
#include "memory.h"

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

// Phase 2 — F4 : triggers GPIO (definis dans gpio_triggers.ino).
struct TriggerConfig;
extern TriggerConfig triggers[4];
extern void  fireTrigger(uint8_t id);
extern bool  saveTriggers();

// Phase 3 — F5 : snapshot + serialisation (definis dans debug_web.ino).
// Le push WS devient au maximum toutes les 500ms (garde cote serveur).
extern void   debugSnapshotTick();
extern size_t debugSnapshotToJSON(char *buf, size_t maxlen);

// Chemin MIDI existant (defini dans midi_handler.ino) — reutilise la routage
// en memoire, sans latence ajoutee. Ordre alphab .ino : m < w, la definition
// precede ; on declara ici pour etre sur de la linkage externe standard.
extern void handleNoteOn(uint8_t inChannel, uint8_t inNote, uint8_t inVelocity);
extern void handleCC(uint8_t inChannel, uint8_t cc_number, uint8_t cc_value);

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("ws"); // endpoint /ws — defile dans server en setup

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
static void handleConfig(AsyncWebServerRequest *req) {
  serveFile(req, "/web/config.html", "text/html");
}
static void handleFilter(AsyncWebServerRequest *req) {
  serveFile(req, "/web/filter.html", "text/html");
}
static void handleMemory(AsyncWebServerRequest *req) {
  serveFile(req, "/web/memory.html", "text/html");
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
  req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown-instrument\"}");
}

// -------------------------------------------------- Phase 2 — F4 triggers GPIO
// GET /api/triggers → [ {id, channel, note, velocity}, ... x4 ]
static void handleGetTriggers(AsyncWebServerRequest *req) {
  StaticJsonDocument<256> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject o = arr.createNestedObject();
    o["id"]       = i;
    o["channel"]  = triggers[i].channel;
    o["note"]     = triggers[i].note;
    o["velocity"] = triggers[i].velocity;
  }
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// POST /api/trigger  body: { "id": 0..3, "channel": 1|2|10, "note": 0..127,
//                            "velocity": 0..127 }  → maj + saveTriggers()
static void handleSetTrigger(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id   = obj["id"];
  const uint16_t chan = obj["channel"];
  const uint16_t note = obj["note"];
  const uint16_t vel  = obj["velocity"];
  if (id >= 4 || (chan != 1 && chan != 2 && chan != 10) || note > 127 || vel > 127) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-params\"}");
    return;
  }
  triggers[id].channel  = (uint8_t)chan;
  triggers[id].note     = (uint8_t)note;
  triggers[id].velocity = (uint8_t)vel;
  const bool saved = saveTriggers();
  if (saved)
    req->send(200, "application/json", "{\"ok\":true}");
  else
    req->send(500, "application/json", "{\"ok\":false,\"error\":\"save-failed\"}");
}

// POST /api/test  body: { "id": 0..3 } → fireTrigger(id)
// Joue la note via le chemin MIDI existant (zéro latence audio).
// Exécution depuis l'event-loop serveur (Core 1, idle) — pas dans le hot path.
static void handleTestTrigger(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id = obj["id"];
  if (id >= 4) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-id\"}");
    return;
  }
  fireTrigger((uint8_t)id);
  req->send(200, "application/json", "{\"ok\":true}");
}

// POST /api/pad   body: { "note": 0..127, "velocity": 0..127 (opt, def 127) }
// Déclenche un pad drums via le chemin MIDI existant (DRUM_MIDI_CHAN).
// Exécution depuis l'event-loop serveur (Core 1, idle) — pas dans le hot path.
static void handlePad(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  if (!obj.containsKey("note")) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-params\"}");
    return;
  }
  const uint16_t note = obj["note"];
  const uint16_t vel  = (obj.containsKey("velocity") ? (uint16_t)obj["velocity"] : 127);
  if (note > 127 || vel > 127) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-params\"}");
    return;
  }
  handleNoteOn(DRUM_MIDI_CHAN, (uint8_t)note, (uint8_t)vel);
  req->send(200, "application/json", "{\"ok\":true}");
}

// POST /api/cc   body: { "channel": 1|2|10, "cc": 0..119, "value": 0..127 }
// Envoie un control change via le chemin MIDI existant (handleCC), qui
// re-route vers Drums/Synth1/Synth2.ParseCC selon le channel.
// CC 120-127 reservationnee (system) — exclue ici par validation.
// Exécution depuis l'event-loop serveur (Core 1, idle) — pas dans le hot path.
static void handleCC_web(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  if (!obj.containsKey("channel") || !obj.containsKey("cc") || !obj.containsKey("value")) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-params\"}");
    return;
  }
  const uint16_t chan = obj["channel"];
  const uint16_t cc   = obj["cc"];
  const uint16_t val  = obj["value"];
  if ((chan != 1 && chan != 2 && chan != 10) || cc > 119 || val > 127) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-params\"}");
    return;
  }
  handleCC((uint8_t)chan, (uint8_t)cc, (uint8_t)val);
  req->send(200, "application/json", "{\"ok\":true}");
}

// ----------------------------------------------- Phase C — Presets
// GET  /api/preset/list  → { count: N, presets: [{id,name}] }
static void handlePresetList(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["count"] = presetCount;
  JsonArray arr = doc.createNestedArray("presets");
  presetCount = presetCount > MAX_PRESETS ? MAX_PRESETS : presetCount;
  for (uint8_t i = 0; i < presetCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]   = i;
    o["name"] = presets[i].name;
  }
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// POST /api/preset/save  { name, note, ccs?:{ "7":64, ... } } → { id, ok:true }
static void handlePresetSave(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const char *name = obj["name"] | "";
  const uint16_t note = obj["note"] | 36;
  if (note > 127) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-note\"}");
    return;
  }
  int idx = addPreset(name, (uint8_t)note);
  if (idx < 0) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"full\"}");
    return;
  }
  const JsonVariant ccVar = obj["ccs"];
  if (ccVar.is<JsonObject>()) {
    for (JsonPair kv : ccVar.as<JsonObject>()) {
      int cc = atoi(kv.key().c_str());
      if (cc >= 0 && cc < 128) {
        int v = (int)kv.value();
        presets[idx].ccs[cc] = (uint8_t)constrain(v, 0, 127);
      }
    }
  }
  savePresets();
  String out;
  out.reserve(48);
  out += "{\"ok\":true,\"id\":";
  out += idx;
  out += "}";
  req->send(200, "application/json", out);
}

// POST /api/preset/load  { id } → { note, ccs:{ "7":64, ... } }
static void handlePresetLoad(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id = obj["id"];
  if (id >= presetCount) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-id\"}");
    return;
  }
  Preset &p = presets[id];
  JsonDocument doc;
  doc["id"]   = id;
  doc["name"] = p.name;
  doc["note"] = p.note;
  JsonObject ccs = doc.createNestedObject("ccs");
  for (int c = 0; c < 128; c++) {
    if (p.ccs[c] != PRESET_CC_UNUSED) {
      ccs[String(c)] = p.ccs[c];
    }
  }
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// POST /api/preset/del  { id } → { ok:true }
static void handlePresetDel(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id = obj["id"];
  if (id >= presetCount) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-id\"}");
    return;
  }
  deletePreset((uint8_t)id);
  savePresets();
  req->send(200, "application/json", "{\"ok\":true}");
}

// POST /api/preset/update-cc  { id, cc, value } → sauvegarde dynamique
static void handlePresetUpdateCC(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id    = obj["id"];
  const uint16_t cc    = obj["cc"];
  const uint16_t value = obj["value"];
  if (id >= presetCount || cc > 127 || value > 127) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-params\"}");
    return;
  }
  presets[id].ccs[(uint8_t)cc] = (uint8_t)value;
  savePresets();
  req->send(200, "application/json", "{\"ok\":true}");
}

// ----------------------------------------------- Phase E — Memories
// GET  /api/memory/list  → { count, memories: [{id,name}] }
static void handleMemList(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["count"] = memCount;
  JsonArray arr = doc.createNestedArray("memories");
  for (uint8_t i = 0; i < memCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]   = i;
    o["name"] = memories[i].name;
  }
  String out; serializeJson(doc, out);
  req->send(200, "application/json", out);
}
// POST /api/memory/save  { name } → { id, ok:true }
static void handleMemSave(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const char *name = obj["name"] | "Memory";
  int idx = addMemory(name);
  if (idx < 0) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"full\"}"); return; }
  // Store global CCs
  const JsonVariant ccVar = obj["ccs"];
  if (ccVar.is<JsonObject>()) {
    for (JsonPair kv : ccVar.as<JsonObject>()) {
      int cc = atoi(kv.key().c_str());
      if (cc >= 0 && cc < 128)
        memories[idx].globalCCs[cc] = (uint8_t)constrain((int)kv.value(), 0, 127);
    }
  }
  saveMemories();
  String out = "{\"ok\":true,\"id\":"; out += idx; out += "}";
  req->send(200, "application/json", out);
}
// POST /api/memory/load  { id } → { ok:true }
static void handleMemLoad(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id = obj["id"];
  if (id >= memCount) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-id\"}"); return; }
  loadMemory((uint8_t)id);
  // Return CCs so frontend can update localStorage/sliders
  Memory &m = memories[id];
  JsonDocument doc;
  doc["ok"] = true;
  doc["id"] = id;
  JsonObject ccs = doc.createNestedObject("ccs");
  for (int c = 0; c < 128; c++) {
    if (m.globalCCs[c] != MEM_CC_UNUSED)
      ccs[String(c)] = m.globalCCs[c];
  }
  String out; serializeJson(doc, out);
  req->send(200, "application/json", out);
}
// POST /api/memory/del  { id } → { ok:true }
static void handleMemDel(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject obj = json.as<JsonObject>();
  const uint16_t id = obj["id"];
  if (id >= memCount) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad-id\"}"); return; }
  delMemory((uint8_t)id);
  saveMemories();
  req->send(200, "application/json", "{\"ok\":true}");
}

// --------------------------------------------- Phase 3 — F5 — WebSocket /ws
// Pousse le snapshot au client. Garde cote serveur : au maximum 1 push / 500ms.
// Le push est fait depuis l'event loop du serveur (Core 1, priorite idle),
// JAMAIS depuis le contexte audio.
// Exporte (non-static) pour etre appelee depuis loop() — AcidBox.ino l'appelle
// sous #ifdef WEB_SERVER_ENABLED, sinon un stub local la satisfait.
void wsPush() {
  static uint32_t last_push = 0;
  if (ws.count() == 0) return; // pas de client = aucun travail
  const uint32_t now = millis();
  if (now - last_push < 500) return;
  last_push = now;
  char buf[220];
  const size_t n = debugSnapshotToJSON(buf, sizeof(buf));
  if (n > 0) ws.textAll(buf, n);
}

// Callbacks WebSocket — appeles depuis l'event loop du serveur (Core 1, idle).
static void wsOnConnect(AsyncWebSocket * /*ws*/, AsyncWebSocketClient *client) {
  // Prefixe constant (le snapshot JSON vary) : pre-fabriquer une fois, reutiliser
  // a chaque connexion -> zero String allocation par client.
  static const char *prefix = "{\"snap\":";
  char buf[220];
  const size_t n = debugSnapshotToJSON(buf, sizeof(buf));
  if (n > 0) {
    char hdr[256];
    const int hlen = snprintf(hdr, sizeof(hdr), "%s%.*s}", prefix, (int)n, buf);
    if (hlen > 0) client->text(hdr, hlen);
  }
}

static void wsOnDisconnect(AsyncWebSocket * /*ws*/, AsyncWebSocketClient * /*client*/, uint8_t /*code*/) {
  // Rien a faire : le prochain wsPush() verra ws.count() == 0.
}

// Dans le fork ESP32Async, les events WebSocket passent par onEvent() ;
// onConnect/onDisconnect ne sont pas des methodes separees.
static void wsOnEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) wsOnConnect(server, client);
  else if (type == WS_EVT_DISCONNECT) wsOnDisconnect(server, client, 0);
}

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
  server.on("/",             HTTP_GET,  handleIndex);
  server.on("/app.js",        HTTP_GET,  handleAppJs);
  server.on("/style.css",     HTTP_GET,  handleStyleCss);
  server.on("/config",        HTTP_GET,  handleConfig);
  server.on("/filter",        HTTP_GET,  handleFilter);
  server.on("/memory",        HTTP_GET,  handleMemory);
  server.on("/api/state",     HTTP_GET,  handleState);
  server.on("/api/triggers",  HTTP_GET,  handleGetTriggers);
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/mute",    handleMute));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/solo",    handleSolo));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/trigger", handleSetTrigger));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/test",    handleTestTrigger));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/pad",     handlePad));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/cc",      handleCC_web));
  server.on("/api/preset/list", HTTP_GET,  handlePresetList);
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/preset/save",      handlePresetSave));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/preset/load",      handlePresetLoad));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/preset/del",       handlePresetDel));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/preset/update-cc", handlePresetUpdateCC));
  server.on("/api/memory/list", HTTP_GET,  handleMemList);
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/memory/save",      handleMemSave));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/memory/load",      handleMemLoad));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/memory/del",       handleMemDel));
  // Phases C+E — init LittleFS
  initPresets();
  initMemories();
  ws.onEvent(wsOnEvent);
  server.addHandler(&ws);
  server.begin();
  DEBF("WebServer: port=%d ws=\/ws\n", WEB_SERVER_PORT);
}

#else
void setupWebServer() {}
void wsPush() {}
#endif