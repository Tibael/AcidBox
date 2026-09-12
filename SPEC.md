# Cahier des charges — AcidBox Extended

> **Rôle** : Ce document est la spécification technique de référence. Il est écrit par le CTO (Hermes) et exécuté par les sub-agents / OpenCode. Toute implémentation DOIT respecter ces contraintes.

## 0. CONTRAINTE ABSOLUE — Non-perturbation audio

**Aucune nouvelle fonction ne doit dégrader l'audio temps réel.** Le moteur audio existant (44.1kHz, buffers DMA de 32 samples, dual-core) est intouchable dans son chemin critique.

Règles non-négociables :
1. **Core 0** reste exclusivement dédié à `audio_task1` (Synth1 + Drums). N'y ajouter AUCUNE tâche.
2. Le WebServer, WiFi, ESP-NOW tournent **uniquement sur Core 1**, en **priorité basse** (tâche idle, priorité 0), jamais dans le chemin audio.
3. **Interdiction de mutex/lock bloquant** dans les fonctions appelées par le chemin audio (`mixer()`, `*_generate()`, `Process()`, `getSample()`).
4. Communication tâche-web ↔ audio uniquement via **variables atomiques** (`volatile` pour flags simples booléens/entiers alignés, `std::atomic` si besoin). Le producteur audio écrit, le consommateur web lit — jamais l'inverse dans le hot path.
5. La lib web doit être **asynchrone** (`ESPAsyncWebServer` + `AsyncTCP`) — pas de `WebServer` bloquant.
6. Le debug web remplace le debug Serial (`DEBUG_ON`) qui consomme des ticks temps réel. Collecte par **snapshot périodique** (500ms via timer2 déjà existant), pas de log par sample.

## 1. Contexte technique existant

- **Cible** : ESP32-S3 avec PSRAM (branche S3-regular). MCU dual-core Xtensa LX7 @240MHz.
- **Audio** : `AcidBox.ino` orchestre 2 tâches FreeRTOS épinglées :
  - Core 0 : `audio_task1` → `synth1_generate()` + `drums_generate()`
  - Core 1 : `audio_task2` → `mixer()` + `i2s_output()` + `synth2_generate()`
- **Instruments (niveau haut)** : `Synth1` (canal MIDI 1), `Synth2` (canal 2), `Drums` (canal 10).
- **Mixer** : `general.ino::mixer()` somme synth1_out + synth2_out + drums_out + delay + reverb → compresseur → limiteur.
- **Sampler** : `sampler.h/.ino`. Contient déjà `bool is_muted[17]` pour les 17 sous-instruments de drums (jamais exposé).
- **MIDI** : `midi_handler.ino::handleNoteOn(channel, note, velocity)` route par canal vers Drums/Synth1/Synth2. Point d'entrée idéal pour le "MIDI interne".
- **Config** : `config.h` centralise tous les `#define`.
- **Pas d'interface web actuellement.**

## 2. Fonctionnalités à développer

