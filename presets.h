// =============================================================================
//  Phase C — Presets nommables (100 max) — persistence LittleFS /presets.json
//
//  Un preset = note MIDI + table CC (index = numero de CC, valeur = valeur du
//  CC, 0xFF = inutilise). Les 128 CC couvrent l'entiere plage MIDI; seules les
//  entrees != 0xFF sont serisees/chargees/rejouees.
//
//  Persistance : un seul fichier JSON (tableau de presets) dans /presets.json.
//  Ecrit HORS hot path (tache web / setup), jamais depuis le chemin audio.
//
//  NOTE : Arduino compile les .ino dans l'ordre alphabetique : presets < web_
//  server, donc les definitions de presets.ino precedent le referencement par
//  web_server.ino. Les externs + prototypes ci-dessous suffisent a la linkage.
// =============================================================================
#ifndef ACIDBOX_PRESETS_H
#define ACIDBOX_PRESETS_H

#include <stdint.h>

#define MAX_PRESETS         100

// 0xFF = CC inutilise dans ce preset (sautant dans le JSON, ignore au chargement)
#define PRESET_CC_UNUSED    0xFF

struct Preset {
  char    name[32];     // nom (31 car. + nul)
  uint8_t note;         // note MIDI 0..127
  uint8_t ccs[128];     // index = numero de CC, valeur = valeur, 0xFF = inutilise
};

extern Preset  presets[MAX_PRESETS];
extern uint8_t presetCount;

// Init LittleFS (mount si besoin) + charge les presets existants.
void  initPresets();

// Serise tout l'array presets[] [..presetCount] vers /presets.json.
bool  savePresets();

// Relit /presets.json dans presets[] et renvoie true sur reussite.
bool  loadPresets();

// Ajoute un preset (note donnee, ccs remis a 0xFF = vide) et met a jour le nom.
// Renvoie l'index du preset ajoute, ou -1 si la table est pleine.
int   addPreset(const char *name, uint8_t note);

// Supprime le preset d'index id et decale les suivants; maj presetCount.
void  deletePreset(uint8_t id);

#endif // ACIDBOX_PRESETS_H