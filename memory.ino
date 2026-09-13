// Phase E — 50 Memories (persistence /memories.json)
// initMemories/saveMemories/loadMemories/addMemory/delMemory/loadMemory
//
// loadMemory(id): applies 4 trigger configs (ch,note,vel) + all CCs.
// Called from web API (Core 1 idle) and MIDI CC handler (Core 1, event-loop).

#include "config.h"
#include "net_config.h"
#include "memory.h"
#include "presets.h"   // for PRESET_CC_UNUSED (0xFF)
#include <LittleFS.h>
#include <ArduinoJson.h>

Memory  memories[MAX_MEMORIES];
uint8_t memCount    = 0;
uint8_t currentMemory = 0;

static const char *kMemPath = "/memories.json";

// --- forward decls (included in web_server.ino via extern) ---
extern TriggerConfig triggers[4];
extern void  handleNoteOn(uint8_t, uint8_t, uint8_t);
extern void  handleCC(uint8_t, uint8_t, uint8_t);

// ----------------------------------------------------------------
static void clearSlot(TriggerSlot *s) {
  s->channel  = 10;
  s->note     = 36;
  s->velocity = 127;
  for (int i = 0; i < 128; i++) s->ccs[i] = MEM_CC_UNUSED;
}

static void clearMemory(Memory *m) {
  m->name[0] = '\0';
  for (int i = 0; i < 4; i++) clearSlot(&m->triggers[i]);
}

// ----------------------------------------------------------------
void initMemories() {
  if (!LittleFS.exists("/")) LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
  for (int i = 0; i < MAX_MEMORIES; i++) clearMemory(&memories[i]);
  loadMemories();
}

bool loadMemories() {
  if (!LittleFS.exists("/")) return false;
  if (!LittleFS.exists(kMemPath)) return false;
  File f = LittleFS.open(kMemPath, "r");
  if (!f) return false;

  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  uint16_t n = arr.size(); if (n > MAX_MEMORIES) n = MAX_MEMORIES;
  memCount = 0;
  for (uint16_t i = 0; i < n; i++) {
    JsonObject o = arr[i];
    Memory &m = memories[memCount];
    clearMemory(&m);
    const char *nm = o["name"];
    if (nm) { size_t l = strlen(nm); if (l > 31) l = 31; memcpy(m.name, nm, l); m.name[l] = '\0'; }
    JsonArray trArr = o["triggers"];
    if (!trArr.isNull()) {
      for (int t = 0; t < 4 && t < (int)trArr.size(); t++) {
        JsonObject ts = trArr[t];
        m.triggers[t].channel  = (uint8_t)(ts["ch"]  | 10);
        m.triggers[t].note     = (uint8_t)(ts["note"]| 36);
        m.triggers[t].velocity = (uint8_t)(ts["vel"] | 127);
        JsonObject ccs = ts["ccs"];
        if (!ccs.isNull()) {
          for (JsonPair kv : ccs) {
            int cc = atoi(kv.key().c_str());
            if (cc >= 0 && cc < 128) m.triggers[t].ccs[cc] = (uint8_t)constrain((int)kv.value(), 0, 127);
          }
        }
      }
    }
    memCount++;
  }
  return true;
}

bool saveMemories() {
  if (!LittleFS.exists("/")) return false;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < memCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = memories[i].name;
    JsonArray trArr = o.createNestedArray("triggers");
    for (int t = 0; t < 4; t++) {
      JsonObject ts = trArr.add<JsonObject>();
      ts["ch"]   = memories[i].triggers[t].channel;
      ts["note"] = memories[i].triggers[t].note;
      ts["vel"]  = memories[i].triggers[t].velocity;
      JsonObject ccs = ts.createNestedObject("ccs");
      for (int c = 0; c < 128; c++) {
        if (memories[i].triggers[t].ccs[c] != MEM_CC_UNUSED)
          ccs[String(c)] = memories[i].triggers[t].ccs[c];
      }
    }
  }
  File f = LittleFS.open(kMemPath, "w");
  if (!f) return false;
  size_t w = serializeJson(doc, f);
  f.close();
  return w > 0;
}

int addMemory(const char *name) {
  if (memCount >= MAX_MEMORIES) return -1;
  Memory &m = memories[memCount];
  clearMemory(&m);
  if (name) { size_t l = strlen(name); if (l > 31) l = 31; memcpy(m.name, name, l); m.name[l] = '\0'; }
  memCount++;
  return memCount - 1;
}

void delMemory(uint8_t id) {
  if (id >= memCount) return;
  for (int i = id; i < memCount - 1; i++) memories[i] = memories[i + 1];
  clearMemory(&memories[memCount - 1]);
  memCount--;
}

// Applies memory index id: sets 4 triggers then fires CCs.
// Side-effect free — called from web OR MIDI CC, no mutex.
void loadMemory(uint8_t id) {
  if (id >= memCount) return;
  currentMemory = id;
  Memory &m = memories[id];
  // Apply triggers via existing trigger config API
  for (int t = 0; t < 4; t++) {
    triggers[t].channel  = m.triggers[t].channel;
    triggers[t].note     = m.triggers[t].note;
    triggers[t].velocity = m.triggers[t].velocity;
    // Fire all non-default CCs for this slot
    for (int c = 0; c < 128; c++) {
      if (m.triggers[t].ccs[c] != MEM_CC_UNUSED)
        handleCC(m.triggers[t].channel, (uint8_t)c, m.triggers[t].ccs[c]);
    }
  }
}