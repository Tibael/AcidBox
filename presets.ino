// =============================================================================
//  Phase C — Presets nommables (100 max) — implementation
//
//  Persistance : tableau JSON complet dans /presets.json :
//    [ { "name": "...", "note": N, "ccs": { "7":64, ... } }, ... ]
//  ...
// =============================================================================

#include "config.h"
#include "net_config.h"
#include "presets.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Globals (exposees par presets.h) -------------------------------------------------
Preset  presets[MAX_PRESETS];
uint8_t presetCount = 0;

static const char *kPresetsPath = "/presets.json";

// Reinitialise un preset : nom vide, note C-1, tous CC a 0xFF (inutilises).
static void clearPreset(Preset *p) {
  p->name[0] = '\0';
  p->note = 0;
  for (int i = 0; i < 128; i++) p->ccs[i] = PRESET_CC_UNUSED;
}

// Init LittleFS (monte si besoin, formate au premier run via
// FORMAT_LITTLEFS_IF_FAILED) puis charge les presets existants.
void initPresets() {
  if (!LittleFS.exists("/")) LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
  for (int i = 0; i < MAX_PRESETS; i++) clearPreset(&presets[i]);
  loadPresets();
}

// Charge /presets.json dans presets[/presetCount]. Retoune true si reussi.
// Garde les actuels en memo si le fichier est absent/corrompu.
bool loadPresets() {
  if (!LittleFS.exists("/")) return false;          // pas de FS = defaults vides
  if (!LittleFS.exists(kPresetsPath)) return false;

  File f = LittleFS.open(kPresetsPath, "r");
  if (!f) return false;

  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }  // parse fail
  f.close();

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  uint16_t n = arr.size();
  if (n > MAX_PRESETS) n = MAX_PRESETS;
  presetCount = 0;
  for (uint16_t i = 0; i < n; i++) {
    const JsonObject o = arr[i].as<JsonObject>();
    Preset &p = presets[presetCount];
    clearPreset(&p);
    const char *nm = o["name"];
    if (nm) {
      size_t l = strlen(nm);
      if (l > 31) l = 31;
      memcpy(p.name, nm, l);
      p.name[l] = '\0';
    }
    p.note = (uint8_t)constrain((int16_t)o["note"], 0, 127);
    const JsonVariant ccVar = o["ccs"];
    if (ccVar.is<JsonVariant>()) {
      for (JsonPair kv : ccVar.as<JsonObject>()) {
        int cc = atoi(kv.key().c_str());
        if (cc >= 0 && cc < 128) {
          int v = (int)kv.value();
          p.ccs[cc] = (uint8_t)constrain(v, 0, 127);
        }
      }
    }
    presetCount++;
  }
  DEBF("Presets: loaded %d from %s\n", (int)presetCount, kPresetsPath);
  return true;
}

// Serise presets[] [..presetCount] vers /presets.json (JSON compact).
bool savePresets() {
  if (!LittleFS.exists("/")) return false;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < presetCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = presets[i].name;
    o["note"] = presets[i].note;
    JsonObject ccs = o.createNestedObject("ccs");
    for (int c = 0; c < 128; c++) {
      if (presets[i].ccs[c] != PRESET_CC_UNUSED) ccs[String(c)] = presets[i].ccs[c];
    }
  }

  File f = LittleFS.open(kPresetsPath, "w");
  if (!f) return false;
  size_t w = serializeJson(doc, f);
  f.close();
  DEBF("Presets: saved %d presets -> %s (%d bytes)\n",
       (int)presetCount, kPresetsPath, (int)w);
  return w > 0;
}

// Ajoute un nouveau preset (ccs remis a 0xFF) a la fin du table
// et met a jour le note + nom donne. Renvoie l'index ou -1 si plein.
int addPreset(const char *name, uint8_t note) {
  if (presetCount >= MAX_PRESETS) return -1;     // table pleine
  Preset &p = presets[presetCount];
  clearPreset(&p);                                // note=0, ccs=0xFF
  p.note = note;
  size_t l = name ? strlen(name) : 0;
  if (l > 31) l = 31;
  if (name && l) { memcpy(p.name, name, l); p.name[l] = '\0'; }
  presetCount++;
  return presetCount - 1;
}

// Supprime le preset d'index id et decale les suivants vers le bas.
void deletePreset(uint8_t id) {
  if (id >= presetCount) return;
  for (int i = id; i < presetCount - 1; i++) presets[i] = presets[i + 1];
  clearPreset(&presets[presetCount - 1]);
  presetCount--;
}