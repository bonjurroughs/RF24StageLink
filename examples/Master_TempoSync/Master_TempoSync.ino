/*
  RF24StageLink - Master_TempoSync

  Same as Master_UsbMidi (reads live USB-MIDI and broadcasts it, frequency-hopping the
  radio), but with BEAT SYNC turned on for the animation library.

  HOW THE CLOCK STAYS OFF THE AIR
    Relaying 24-ppqn MIDI clock is ~48 messages/sec at 120 BPM - it would flood the
    radio. Instead we call link.setAbsorbClock(true): the master swallows the clock and
    transport (0xF8/0xFA/0xFB/0xFC), measures the tempo locally, and rides the BPM +
    beat phase in the hop beacon it already sends ~20x/sec. Fixtures rebuild a smooth
    local clock from those beacons (a tiny PLL in SLAnimator) and animate in lockstep.
    NO dedicated clock packets. Just make sure your DAW sends MIDI Clock to this port.

    NOTE: with absorb on, fixtures get tempo via onSync - the onClock/onStart callbacks
    no longer fire (the master never transmits raw clock). Note/CC/etc. are unaffected.

  *** BOARD REQUIREMENT *** native USB (32U4 / SAMD / Due) - see Master_UsbMidi.
  LIBRARIES (Library Manager): RF24, MIDIUSB, RF24StageLink.
  nRF24 WIRING (32U4):  CE -> D9   CSN -> D10  (SPI on the hardware pins).
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>
#include <MIDIUSB.h>

RF24 radio(9, 10);                 // CE pin, CSN pin
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

void setup() {
  // ---- optional configuration: call BEFORE beginMaster() (must match every fixture) ----
  // link.setChannels(channels, sizeof(channels)); // hop set        (default: 16,22,28,34,40)
  // link.setRadioAddress(address);                // shared address (default: C1 5A 9E 10 01)
  // link.setHopInterval(50);                       // ms per channel (default: 50)
  link.setDataRate(RF24_250KBPS);              // RF24_1MBPS | RF24_2MBPS | RF24_250KBPS (default; best range)
  link.setPALevel(RF24_PA_MAX);                  // RF24_PA_MIN | _LOW | _HIGH | _MAX (default)
  link.setRedundancy(2);                         // resend each MIDI msg N times (default: 2)

  // ---- BEAT SYNC: derive tempo from MIDI clock and ride it in the beacon ----
  link.setAbsorbClock(true);                     // <- the whole point of this example

  if (DEBUG) {
    Serial.begin(115200);
    link.setDebugStream(Serial);                 // prints "[RF] master ch=.. tx=.. bpm=.. play=.." @1Hz
  }

  link.beginMaster();                              // REQUIRED
}

void loop() {
  // Forward every incoming USB-MIDI message. Clock/transport are absorbed inside
  // sendMidi() (see setAbsorbClock above); notes, CC, program, bend go on the air.
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0) link.sendMidi(rx.byte1, rx.byte2, rx.byte3);
  } while (rx.header != 0);

  link.update();                                   // REQUIRED: hop schedule + tempo beacon
}
