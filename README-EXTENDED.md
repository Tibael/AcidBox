# AcidBox Extended — Guide d'installation (utilisateur final)

Ce guide couvre l'installation et la configuration de la version étendue d'AcidBox.
Pour le fonctionnement de base (DAC, schéma, contrôle MIDI), voir [README.md](README.md).

---

## 1. Matériel requis

* ESP32-S3 avec PSRAM (OPI), ex. **Seeed XIAO ESP32S3** ou Lolin S3.
* DAC externe I2S (PCM5102 recommandé).
* 4 boutons / capteurs pour les triggers GPIO (voir §5).

## 2. Compilation

Deux toolchains sont supportées : **Arduino IDE** (§2.1) et **PlatformIO** (§2.2).
Les deux compilent **exactement les mêmes fichiers** (le sketch reste à la racine du
repo, option B) — aucun fichier n'a à être déplacé ni dupliqué, et les dépendances
sont identiques (§3). Choisissez la toolchain de votre choix.

### 2.1 Arduino IDE

1. Installer le core **ESP32** (arduino-esp32) correspondant à votre IDE.
2. Installer les bibliothèques (voir §3).
3. Dans **Tools**, sélectionner :
   * **Board** : `Seeed XIAO ESP32S3` (ou `ESP32S3 Dev Module`)
   * **PSRAM** : `OPI PSRAM`
   * **Partition Scheme** :
     * **Recommandé — Custom** (2 MB APP / 5.9 MB LittleFS) : le fichier
       [`partitions.csv`](partitions.csv) à la racine du repo est utilisé
       automatiquement quand *Custom* est sélectionné. 1.5MB du schéma
       `Default with spiffs` ne suffit PAS pour les samples `data/` (2.9MB).
     * ESP32-S3 Dev Module générique : `No OTA (1MB APP/ 3MB SPIFFS)`
     * **Seeed XIAO ESP32S3** (flash 8 MB) sans custom : `Default with spiffs (3MB APP/1.5MB SPIFFS)`
   * **Filesystem** : `LittleFS`
4. Compiler & uploader le sketch.

> ⚠️ Éviter les cores 3.1.2 / 3.1.3 / 3.2.0-RC1 (bug i2s + PSRAM, voir README.md).

### 2.2 PlatformIO

Aucune dépendance à installer manuellement : `lib_deps` dans `platformio.ini`
gère l'installation automatique des bibliothèques (§3) à la première compilation.

Installation :

```bash
pip install platformio        # ou : pipx install platformio
cd <dossier du projet>
```

Build :

```bash
pio run -e xiao_esp32s3
```

Upload firmware :

```bash
pio run -e xiao_esp32s3 -t upload
```

Upload du système de fichiers (pages web + samples, dossier `data/`) :

```bash
pio run -e xiao_esp32s3 -t uploadfs
```

Moniteur série :

```bash
pio device monitor
```

Notes :

* **Partition** : `partitions.csv` (custom, 2 MB APP / 5.9 MB LittleFS) — les
  samples `data/` (2.9 MB) tiennent avec marge ; `default_8MB` (1.5 MB FS) ne
  suffisait pas.
* **Environnement secondaire** : `esp32s3_generic` pour la carte **ESP32-S3 Dev
  Module** (référence de build) — `pio run -e esp32s3_generic`.
* PlatformIO utilise le platform **pioarduino** (fork communautaire qui suit l'
  arduino-esp32 **core 3.x**) ; le platform officiel `espressif32` est figé sur
  core 2.x et n'est pas compatible avec ce code (APIs I2S différentes).

## 3. Bibliothèques requises (Arduino Library Manager ou Git)

| Bibliothèque | Rôle | Lien |
|---|---|---|
| **ESPAsyncWebServer** | serveur web asynchrone (Web UI) | https://github.com/ESP32Async/ESPAsyncWebServer |
| **AsyncTCP** | transport TCP (dépendance ESPAsyncWebServer) | https://github.com/ESP32Async/AsyncTCP |
| **ArduinoJson** | API JSON + persistance triggers | https://github.com/bblanchon/ArduinoJson |
| **[MIDI Library]** | MIDI serial (déjà requis de base) | https://github.com/FortySevenEffects/arduino_midi_library |

> ⚠️ Utiliser le fork **ESP32Async** (pas le "ESPAsyncWebServer" du Library Manager,
> fork `lacamera` v3.1.0 : il appelle des APIs mbedtls supprimées du core 3.x et
> ne compile pas).

WiFi et LittleFS sont inclus dans le core ESP32 (aucune lib externe).

## 4. Interface web

L'ESP32-S3 crée son **Access Point WiFi** au boot :

| Paramètre | Valeur |
|---|---|
| SSID | `AcidBox` |
| Mot de passe | `acidacid` |
| IP | `192.168.4.1` |
| Canal radio | **1** (fixe, partagé avec ESP-NOW — voir §6) |

Ouvrir `http://192.168.4.1` sur n'importe quel appareil connecté à l'AP `AcidBox`.

### Pages

| URL | Contenu |
|---|---|
| `/` | **Mixer** — mute/solo des 3 instruments (Synth1, Synth2, Drums) |
| `/config` | **Triggers GPIO** — config des 4 triggers (canal, note, vélocité) |
| `/debug` | **Monitoring temps réel** — charge CPU, heap/PSRAM, notes MIDI (WebSocket 500 ms) |

