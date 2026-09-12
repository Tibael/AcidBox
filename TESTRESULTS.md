# Results de tests statiques — AcidBox Extended (Phase 1-4)

Date : 2026-09-12
Périmètre : S1-S8 (plan de test, section 2)

| # | Vérification | Résultat | Détail / Évidence |
|---|---|---|---|
| S1 | Pas de mutex dans le hot path | ✅ PASS | 0 occurrence de `xSemaphore`/`mutex`/`lock` dans `general.ino`, `sampler.ino`, `synthvoice.ino`, `AcidBox.ino` (audio tasks). |
| S2 | Pas de print dans le hot path | ✅ PASS | Seuls `DEBF`/`DEBUG` sous `#ifdef DEBUG_{MASTER_OUT,_SAMPLER,_MIDI,_JUKEBOX,_FX,_TIMING}`. Hors `#ifdef`, les macros sont no-op. Aucun `Serial.print` direct. |
| S3 | Flags volatile bien typés | ✅ PASS | `audible_synth1/2/drums` sur AcidBox.ino:121-123 : `volatile bool`. `s1t..fxt` : `volatile uint32_t`. `current_gen_buf/out_buf` : `volatile uint8_t`. Tous nativement alignés, accès atomiques. |
| S4 | Pas d'appel web depuis audio | ✅ PASS | grep croisé : `mixer()`, `synth1/2_generate()`, `drums_generate()`, `Sampler::Process`, `SynthVoice::getSample` n'appellent aucune fonction web. |
| S5 | WiFi/serveur pas sur Core 0 | ✅ PASS | `xTaskCreatePinnedToCore` : audio_task1 → core 0 (line 343), audio_task2 → core 1 (line 344). WiFi/ESPAsyncWebServer tournent sur core 1 (priorité idle). ESP-NOW callback sur core 1. |
| S6 | Persistance triggers JSON valide | ✅ PASS | `saveTriggers` (gpio_triggers.ino:100-115) : validation bornes `chan ∈ {1,2,10}`, `note ≤ 127`, `vel ≤ 127`. Rejet silencieux si `LittleFS.exists("/")` == false. `loadTriggers` valide taille (0-512 bytes) et parse JSON. |
| S7 | API REST schémas JSON | ✅ PASS | `app.js` envoie `{instrument, muted}` / `{instrument, solo}` → handler `handleMute/handleSolo` attend `obj["instrument"]`/`obj["muted"]`/`obj["solo"]`. `config.html` envoie `{id, channel, note, velocity}` → `handleSetTrigger` attend les 4. Cohérent. |
| S8 | Auto-release note-0 | ✅ PASS | `gpio_triggers.ino:55-56` : `held_note[4]` + `held_active[4]` (bool séparé). Note 0 valide (pas de sentinelle `0`). `fireTrigger`/`releaseTrigger` check `held_active[id]` avant `handleNoteOff`. |

## Verdict

8/8 checks PASS. Aucune anomalie bloquante dans le chemin audio.