#ifndef NET_CONFIG_H
#define NET_CONFIG_H

// ==== F1 — WiFi Access Point (propriété AcidBox) =============================
// L'ESP32-S3 crée son propre AP, mode pur: aucune connexion à un réseau externe.
// Le canal radio est FIXE (canal 1) — impératif pour la compatibilité future
// ESP-NOW (ESP-NOW et l'AP doivent obligatoirement partager le même canal).
//
// WebServer / WiFi / ESP-NOW ne tournent QUE sur Core 1, en priorité idle (0).
// Voir la contrainte absolue (section 0) du SPEC: aucun mutex bloquant dans le
// chemin audio, aucune tâche ajoutée sur Core 0.

#define WIFI_SSID           "AcidBox"
#define WIFI_PASSWORD       "acidacid"
#define WIFI_CHANNEL        1     // canal radio FIXE, partagé à venir avec ESP-NOW

// ==== F6 — ESP-NOW (squelette, DÉSACTIVÉ par défaut) =========================
// Ne pas l'initialiser tant que non activé. Le squelette arrive en Phase 4.
#define ESPNOW_ENABLED      0

// ==== F2 — Interface web =====================================================
#define WEB_SERVER_ENABLED  1
#define WEB_SERVER_PORT     80

#endif