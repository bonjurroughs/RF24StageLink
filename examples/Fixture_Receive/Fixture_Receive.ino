/*
  RF24StageLink - Fixture_Receive

  A listening fixture that prints the live MIDI stream it receives, using the
  library's built-in TYPED callbacks - so you subscribe to decoded events
  (NoteOn, ControlChange, ...) instead of hand-decoding raw bytes. Swap the bodies
  of the handlers below for your own behavior (lights, relays, etc.).

  Like the other examples it's an OPTION MENU: only the essentials are active; the
  rest is commented (with defaults) so every option is obvious at a glance.

  Works on any board. Uses the default channel list + address, so it pairs with an
  unmodified Master_UsbMidi out of the box.

  nRF24 WIRING:  CE -> D9   CSN -> D10   MOSI/MISO/SCK on hardware SPI
    (Uno/Nano: D11/D12/D13.   Leonardo/Micro: the ICSP header.)

  LIBRARIES (Library Manager): RF24, RF24StageLink.
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>

RF24 radio(9, 10);                 // CE pin, CSN pin
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

// Optional custom hop set / address (must match EVERY node).
// Keep every channel <= ch ~54 (the band the modules actually transmit on).
// static const uint8_t channels[] = { 16, 22, 28, 34, 40, 46 }; // max 16, all <= 54
// static const uint8_t address[5] = { 0xC1, 0x5A, 0x9E, 0x10, 0x01 };

// ---- typed MIDI handlers (channel is 1..16) ----  put YOUR behavior in these
void onNoteOn (uint8_t ch, uint8_t note, uint8_t vel) { if (DEBUG) { Serial.print(F("NoteOn    ch=")); Serial.print(ch); Serial.print(F(" note=")); Serial.print(note); Serial.print(F(" vel=")); Serial.println(vel); } }
void onNoteOff(uint8_t ch, uint8_t note, uint8_t vel) { if (DEBUG) { Serial.print(F("NoteOff   ch=")); Serial.print(ch); Serial.print(F(" note=")); Serial.print(note); Serial.print(F(" vel=")); Serial.println(vel); } }
void onCC     (uint8_t ch, uint8_t cc,   uint8_t val) { if (DEBUG) { Serial.print(F("CC        ch=")); Serial.print(ch); Serial.print(F(" cc="));   Serial.print(cc);   Serial.print(F(" val=")); Serial.println(val); } }
void onProgram(uint8_t ch, uint8_t prog)              { if (DEBUG) { Serial.print(F("Program   ch=")); Serial.print(ch); Serial.print(F(" prog=")); Serial.println(prog); } }
void onBend   (uint8_t ch, int16_t bend)              { if (DEBUG) { Serial.print(F("PitchBend ch=")); Serial.print(ch); Serial.print(F(" bend=")); Serial.println(bend); } }
void onStart()    { if (DEBUG) Serial.println(F("Start")); }
void onStop()     { if (DEBUG) Serial.println(F("Stop")); }
void onContinue() { if (DEBUG) Serial.println(F("Continue")); }
// void onClock()  { /* fires 24x per beat - noisy; uncomment registration below to use */ }

// Raw catch-all (every message that passes the channel filter, incl. SysEx/MTC/
// poly-aftertouch/channel-pressure that the typed callbacks don't cover):
// void onRaw(uint8_t b0, uint8_t b1, uint8_t b2) { /* ... */ }

void setup() {
  if (DEBUG) Serial.begin(115200);

  // ---- subscribe to the MIDI you care about ----
  link.onNoteOn(onNoteOn);
  link.onNoteOff(onNoteOff);
  link.onControlChange(onCC);
  link.onProgramChange(onProgram);
  link.onPitchBend(onBend);
  link.onStart(onStart);
  link.onStop(onStop);
  link.onContinue(onContinue);
  // link.onClock(onClock);                         // 24 ticks per beat (floods Serial)
  // link.onMidi(onRaw);                            // raw catch-all / escape hatch

  // ---- MIDI channel filter (default: all channels) ----
  // link.listenChannel(1);                         // react to MIDI channel 1 only
  // link.setChannelMask(0b0000000000000011);       // or any subset: bit0=ch1, bit1=ch2, ...
  // link.listenAllChannels();                      // back to all (default)

  // ---- optional RF24StageLink config: BEFORE beginFixture(); must match every node ----
  // link.setChannels(channels, sizeof(channels)); // hop set        (default: 5 in-band channels 16,22,28,34,40)
  // link.setRadioAddress(address);                // shared address (default: C1 5A 9E 10 01)
  // link.setHopInterval(50);                       // ms per channel (default: 50)
  // link.setDataRate(RF24_250KBPS);                // RF24_1MBPS | RF24_2MBPS | RF24_250KBPS (default)
  // link.setPALevel(RF24_PA_MAX);                  // RF24_PA_MIN | _LOW | _HIGH | _MAX (default)
  // link.setRedundancy(2);                         // (master TX only; no effect on a fixture)

  // ---- optional diagnostics ----
  // link.setDebugStream(Serial);                   // "[RF] fixture synced=.. ch=.. pkt=.. rx=.." @1Hz
  // link.disableDebug();

  link.beginFixture();                             // REQUIRED
}

void loop() {
  link.update();                                   // REQUIRED: track hops + dispatch MIDI

  // Status you can read any time:
  // link.isSynced();                              // true once locked onto the master
  // link.currentChannel();                        // current hop channel (0..125)
}
