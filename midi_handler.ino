// Phase E — Memory CC dispatch — forward decls (memory.ino compiles after
// midi_handler.ino alphabetically; these symbols need prototypes here).
extern uint8_t memCount;
extern uint8_t currentMemory;
extern void    loadMemory(uint8_t id);

inline void MidiInit() {
  
#ifdef MIDI_VIA_SERIAL
  MIDI_PORT.begin(115200);
#endif
#ifdef MIDI_VIA_SERIAL2
  pinMode( MIDIRX_PIN , INPUT_PULLDOWN);
  pinMode( MIDITX_PIN , OUTPUT);
  Serial2.begin( 31250, SERIAL_8N1, MIDIRX_PIN, MIDITX_PIN ); // midi port
#endif

#ifdef MIDI_VIA_SERIAL
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleCC);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleProgramChange(handleProgramChange);
  MIDI.begin(MIDI_CHANNEL_OMNI);
#endif
#ifdef MIDI_VIA_SERIAL2
  MIDI2.setHandleNoteOn(handleNoteOn);
  MIDI2.setHandleNoteOff(handleNoteOff);
  MIDI2.setHandleControlChange(handleCC);
  MIDI2.setHandlePitchBend(handlePitchBend);
  MIDI2.setHandleProgramChange(handleProgramChange);
  MIDI2.begin(MIDI_CHANNEL_OMNI);
#endif

}


// Phase 3 — F5 : compteur de NoteOn — ecrit en lecture par midi (Core 1),
// copie dans le snapshot par regular_checks(). Volatile : un uint32_t aligne
// s'ecrit/lit de facon atomique sur le port de l'ESP32.
volatile uint32_t midi_note_count = 0;

void handleNoteOn(uint8_t inChannel, uint8_t inNote, uint8_t inVelocity) {
#ifdef DEBUG_MIDI
  DEB("MIDI note on ");
  DEBUG(inNote);
#endif
  midi_note_count++;
  if (inChannel == DRUM_MIDI_CHAN )         {Drums.NoteOn(inNote, inVelocity);}
  else if (inChannel == SYNTH1_MIDI_CHAN )  {Synth1.on_midi_noteON(inNote, inVelocity);}
  else if (inChannel == SYNTH2_MIDI_CHAN )  {Synth2.on_midi_noteON(inNote, inVelocity);}
}

void handleNoteOff(uint8_t inChannel, uint8_t inNote, uint8_t inVelocity) {
  if (inChannel == DRUM_MIDI_CHAN )         {Drums.NoteOff(inNote);}
  else if (inChannel == SYNTH1_MIDI_CHAN )  {Synth1.on_midi_noteOFF(inNote, inVelocity);}
  else if (inChannel == SYNTH2_MIDI_CHAN )  {Synth2.on_midi_noteOFF(inNote, inVelocity);}

}

inline void handleCC(uint8_t inChannel, uint8_t cc_number, uint8_t cc_value) {
  switch (cc_number) { // global parameters yet set via ANY channel CCs
    case CC_ANY_COMPRESSOR:
      Comp.SetRatio(3.0f + cc_value * 0.307081f);
      DEBF("Set Comp Ratio %d\r\n", cc_value);
      break;
    case CC_ANY_DELAY_TIME:
      Delay.SetLength(cc_value * MIDI_NORM);
      break;
    case CC_ANY_DELAY_FB:
      Delay.SetFeedback(cc_value * MIDI_NORM);
      break;
    case CC_ANY_DELAY_LVL:
      Delay.SetLevel(cc_value * MIDI_NORM);
      break;
    case CC_ANY_RESET_CCS:
    case CC_ANY_NOTES_OFF:
    case CC_ANY_SOUND_OFF:
        if (inChannel == SYNTH1_MIDI_CHAN && millis()-last_reset>1000 ) {
#ifdef JUKEBOX
          do_midi_stop();
#endif
          Synth1.allNotesOff();
          Synth2.allNotesOff();
          last_reset = millis();
        }
      break;
#ifndef NO_PSRAM
    case CC_ANY_REVERB_TIME:
      Reverb.SetTime(cc_value * MIDI_NORM);
      break;
    case CC_ANY_REVERB_LVL:
      Reverb.SetLevel(cc_value * MIDI_NORM);
      break;
#endif
    // Phase E — Memory selection via CC (defined in memory.ino)
    case 22:  // CC_MEMORY_SELECT
      if (cc_value < memCount) loadMemory(cc_value);
      break;
    case 27:  // CC_MEMORY_PREV
      if (cc_value > 64 && currentMemory > 0) loadMemory(--currentMemory);
      break;
    case 28:  // CC_MEMORY_NEXT
      if (cc_value > 64 && currentMemory < memCount - 1) loadMemory(++currentMemory);
      break;
    default:
      if (inChannel == DRUM_MIDI_CHAN )         {Drums.ParseCC(cc_number, cc_value);}
      else if (inChannel == SYNTH1_MIDI_CHAN )  {Synth1.ParseCC(cc_number, cc_value);}
      else if (inChannel == SYNTH2_MIDI_CHAN )  {Synth2.ParseCC(cc_number, cc_value);}
  }
}

void handleProgramChange(uint8_t inChannel, uint8_t number) {
  if (inChannel == DRUM_MIDI_CHAN) {     Drums.SetProgram(number);  }
}

inline void handlePitchBend(uint8_t inChannel, int number) {
  if (inChannel == DRUM_MIDI_CHAN )         {Drums.PitchBend(number);}
  else if (inChannel == SYNTH1_MIDI_CHAN )  {Synth1.PitchBend(number);}
  else if (inChannel == SYNTH2_MIDI_CHAN )  {Synth2.PitchBend(number);}
}
