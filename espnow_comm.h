// =============================================================================
//  Phase 4 — F6 — Squelette ESP-NOW (DÉSACTIVÉ par défaut)
//
//  Contrainte absolue (SPEC §0):
//    * ESPNOW_ENABLED est 0 dans net_config.h : tout le corps du .ino est
//      compilé en #if ESPNOW_ENABLED, donc inactif et sans impact runtime.
//    * L'ESP-NOW et l'AP WiFi PARTAGENT LE MÊME CANAL RADIO (canal 1, fixé
//      par WIFI_CHANNEL dans net_config.h). Tout un ESP-NOW doit être sur ce
//      canal pour communiquer avec l'AP — ne pas changer le canal ailleurs.
//    * Taille max du payload ESP-NOW : 250 octets. Le struct ci-dessous
//      (6 octets) est très en deçà de cette limite.
//
//  API publique (uniquement active si ESPNOW_ENABLED == 1) :
//    void setupEspNow()                                                      — init + callback réception
//    bool espNowSendMidi(uint8_t channel, uint8_t note, uint8_t velocity,
//                        bool noteOn)                                        — envoi d'une note
//
//  NOTE : espnow_comm.<a> compile avant midi_handler.<a> dans l'ordere
//  alphabetique des .ino (e < m). Les declarations anticipatees de
//  handleNoteOn/handleNoteOff (ici-meme, comme dans gpio_triggers.<a>)
//  sont donc obligatoires pour que le callback de reception compile.
// =============================================================================
#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <Arduino.h>

// Magic 2 octets en tête du payload : signature des trames MIDI ESP-NOW.
static const uint8_t ESPNOW_MIDI_MAGIC[2] = { 0xA1, 0xD2 };

// Payload MIDI passe par ESP-NOW. 6 octets sur 250 max — marge quasi totale.
typedef struct {
  uint8_t magic[2];   // = ESPNOW_MIDI_MAGIC
  uint8_t channel;    // canal MIDI : 1, 2 ou 10
  uint8_t note;       // 0..127
  uint8_t velocity;   // 0..127
  uint8_t noteOn;     // 1 = NoteOn, 0 = NoteOff
} EspNowMidiMsg;

void setupEspNow();
bool espNowSendMidi(uint8_t channel, uint8_t note, uint8_t velocity, bool noteOn);

#endif // ESPNOW_COMM_H