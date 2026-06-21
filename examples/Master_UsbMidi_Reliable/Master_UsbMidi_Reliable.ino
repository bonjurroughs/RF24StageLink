/*
  RF24StageLink - Master_UsbMidi_Reliable   (POINT-TO-POINT / ONE fixture)

  Same as Master_UsbMidi, but RELIABLE mode: every MIDI message is sent with
  auto-ACK + hardware retries, so it is delivered guaranteed (within the hop dwell)
  and link.sendMidi() tells you whether the fixture got it. Still frequency-hops to
  ride out 2.4GHz interference.

  *** ONE FIXTURE ONLY ***
  Reliable mode is point-to-point. nRF24 auto-ACK expects exactly ONE receiver to
  ack each packet. With two or more fixtures their ACKs collide and the link stalls.
  For a multi-fixture rig use the plain Master_UsbMidi (broadcast) instead.
  The fixture MUST also run in reliable mode: Fixture_Reliable (setReliable(true)).

  *** BOARD REQUIREMENT ***
  Arduino MIDIUSB needs NATIVE USB: Leonardo, Micro, Pro Micro (ATmega32U4), Zero,
  MKR, Nano 33 (SAMD), or Due. It will NOT run on an Uno/Nano (no native USB).

  LIBRARIES (Library Manager): RF24, MIDIUSB, RF24StageLink.

  nRF24 WIRING (32U4):  CE -> D9   CSN -> D10
    SPI MOSI/MISO/SCK on the board's hardware-SPI pins:
      Leonardo/Micro : the ICSP header.   Pro Micro : MOSI=D16 MISO=D14 SCK=D15.
    VCC -> 3.3V, GND -> GND (a 10uF cap across the radio's VCC/GND is recommended).
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>
#include <MIDIUSB.h>

RF24 radio(9, 10);                 // CE pin, CSN pin
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

// Custom hop set / address, if you want them (must match on the fixture too).
// Keep every channel in the band the modules actually transmit on: <= ch ~54.
// static const uint8_t channels[] = { 16, 22, 28, 34, 40, 46 }; // max 16, all <= 54
// static const uint8_t address[5] = { 0xC1, 0x5A, 0x9E, 0x10, 0x01 };

void setup() {
  // ---- optional configuration: call BEFORE beginMaster() ----
  // Radio settings (setChannels/Address/HopInterval/DataRate/PALevel/Reliable) MUST
  // match on the master and the fixture. Defaults are shown in the comments.
  link.setReliable(true);                        // <-- ACK + retries (ONE fixture; set on BOTH ends)
  // link.setChannels(channels, sizeof(channels)); // hop set        (default: 5 in-band channels 16,22,28,34,40)
  // link.setRadioAddress(address);                // shared address (default: C1 5A 9E 10 01)
  // link.setHopInterval(50);                       // ms per channel (default: 50)
  link.setDataRate(RF24_250KBPS);              // RF24_1MBPS | RF24_2MBPS | RF24_250KBPS (default; best range)
  link.setPALevel(RF24_PA_MAX);                  // RF24_PA_MIN | _LOW | _HIGH | _MAX (default)
  link.setRedundancy(1);                         // reliable mode: hardware retries replace rebroadcast; keep 1

  // ---- TX noise filter: keep junk off the air ----
  // link.setDropActiveSensing(true);               // drop 0xFE Active Sensing (DEFAULT: true)
  // link.setDropMtcQuarterFrame(true);             // drop 0xF1 MTC quarter-frame (default: false)

  // ---- optional diagnostics (radio/sync status, NOT MIDI) ----
  if (DEBUG) {
    Serial.begin(115200);
    link.setDebugStream(Serial);                 // prints "[RF] master ch=.. tx=.." once a second
  }

  link.beginMaster();                              // REQUIRED
}

void loop() {
  // Forward every incoming USB-MIDI message over the air. In reliable mode
  // sendMidi() returns false if the fixture did NOT ack it (out of range / off).
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0) {
      bool delivered = link.sendMidi(rx.byte1, rx.byte2, rx.byte3);
      if (DEBUG && !delivered) Serial.println(F("MISSED (fixture did not ACK)"));
    }
  } while (rx.header != 0);

  // You can also send a message yourself:
  // link.sendMidi(0x90, 60, 100);                 // Note On, ch1, note 60, velocity 100

  link.update();                                   // REQUIRED: runs the frequency-hop schedule

  // Status you can read any time:
  // link.currentChannel();                        // current hop channel (0..125)
}
