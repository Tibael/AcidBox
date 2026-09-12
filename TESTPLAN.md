# Plan de test — AcidBox Extended

> Tests par délégation aux sub-agents. Aucun hardware disponible : la stratégie est **compilation multi-cibles + review statique outillée**. Le plan se complétera avec des tests hardware quand libaël aura un XIAO ESP32S3 branché.

## 0. Environnement

- **Cible principale** : XIAO ESP32S3 (voiredev Arduino `esp32:esp32:XIAO_ESP32S3`)
- **Cible secondaire** : ESP32-S3 générique (celle du build de référence)
- **Toolchain** : arduino-cli 1.5.1, core esp32 3.3.11, libs ESP32Async (ESPAsyncWebServer 3.12.0, AsyncTCP 3.5.0), ArduinoJson 7.4.3, MIDI Library 5.0.2
- **Décision utilisateur (2026-09-11)** : pas de QEMU — compilation cible + review statique uniquement.

## 1. Tests de compilation (gate obligatoire)

| # | Test | Commande | Critère de réussite |
|---|------|----------|---------------------|
| C1 | Build cible XIAO | `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=no_ota` | exit 0, pas d'erreur |
| C2 | Build générique S3 (référence) | `--fqbn esp32:esp32:esp32s3:...` | exit 0 |
| C3 | Build sans web (regression) | compile avec `WEB_SERVER_ENABLED` à 0 dans net_config.h (modification temporaire) | exit 0 — prouve que le firmware reste compilable sans la fonctionnalité web |
| C4 | Build sans PSRAM (regression NO_PSRAM) | compile avec `NO_PSRAM` (via un sketch de test ou flag temporaire) | exit 0 — le mode dégradé d'origine doit survivre |
| C5 | Taille flash/RAM | lecture de la sortie compile | flash < 100%, RAM < 90% |

## 2. Tests statiques (revue outillée)

| # | Test | Méthode | Critère |
|---|------|---------|---------|
| S1 | Pas de mutex dans le chemin audio | grep des fichiers audio (`general.ino`, `sampler.ino`, `synthvoice.ino`, `AcidBox.ino` audio tasks) | 0 occurrence de `xSemaphore`/`mutex`/`lock` dans le hot path |
| S2 | Pas de print dans le chemin audio | grep `Serial.print`/`DEB(` dans mixer/generate/Process | 0 occurrence hors `#ifdef DEBUG` |
| S3 | Flags volatile bien typés | review des déclarations `audible_*`, `dbg_snap` | `volatile` + alignment natif, pas d'accès multi-mots non atomique |
| S4 | Prototypes/fonctions web non appelés depuis audio | grep croisé des appels | 0 appel à une fonction web depuis audio_task1/2, mixer, generate |
| S5 | WiFi/serveur pas sur Core 0 | grep xTaskCreatePinnedToCore | tâches nouvelles épinglées à core 1 uniquement |
| S6 | Persistance trigger : contenu JSON valide | review de saveTriggers/loadTriggers | validation bornes (canal ∈ {1,2,10}, note ≤ 127, velocity ≤ 127), rejet silencieux si FS absent |
| S7 | API REST : schémas JSON corrects | lecture des handlers | cohérence entre ce que config.html/app.js envoient et ce que les handlers attendent |
| S8 | Auto-release note-0 | review gpio_triggers.ino | held_active[] bool séparé, note 0 jouable |

## 3. Tests dynamiques (hardware XIAO requis — non exécutables ici)

À exécuter par libaël quand le hardware sera branché :
| # | Test | Procédure | Critère |
|---|------|-----------|----------|
| H1 | Boot + AP visible | flasher, chercher SSID `AcidBox` sur un téléphone | AP joignable, IP 192.168.4.1 |
| H2 | Page mixer | navigateur → 192.168.4.1 | boutons mute/solo fonctionnels, audio réagit sans glitch audible |
| H3 | Config triggers | page /config, bouton Test | note jouée immédiatement, persistance après reboot |
| H4 | Debug temps réel | page /debug | WebSocket connecté, valeurs qui bougent, pas de dégradation audio (écoute) |
| H5 | Timing audio | comparer s1T+s2T+drT+fxT à DMA_BUF_TIME sur la page debug | total < DMA_BUF_TIME, pas de clipping du timing |
| H6 | Triggers GPIO réels | brancher le code GPIO utilisateur dans readTriggerGPIOs() | déclenchement fiable, auto-release OK |

## 4. Procédure de délégation

- Chaque sous-agent reçoit ce plan et un périmètre (ex. "exécute C1-C5 et S1-S2, rapporte tout")
- Le sub-agent N'installe rien, ne commit rien — le CTO (Hermes) interprète, corrige (via OpenCode si besoin) et commit
- Toute anomalie est tracée dans ce fichier avec statut (fail → fix → re-test)

## 5. Résultats (rempli au fil des tests)

| Test | Statut | Notes |
|------|--------|-------|
| C1 | ✅ PASS | XIAO ESP32S3 default_8MB: 1 139 926 B (34% flash), RAM 43% — après fix HWCDC (Serial→Serial0, cf. bugs) |
| C2 | ✅ PASS | esp32s3 générique no_ota: 1 154 067 B (55%), RAM 43% |
| C3 | ✅ PASS | web OFF: 516 336 B (24%) vs web ON 1 154 067 B (55%) — flag efficace après fix #ifdef→#if |
| C4 | ✅ PASS | NO_PSRAM: 1 152 919 B (54%), RAM 25% (buffers PSRAM absents) |
| C5 | ✅ PASS | toutes tailles < limites, marge confortable |
| S1-S8 | ✅ PASS 8/8 | TESTRESULTS.md — aucun mutex/print dans hot path, flags atomiques, JSON cohérents |
| H1-H6 | ⏳ HARDWARE | XIAO physique requis — procédure prête (section 3) |

## Bugs trouvés et corrigés par les tests

1. **#ifdef vs #if (WEB_SERVER_ENABLED)** — le flag valeur 0 ne désactivait rien: 2 builds de taille identique. Fix: #if partout. Prouvé par C3 (24% vs 55%).
2. **XIAO HWCDC** — Serial est USB-CDC (HWCDC) sur XIAO, pas HardwareSerial: SerialMIDI ne compilait pas. Fix: #undef BOARD_HAS_UART_CHIP pour ARDUINO_XIAO_ESP32S3 + MIDI_PORT Serial0. Bug upstream (projet original), préexistant à nos modifs.
3. **PartitionScheme XIAO** — le XIAO est 8MB flash: 'no_ota' n'existe pas, utiliser 'default_8MB' (3MB APP/1.5MB SPIFFS). Documentation mise à jour dans README-EXTENDED.
