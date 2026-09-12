// =============================================================================
//  Phase 3 — F5 — Debug web : snapshot 500ms + WebSocket push
//
//  Contrainte absolue (SPEC §0):
//    * Le snapshot est ecrit HORS hot path : regular_checks() (loop, Core 1,
//      idle) avec un garde-temps 500ms. JAMAIS depuis une ISR ni depuis le
//      chemin audio (mixer / *_generate / i2s_output).
//  * Simple copie de uint32_t volatiles (s1T, s2T, drT, fxT) + ESP.getFreeHeap
//      / ESP.getFreePsram / millis / compteur MIDI. AUCUN lock, AUCUNE
//      allocation dans le chemin audio : un uint32_t aligne s'ecrit/lit de
//      facon atomique sur le port de l'ESP32 — le lecteur web ne voit jamais
//      de valeur corrompue.
//    * Le WebSocket pousse le snapshot UNiquement si un client est connecte
//      (ws.count() > 0), depuis la tache serveur (event loop ESPAsyncWebServer,
//      Core 1, priorite idle) — pas depuis le contexte audio.
//    * La serialisation JSON compact est faite au moment du push (Core 1,
//      idle) sur un buffer stack, hors de tout contexte temps reel.
//
//  Fichier compile UNIQUEMENT si WEB_SERVER_ENABLED (net_config.h). Quand la
//  feature est desactivee, AcidBox.ino fournit le stub debugSnapshotTick().
// =============================================================================
#if WEB_SERVER_ENABLED

#include "config.h"
#include "net_config.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

// --- sources des donnees (definis dans AcidBox.ino / midi_handler.ino) -------
extern volatile uint32_t s1T, s2T, drT, fxT;
extern volatile uint32_t midi_note_count;

// --- API publique (appelee depuis regular_checks + setupWebServer) -----------
// Copie atomique-mot-a-mot des compteurs -> dbg_snap. HORS hot path.
void debugSnapshotTick();
// Serialise dbg_snap en JSON compact. Retourne le nombre d'octets ecrits.
size_t debugSnapshotToJSON(char *buf, size_t maxlen);

// =============================================================================
//  Snapshot partage — ecrit 1x/500ms par regular_checks, lu par le WebSocket.
//  Tous les champs sont des uint32_t : une ecriture par mot est atomique sur
//  le port de l'ESP32, le lecteur ne voit jamais de valeur corrompue.
// =============================================================================
struct DebugSnapshot {
  uint32_t s1T;           // temps synth1  (us)
  uint32_t s2T;           // temps synth2  (us)
  uint32_t drT;           // temps drums   (us)
  uint32_t fxT;           // temps mixer+i2s+fx (us)
  uint32_t DMA_BUF_TIME;  // budget du buffer DMA (us)
  uint32_t free_heap;     // heap libre (bytes)
  uint32_t free_psram;    // PSRAM libre (bytes)
  uint32_t midi_note_count; // nb de NoteOn recues (cumule)
  uint32_t uptime_ms;     // uptime (ms)
};

volatile DebugSnapshot dbg_snap; // ecrit par regular_checks, lu par le WS

// Garde-temps 500ms (static local) : regular_checks tourne a une frequence
// irreguliere, on ne renouvelle le snapshot qu'une fois toutes les 500ms.
void debugSnapshotTick() {
  static uint32_t last_tick = 0;
  const uint32_t now = millis();
  if (now - last_tick < 500) return;
  last_tick = now;

  // Copie mot a mot des volatiles : chaque l'ecriture d'un uint32_t aligne est
  // atomique sur le port de l'ESP32, le lecteur (WS sur Core 1) peut lire en
  // parallele sans voir de valeur corrompue.
  dbg_snap.s1T           = s1T;
  dbg_snap.s2T           = s2T;
  dbg_snap.drT           = drT;
  dbg_snap.fxT           = fxT;
  dbg_snap.DMA_BUF_TIME  = DMA_BUF_TIME; // const, copie pour uniformite
  dbg_snap.free_heap     = (uint32_t)ESP.getFreeHeap();
  dbg_snap.free_psram    = (uint32_t)ESP.getFreePsram();
  dbg_snap.midi_note_count = midi_note_count;
  dbg_snap.uptime_ms     = now;
}

// Serialise le snapshot en JSON compact :
//   {"s1":N,"s2":N,"dr":N,"fx":N,"dms":N,"hm":N,"ps":N,"mp":N,"up":N}
// ~150-180 octets. Reserve 220 (StaticJsonDocument + buffer sortie) pour la
// marge. HORS hot path : appelee uniquement depuis la tache serveur quand
// ws.count() > 0 (push regulier ou connexion).
size_t debugSnapshotToJSON(char *buf, size_t maxlen) {
  StaticJsonDocument<220> doc;
  doc["s1"] = dbg_snap.s1T;
  doc["s2"] = dbg_snap.s2T;
  doc["dr"] = dbg_snap.drT;
  doc["fx"] = dbg_snap.fxT;
  doc["dms"]  = dbg_snap.DMA_BUF_TIME;
  doc["hm"]   = dbg_snap.free_heap;
  doc["ps"]   = dbg_snap.free_psram;
  doc["mp"]   = dbg_snap.midi_note_count;
  doc["up"]   = dbg_snap.uptime_ms;
  return serializeJson(doc, buf, maxlen);
}

#endif // WEB_SERVER_ENABLED