### F1 — WiFi Access Point (propre AP)
- L'ESP32-S3 crée son propre AP : SSID `AcidBox`, mot de passe configurable.
- **Canal radio FIXE** (ex. canal 1) — impératif pour compatibilité future ESP-NOW (ESP-NOW et AP doivent partager le canal).
- Pas de connexion à un réseau externe (mode AP pur = moins d'interruptions radio imprévisibles).
- Config dans nouveau fichier `net_config.h`.

### F2 — Interface web (ESPAsyncWebServer, Core 1, priorité idle)
Pages servies depuis LittleFS (`/data/web/`) :
- `GET /` → **Mixer** : mute/solo des 3 instruments principaux (Synth1, Synth2, Drums) + optionnellement les 17 sous-instruments drums.
- `GET /debug` → **Monitoring temps réel** (voir F5).
- `GET /config` → **Config des 4 triggers GPIO** (voir F4).

API REST (JSON) :
- `GET  /api/state` → état complet (mutes, solos, triggers, stats).
- `POST /api/mute` → `{ "instrument": <id>, "muted": <bool> }`.
- `POST /api/solo` → `{ "instrument": <id>, "solo": <bool> }` (multi-solo autorisé).
- `POST /api/trigger` → `{ "id": 0..3, "channel": <1|2|10>, "note": 0..127, "velocity": 0..127 }`.
- `GET  /ws` → WebSocket debug (push snapshot 500ms).

### F3 — Mute / Solo par instrument
- **Mute** : coupe un instrument. Multi-instrument.
- **Solo** : **multi-instrument** (plusieurs instruments peuvent être en solo simultanément). Si ≥1 solo actif → seuls les instruments en solo sont audibles ; les autres sont silencieux (indépendamment de leur mute). Si aucun solo → règle mute normale.
- **Niveau d'application** : dans `mixer()` (general.ino), appliquer un gain 0.0/1.0 sur `synth1_out`, `synth2_out`, `drums_out` selon l'état effectif calculé. La logique solo/mute est calculée HORS du hot path (dans la tâche web ou regular_checks), qui met à jour 3 flags `volatile bool audible_synth1/synth2/drums`. Le mixer ne fait que lire ces 3 booléens.
- Pour les 17 sous-instruments drums : réutiliser le `is_muted[17]` existant du sampler, l'exposer via setters.

### F4 — 4 triggers GPIO → notes MIDI internes (persistant)
- 4 triggers, chacun configurable : `{ channel, note, velocity }`.
- La **lecture GPIO est laissée vide** (l'utilisateur a déjà son code) : fournir une fonction stub `readTriggerGPIOs()` documentée + une fonction publique `fireTrigger(uint8_t id)` que son code appellera.
- `fireTrigger(id)` déclenche une **note MIDI interne** : appelle directement `handleNoteOn(triggers[id].channel, triggers[id].note, triggers[id].velocity)` — réutilise le chemin MIDI existant, aucune latence ajoutée.
- Prévoir aussi `releaseTrigger(id)` → `handleNoteOff(...)` pour les notes tenues (synths). Pour les drums (one-shot), le NoteOff peut être immédiat ou ignoré.
- **Persistance** : config des 4 triggers sauvegardée en LittleFS (`/config/triggers.json`), rechargée au boot. Écriture flash uniquement sur changement via l'API web (jamais dans le hot path).

**Décisions de review (post-implémentation) :**
- `handleNoteOn/handleNoteOff` : `inline` retiré de midi_handler.ino — l'appel depuis gpio_triggers.ino (ordre alphabétique .ino : g < m) exige une linkage externe standard. Déclarations anticipées dans gpio_triggers.ino.
- `held_active[4]` (bool séparé) ajouté : la sentinelle `held_note==0` invalide la note MIDI 0 (C-1, valide). La validité de l'auto-release ne dépend plus de la valeur de la note.
- Auto-release au re-fire : si le trigger tenait déjà une note (configurée différente entre-temps), NoteOff avant NoteOn — évite les notes orphelines sur les synthés.

### F5 — Debug web (remplace Serial debug)
- Collecter les métriques déjà calculées : `s1T, s2T, drT, fxT` (temps par module en µs), `DMA_BUF_TIME`, heap libre, PSRAM libre, activité MIDI (compteur de notes).
- Snapshot copié dans une struct partagée 1×/500ms (timer2 existant).
- WebSocket pousse ce snapshot au client. Page `debug.html` affiche : charge CPU par module (% de DMA_BUF_TIME), heap/PSRAM, dernières notes MIDI, état buffers.

### F6 — Structure ESP-NOW (préparée, DÉSACTIVÉE)
- Créer `espnow_comm.h/.ino` avec squelette : init, callback réception, fonction envoi.
- **Désactivé par défaut** (`#define ESPNOW_ENABLED 0`). Ne pas l'initialiser tant que non activé.
- Documenter la contrainte canal partagé avec l'AP.
- But : préparer la comm 2 ESP32 (ex. mute distant) sans l'implémenter maintenant.

## 3. Nouveaux fichiers

| Fichier | Rôle |
|---------|------|
| `net_config.h` | Config WiFi AP + canal + defines ESP-NOW |
| `web_server.ino` | ESPAsyncWebServer, routes REST, WebSocket, service LittleFS |
| `gpio_triggers.ino` | Config 4 triggers, `fireTrigger()`, `readTriggerGPIOs()` (stub), persistance JSON |
| `espnow_comm.h` / `espnow_comm.ino` | Squelette ESP-NOW désactivé |
| `data/web/index.html` | UI mixer |
| `data/web/debug.html` | UI monitoring |
| `data/web/config.html` | UI config triggers |
| `data/web/app.js` | fetch + WebSocket |
| `data/web/style.css` | style dark |

## 4. Fichiers modifiés

| Fichier | Modification |
|---------|-------------|
| `config.h` | Ajouter defines `WEB_SERVER_ENABLED`, includes conditionnels |
| `AcidBox.ino` | Init WiFi AP + web task (Core1, prio 0) dans setup() ; hooks debug snapshot |
| `general.ino` (mixer) | Appliquer gain audible_synth1/2/drums (lecture de 3 volatile bool) |
| `sampler.h` | Exposer `SetMute/GetMute/SetSolo` pour les 17 sous-instruments |
| `synthvoice.h` | (si besoin) exposer un flag mute cohérent |
| `midi_handler.ino` | Exposer `handleNoteOn/Off` pour appel interne (déjà accessibles) |

## 5. Dépendances à ajouter
- `ESPAsyncWebServer` + `AsyncTCP` (ESP32).
- `ArduinoJson` (pour API JSON + persistance triggers).
- WiFi + LittleFS (déjà dans le core ESP32).

## 6. Découpage en phases (livrables incrémentaux)

- [x] **Phase 1** : `net_config.h`, WiFi AP, `web_server.ino` minimal, mute/solo (mixer + sampler), `index.html`. → Compile + AP visible + mute fonctionne.
- [x] **Phase 2** : `gpio_triggers.ino`, `fireTrigger()`, persistance JSON, `config.html`. → Trigger interne joue une note.
- [x] **Phase 3** : debug snapshot + WebSocket + `debug.html`. → Monitoring temps réel sans Serial.
- [x] **Phase 4** : squelette `espnow_comm` désactivé. → Compile, prêt pour activation future.

## 7. Critères d'acceptation

1. Le code **compile** pour ESP32-S3 (branche S3-regular, PSRAM activée).
2. L'audio n'a **aucune dégradation** : timing modules (`s1T+s2T+drT+fxT`) reste < DMA_BUF_TIME.
3. Mute/solo (multi) fonctionnent et sont audibles instantanément.
4. Les 4 triggers déclenchent la bonne note sur le bon canal ; config survit au reboot.
5. Page debug affiche les stats temps réel via WebSocket.
6. Squelette ESP-NOW présent mais désactivé (aucun impact runtime).
7. Aucun mutex bloquant dans le hot path audio.

## 8. Style & conventions
- Respecter le style existant (Arduino `.ino` + `.h`, indentation 2 espaces, macros `DEB/DEBF`).
- Ne pas casser les `#ifdef NO_PSRAM` / `USE_INTERNAL_DAC` / `JUKEBOX`.
- Commits atomiques par phase, auteur `hermes`.