### API REST (JSON)

| Méthode | Endpoint | Body | Description |
|---|---|---|---|
| `GET` | `/api/state` | — | État complet (mute, solo, audible par instrument) |
| `POST` | `/api/mute` | `{"instrument":"synth1","muted":true}` | Mute on/off |
| `POST` | `/api/solo` | `{"instrument":"drums","solo":true}` | Solo on/off (multi-solo) |
| `GET` | `/api/triggers` | — | Config des 4 triggers |
| `POST` | `/api/trigger` | `{"id":0,"channel":10,"note":36,"velocity":127}` | Modifier + sauvegarder un trigger |
| `POST` | `/api/test` | `{"id":0}` | Tester un trigger (fireTrigger) |
| `GET` | `/ws` | — | WebSocket push du snapshot de debug (500 ms) |

Identifiants d'instrument : `synth1`, `synth2`, `drums`.

### Persistance

* Triggers : `/config/triggers.json` (LittleFS) — rechargé au boot, écrit uniquement
  via l'API web (`POST /api/trigger`).
* Mute/solo : en mémoire (perdus au reboot — comportement par conception).
* Samples : chargés depuis LittleFS (`/data/...`), pré-requis upload via
  **Tools → ESP32 Sketch Data Upload** (voir README.md).

## 5. Triggers GPIO (4 boutons)

La lecture GPIO est **laissée vide** — l'utilisateur branche son propre code dans
`readTriggerGPIOs()` (gpio_triggers.ino). À appeler dans `loop()` ou votre boucle.

Exemple minimal (relais/bouton avec pull-down, front montant) :

```cpp
// À placer dans loop() ou votre code utilisateur (jamais dans le chemin audio)
#define TRIGGER_PIN_0  2
#define TRIGGER_PIN_1  3
#define TRIGGER_PIN_2  4
#define TRIGGER_PIN_3  5

// Dans setup() :
pinMode(TRIGGER_PIN_0, INPUT_PULLDOWN);
// ... idem pour les autres

void readTriggerGPIOs() {
  static uint8_t prev = 0;
  uint8_t cur = 0;
  if (digitalRead(TRIGGER_PIN_0)) cur |= 1;
  if (digitalRead(TRIGGER_PIN_1)) cur |= 2;
  if (digitalRead(TRIGGER_PIN_2)) cur |= 4;
  if (digitalRead(TRIGGER_PIN_3)) cur |= 8;

  for (int i = 0; i < 4; i++) {
    uint8_t mask = 1 << i;
    bool now = cur & mask;
    bool was = prev & mask;
    if (now && !was) fireTrigger(i);
    if (!now && was) releaseTrigger(i);
  }
  prev = cur;
}
```

API disponible (déclarées dans gpio_triggers.ino) :
* `fireTrigger(uint8_t id)` — joue la note configurée (NoteOn)
* `releaseTrigger(uint8_t id)` — relâche la note tenue (NoteOff)
* `readTriggerGPIOs()` — stub à remplir (code utilisateur)

La config de chaque trigger (canal 1/2/10, note 0–127, vélocité 0–127) se fait
via `http://192.168.4.1/config` et persiste au reboot.

## 6. ESP-NOW (futur — section préparée)

Le squelette ESP-NOW est présent mais **désactivé par défaut** (`ESPNOW_ENABLED = 0`
dans `net_config.h`). Il sera utilisé pour la comm 2-ESP32 (ex. mute distant).

### Contraintes (identiques à l'AP)

* **Canal radio partagé** : ESP-NOW et l'AP WiFi doivent être sur le **même canal**
  (canal 1, `WIFI_CHANNEL` dans `net_config.h`). Ne pas changer le canal dans l'un
  sans changer l'autre.
* **Payload max** : 250 octets par trame ESP-NOW. La trame MIDI actuelle
  (`EspNowMidiMsg`) fait 6 octets.
* **Initialisation** : `setupEspNow()` est appelé après `setupWebServer()`
  (le mode AP et le canal sont déjà en place).

### Activation (future)

1. Mettre `#define ESPNOW_ENABLED 1` dans `net_config.h`.
2. Récupérer l'adresse MAC du pair distant (ex. `dev()` / `getmac()`).
3. Décommenter et remplir le bloc `esp_now_add_peer()` dans `espnow_comm.ino`.
4. À l'envoi : `espNowSendMidi(channel, note, velocity, noteOn)`.
5. À la réception : le callback `espNowOnDataRecv` appelle directement
   `handleNoteOn` / `handleNoteOff` (chemin MIDI existant, zéro latence).

Aucun impact audio : le callback ESP-NOW ne fait que des opérations légères
(memcpy + check magie + appel MIDI), jamais de mutex bloquant dans le hot path.

## 7. Limites & notes

* `ESPAsyncWebServer` tourne sur **Core 1** en priorité idle — le chemin audio
  (Core 0 : synth1 + drums) n'est jamais touché.
* Le debug web remplace le debug Serial (`DEBUG_ON` consomme des ticks temps réel).
* Ne pas ajouter de tâche sur Core 0, ni de mutex bloquant dans `mixer()` /
  `*_generate()` / `Process()` / `getSample()`.