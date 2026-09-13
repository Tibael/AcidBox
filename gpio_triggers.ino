// =============================================================================
//  Phase 2 — F4 — 4 triggers GPIO -> notes MIDI internes (persistant)
//
//  Contrainte absolue (SPEC §0):
//    * fireTrigger()/releaseTrigger() sont appelles HORS hot path (tache web,
//      loop(), code utilisateur de lecture GPIO) — JAMAIS depuis une ISR ni
//      depuis le chemin audio.
//    * La lecture GPIO elle-meme est laissee vide : l'utilisateur branche son
//      propre code dans readTriggerGPIOs() (pinMode/edge-detect a l'exterieur).
//      Ce sont son code et la page web qui appellent fireTrigger()/releaseTrigger().
//    * Persistance LittleFS /config/triggers.json : ecriture unicquement sur
//      changement via l'API web, rechargement au boot (loadTriggers()).
//      Si LittleFS n'est pas monte, on skip silencieusement.
//
//  API publique :
//    void  loadTriggers()              — recharger la config au boot
//    bool  saveTriggers()              — ecrire /config/triggers.json
//    void  fireTrigger(uint8_t id)     — NoteOn interne (+auto-release precedents)
//    void  releaseTrigger(uint8_t id)  — NoteOff de la note tenue
//    void  readTriggerGPIOs()          — STUB a remplir (code utilisateur)
//
//  NOTE : Arduino compile les .ino dans l'ordre alphabetique : acidbox <
//  gpio_triggers < midi_handler < web_server. handleNoteOn/handleNoteOff sont
//  defines (sans 'inline') dans midi_handler.ino donc ET sauvegardes dans le
//  header genere ET prototypisees ci-dessous par securite (double declaration
//  non inline = OK du moment que les signatures match).
// =============================================================================

#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
const int hallPins[4] = {SENSOR1_PIN, SENSOR2_PIN, SENSOR3_PIN, SENSOR4_PIN};
const int nbHallSensors = sizeof(hallPins)/sizeof(hallPins[0]);
int readValues[nbHallSensors] = {1 * nbHallSensors};
int _readValues[nbHallSensors] = {1 * nbHallSensors};

// declarations anticipatees (midi_handler.ino, plus loin dans la compilation)
void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);

// ---------------------------------------------------------------------- config
struct TriggerConfig {
  uint8_t channel;   // 1, 2 ou 10
  uint8_t note;      // 0..127
  uint8_t velocity;  // 0..127
};

// Table des 4 triggers — defini ici (la sous-unit .ino), extern'd par web_server.ino.
TriggerConfig triggers[4] = {
  { 10, 36, 127 },   // 0 — kick
  { 10, 38, 127 },   // 1 — snare
  { 10, 42, 127 },   // 2 — low toms
  { 10, 46, 127 },   // 3 — hi-hat
};

// note en cours par trigger — pour auto-release a la re-arm.
// bool separe: la note MIDI 0 (C-1) est valide, on ne peut pas l'utiliser
// comme sentinelle.
static uint8_t held_note[4] = { 0, 0, 0, 0 };
static bool    held_active[4] = { false, false, false, false };

void initGPIO(){
  for (int x=0; x < nbHallSensors; x++){
    pinMode(hallPins[x], INPUT_PULLUP);
  }
}

// ------------------------------------------------------------------------ fire
// A appeler HORS hot path (tache web / loop / code GPIO utilisateur).
// Relance par re-arm : si le trigger etait deja tenu, on fait NoteOff avant
// NoteOn, puis on memoise la nouvelle note tenue.
void fireTrigger(uint8_t id) {
  if (id >= 4) return;
  if (held_active[id]) {
    handleNoteOff(triggers[id].channel, held_note[id], 0);
    held_active[id] = false;
  }
  handleNoteOn(triggers[id].channel, triggers[id].note, triggers[id].velocity);
  held_note[id]   = triggers[id].note;
  held_active[id] = true;
}

void releaseTrigger(uint8_t id) {
  if (id >= 4) return;
  if (held_active[id]) {
    handleNoteOff(triggers[id].channel, held_note[id], 0);
    held_active[id] = false;
  }
}

// ------------------------------------------------------------------------ GPIO
// STUB — le code de lecture GPIO (pinMode, edge-detect, rebond, etc.) vit cote
// utilisateur. A remplir dans la boucle utilisateur : lire chaque pin, sur
// front montant appeler fireTrigger(i), sur front descendant releaseTrigger(i).
// N'AJOUTER RIEN dans le chemin audio (mixer / *_generate / Process / getSample).
void readTriggerGPIOs() {
  for (int x=0; x < nbHallSensors; x++){
    readValues[x] = digitalRead(hallPins[x]);
    if (readValues[x] != _readValues[x]){
      if (!readValues[x])
      {
        fireTrigger(x);
      }
      _readValues[x]=readValues[x];
    }
  }
}

// ---------------------------------------------------------------- persistance
static const char *kTriggersPath = "/config/triggers.json";

bool saveTriggers() {
  // garder-fou : LittleFS commence par Drums.Init() (sampler.ino), qui precede
  // setupWebServer() dans setup() — si jamais non monte, open() echouera et on
  // retournera false sans ecrire.
  if (!LittleFS.exists("/")) return false;
  if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");

  StaticJsonDocument<256> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject o = arr.createNestedObject();
    o["id"]       = i;
    o["channel"]  = triggers[i].channel;
    o["note"]     = triggers[i].note;
    o["velocity"] = triggers[i].velocity;
  }
  File f = LittleFS.open(kTriggersPath, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  DEBF("Triggers: saved %d bytes -> %s\n", (int)n, kTriggersPath);
  return n > 0;
}

void loadTriggers() {
  if (!LittleFS.exists("/")) return;  // silencieux : pas de FS = defaults
  if (!LittleFS.exists(kTriggersPath)) return;

  File f = LittleFS.open(kTriggersPath, "r");
  if (!f) return;
  size_t size = f.size();
  if (size == 0 || size > 512) { f.close(); return; }
  uint8_t buf[512];
  if (f.read(buf, size) != size) { f.close(); return; }
  f.close();

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, buf, size)) return;  // parse fail -> garde defaults
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() != 4) return;
  for (int i = 0; i < 4; i++) {
    const JsonObject o = arr[i].as<JsonObject>();
    uint16_t c  = o["channel"];
    uint16_t n  = o["note"];
    uint16_t v  = o["velocity"];
    if ( (c == 1 || c == 2 || c == 10) && n < 128 && v < 128 ) {
      triggers[i].channel  = (uint8_t)c;
      triggers[i].note     = (uint8_t)n;
      triggers[i].velocity = (uint8_t)v;
    }
  }
  DEBF("Triggers: loaded from %s\n", kTriggersPath);
}