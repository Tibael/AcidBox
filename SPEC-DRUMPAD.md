# Cahier des charges — AcidBox Feature: Drumpad TR-808 & Navigation Web

> **Rôle** : Document de spécification technique rédigé par le CTO (Hermes).
> Exécuté par OpenCode / sub-agents.
> **Branche de travail** : `feature/drumpad-tr808`

## 0. CONTRAINTES ABSOLUES (NON-NÉGOCIABLES)

1. **Non-perturbation de l'audio et des capteurs** :
   - Le moteur audio (44.1kHz, double tâche FreeRTOS, Core 0 audio_task1, Core 1 audio_task2) et la lecture des capteurs / triggers GPIO sont **prioritaires absolus**.
   - La page web et le serveur HTTP (`ESPAsyncWebServer` sur Core 1 à priorité 0 idle) ne doivent **jamais** bloquer l'audio ou les capteurs.
   - **Interdiction absolue de mutex bloquant ou de delay** dans les handlers web ou le chemin audio.
   - Les déclenchements de pads depuis le web appellent `handleNoteOn(DRUM_MIDI_CHAN, note, velocity)` (non-bloquant, atomique, routage MIDI direct en mémoire).
   - L'envoi depuis le navigateur vers l'ESP32 doit être asynchrone (requête `fetch` POST sans blocage ou message WebSocket).
2. **Double compatibilité Arduino IDE & PlatformIO** :
   - Le code doit impérativement compiler sans aucune erreur ni warning bloquant avec :
     - `arduino-cli compile --fqbn esp32:esp32:seeed_xiao_esp32s3:PSRAM=opi,PartitionScheme=partitions` (ou setup actuel `partitions.csv`)
     - `pio run -e xiao_esp32s3` (PlatformIO + pioarduino).
   - Respecter scrupuleusement le modèle de compilation `.ino` (concaténation de tous les `.ino` en une seule unité de compilation, déclarations de prototypes, gardes d'inclusion).

---

## 1. Objectifs de la fonctionnalité

1. **Page principale (`/` ou `/index.html`) transformée en Drumpad temps réel** :
   - Remplacer l'ancien affichage Mixer par une grille de pads TR-808 réactive et ergonomique (adaptée desktop et mobile / touch).
   - Chaque pad correspond à un son/instrument du kit TR-808.
   - Afficher clairement sur chaque pad :
     - Le nom de l'instrument TR-808 (ex: Bass Drum, Snare, HiHat Closed, etc.)
     - L'abréviation usuelle (BD, SD, CH, OH, etc.)
     - Le **numéro de note MIDI** exact (ex: Note 36, Note 38, etc.) afin que l'utilisateur puisse immédiatement le repérer pour le reporter dans la page `/config` des triggers GPIO.
   - Jouer le son au clic / appui (tactile et souris) avec retour visuel immédiat (animation active sur le pad, faible latence).
2. **Barre de navigation globale** :
   - Présente sur toutes les pages (`/`, `/config`, `/debug`).
   - Liens clairs vers :
     - **Drumpad** (`/`)
     - **Config Triggers** (`/config`)
     - **Debug / Monitor** (`/debug`)
3. **API Backend pour le Drumpad** :
   - Endpoint léger et non bloquant `POST /api/pad` ou réutilisation / complément de `POST /api/play` ou `POST /api/test` :
     - Payload : `{"note": <uint8_t>, "velocity": <uint8_t>}` (par défaut velocity = 127, channel = 10 / DRUM_MIDI_CHAN).
     - Handler : appelle directement `handleNoteOn(DRUM_MIDI_CHAN, note, velocity);`
     - Réponse HTTP immédiate `{"ok":true}`.
   - Aucun impact sur la boucle audio, temps d'exécution minimal.

---

## 2. Cartographie des notes TR-808 (`midi_config.h`)

D'après `midi_config.h` (lignes 231-244) :
- **Note 35, 36** : Bass Drum (BD) — pad standard 36 (C1 / Bass Drum)
- **Note 37** : Rim Shot / Claves (RS)
- **Note 38, 40** : Snare Drum (SD) — pad standard 38 (D1 / Snare Drum) & 40
- **Note 39** : Hand Clap / Maracas (CP)
- **Note 42, 44** : Closed Hi-Hat (CH) — pad standard 42 (F#1 / Closed HiHat)
- **Note 46** : Open Hi-Hat (OH) — pad standard 46 (A#1 / Open HiHat)
- **Note 41, 43** : Low Tom / Low Conga (LT)
- **Note 45, 47** : Mid Tom / Mid Conga (MT)
- **Note 48, 50** : Hi Tom / Hi Conga (HT)
- **Note 49** : Crash Cymbal (CY)
- **Note 51** : Cowbell (CB)

Le drumpad affichera tous ces sons TR-808 (au moins 12 à 16 pads principaux organisés en grille 4x3 ou 4x4) avec leur numéro de note bien visible.

---

## 3. Plan de découpage des phases

### Phase 1 : Backend `web_server.ino`
- Ajouter la route `POST /api/pad` acceptant `{ "note": 0..127, "velocity": 0..127 }`.
- Déclencher `handleNoteOn(DRUM_MIDI_CHAN, note, vel)` sur Core 1 sans aucun blocage.
- Vérifier la déclaration préalable de `handleNoteOn` dans `web_server.ino` (qui se trouve plus haut ou plus bas selon l'ordre alphabétique `.ino` : `midi_handler.ino` < `web_server.ino`).

### Phase 2 : Frontend (`data/web/index.html`, `data/web/app.js`, `data/web/style.css`)
- Dans `index.html` :
  - Remplacer le contenu `<main id="mixer">` par `<main id="drumpad">`.
  - Intégrer les pads TR-808 avec attribut `data-note="36"`, label du son et affichage visible du numéro de note MIDI.
  - Ajouter la navigation `<nav class="nav-links">` avec liens vers `/`, `/config`, `/debug`.
- Dans `style.css` :
  - Styles pour la navigation, la grille drumpad (pads carrés/arrondis, responsive, touches tactiles `touch-action: manipulation`, état active/pressé avec surbrillance orange/cyan TR-808).
- Dans `app.js` :
  - Écouteurs `pointerdown` / `click` sur les pads pour déclenchement ultra réactif.
  - Envoi vers `/api/pad` avec note et velocity.
- Mettre à jour `config.html` et `debug.html` pour inclure la même barre de navigation cohérente.

### Phase 3 : Validation Compilation & Double Toolchain
- Compiler avec `arduino-cli`.
- Compiler avec `pio run -e xiao_esp32s3`.
- Vérifier le zero impact sur la taille mémoire et la stabilité.
