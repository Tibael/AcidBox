//  Phase E — Memories (50 max, MIDI CC selectable)
//  Persistence: /memories.json
//
//  Prototype-quirk fix: include this in AcidBox.ino #if WEB_SERVER_ENABLED
#ifndef ACIDBOX_MEMORY_H
#define ACIDBOX_MEMORY_H

#include <stdint.h>

#define MAX_MEMORIES    50
#define MEM_CC_UNUSED   0xFF

struct TriggerSlot {
  uint8_t channel;
  uint8_t note;
  uint8_t velocity;
  uint8_t ccs[128];  // index = CC number, value = CC value, 0xFF = unused
};

struct Memory {
  char         name[32];
  uint8_t      globalCCs[128];   // global filter CCs (index = CC number)
  TriggerSlot  triggers[4];
};

extern Memory  memories[MAX_MEMORIES];
extern uint8_t memCount;
extern uint8_t currentMemory;     // selected via MIDI CC 22/27/28

void  initMemories();
bool  saveMemories();
bool  loadMemories();
int   addMemory(const char *name);
void  delMemory(uint8_t id);
void  loadMemory(uint8_t id);   // apply 4 triggers + all CCs

#endif