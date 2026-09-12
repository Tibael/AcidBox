// =============================================================================
//  Phase 4 — F6 — Squelette ESP-NOW (DÉSACTIVÉ par défaut)
//
//  Contrainte absolue (SPEC §0):
//    * Tout le corps de ce fichier est compilé dans #if ESPNOW_ENABLED.
//      ESPNOW_ENABLED vaut 0 dans net_config.h -> fichier inactif, zéro
//      impact runtime, aucun code ESP-NOW émis par le compileur.
//    * Canal radio PARTAGÉ : l'ESP-NOW doit tourner sur le même canal que
//      l'AP WiFi (canal 1, WIFI_CHANNEL dans net_config.h). setupEspNow()
//      est appelé APRÈS setupWebServer() (qui fait déjà WiFi.mode(WIFI_AP)),
//      donc on ne touche plus au mode WiFi — on hérite juste du canal.
//    * Payload ESP-NOW : max 250 octets. EspNowMidiMsg fait 6 octets.
//    * Callback de réception : fait que des opérations légères (memcpy +
//      check magie + appel handleNoteOn/Off qui réutilise le chemin MIDI
//      existant). JAMAIS de mutex bloquant, JAMAIS dans le chemin audio.
//
//  Activer (quand la comm 2 ESP32 sera réellement nécessaire) :
//    * Mettre #define ESPNOW_ENABLED 1 dans net_config.h.
//    * Déléguer l'adresse MAC du pair à esp_now_add_peer() ci-dessous.
// =============================================================================

#include "config.h"
#include "net_config.h"
#include "espnow_comm.h"

#if ESPNOW_ENABLED

#include <WiFi.h>
#include <esp_now.h>

// Déclarations anticipées (midi_handler.ino, plus loin dans la compilation
// alphabetique) — memes declarations que dans gpio_triggers.ino.
void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);

// ------------------------------------------------------------------- réception
// Callback ESP-NOW : appele par le stack WiFi sur Core 1 (config_wifi task),
// HORS du chemin audio. Operations legeres uniquement :
//   1. verifier la taille (le payload doit tenir dans EspNowMidiMsg),
//   2. verifier la magie (ESPNOW_MIDI_MAGIC),
//   3. appeler handleNoteOn / handleNoteOff selon le flag noteOn — le chemin
//      MIDI existant prend le relais (aucune latence audio ajoutée).
static void espNowOnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data) {
  (void)recv_info; // MAC émetteur — pas encore utilisé (le peer sera délégué)
  if (data == NULL) return;
  const size_t len = sizeof(EspNowMidiMsg);
  EspNowMidiMsg msg;
  // la taille effective de la trame ESP-NOW n'est pas exposée par le callback
  // (data point sur un buffer du stack) : on fait confiance a la magie + aux
  // valeurs plausibles pour filtrer.
  memcpy(&msg, data, len);
  if (msg.magic[0] != ESPNOW_MIDI_MAGIC[0] || msg.magic[1] != ESPNOW_MIDI_MAGIC[1]) {
    return; // pas une trame MIDI AcidBox — on ignore silencieusement
  }
  if (msg.noteOn) {
    handleNoteOn(msg.channel, msg.note, msg.velocity);
  } else {
    handleNoteOff(msg.channel, msg.note, msg.velocity);
  }
}

// ----------------------------------------------------------------------- init
void setupEspNow() {
  // WiFi.mode(WIFI_AP) est deja fait par setupWebServer() (initWifiAP) — on
  // ne le refait pas. Le canal radio est herite de WIFI_CHANNEL (canal 1).
  // A ce point le stack WiFi est actif, esp_now_init() peut etre appele.

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    DEBF("ESP-NOW init failed: %d\n", (int)err);
    return;
  }

  err = esp_now_register_recv_cb(espNowOnDataRecv);
  if (err != ESP_OK) {
    DEBF("ESP-NOW recv cb failed: %d\n", (int)err);
    return;
  }

  // Peer : ajouter l'adresse MAC du pair distant. A decommenter/remplir lors de
  // l'activation reelle (ex. l'autre ESP32 de la config a 2 boitiers).
  //
  // esp_now_peer_info_t peer = {
  //   .peer_addr = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
  //   .channel   = WIFI_CHANNEL,      // même canal que l'AP (contrainte F1/F6)
  //   .ifkey     = {0},               // clé de chiffrement optionnelle (16o)
  //   .encrypt   = false,
  //   .lsid      = 0,
  // };
  // esp_now_add_peer(&peer);

  DEBF("ESP-NOW: ready (chan=%d)\n", WIFI_CHANNEL);
}

// ------------------------------------------------------------------------ envoi
bool espNowSendMidi(uint8_t channel, uint8_t note, uint8_t velocity, bool noteOn) {
  EspNowMidiMsg msg;
  msg.magic[0]    = ESPNOW_MIDI_MAGIC[0];
  msg.magic[1]    = ESPNOW_MIDI_MAGIC[1];
  msg.channel     = channel;
  msg.note        = note;
  msg.velocity    = velocity;
  msg.noteOn      = noteOn ? 1 : 0;

  const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_err_t err = esp_now_send((uint8_t *)bcast, (uint8_t *)&msg, sizeof(msg));
  if (err != ESP_OK) {
    DEBF("ESP-NOW send failed: %d\n", (int)err);
    return false;
  }
  return true;
}

#else // ESPNOW_ENABLED == 0 — stubs pour que les references compilent

void setupEspNow() {}
bool espNowSendMidi(uint8_t, uint8_t, uint8_t, bool) { return false; }

#endif // ESPNOW_ENABLED