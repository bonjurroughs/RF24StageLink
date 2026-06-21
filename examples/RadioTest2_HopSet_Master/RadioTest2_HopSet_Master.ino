/*
  RF24StageLink - RadioTest2_HopSet_Master   (DIAGNOSTIC - upload as-is)

  Hops the library's in-band default channel set (all <= ch 54, the band the
  modules actually transmit on) and broadcasts sync beacons - no MIDI. Pair with
  RadioTest2_HopSet_Fixture.

  PURPOSE - confirm the corrected hop set holds sync while hopping. Run this AFTER
  RadioTest1 holds on a single channel. If the fixture holds "synced=1" while ch=
  keeps changing, the full frequency-hopping link is healthy and the real examples
  (which use this same default set) will work.

  WIRING: CE -> D9   CSN -> D10   MOSI/MISO/SCK on hardware SPI.
  Watch Serial @115200 on the FIXTURE board.
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>

RF24 radio(9, 10);
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

// Same as the library default - all in the modules' good band (2416-2440 MHz).
static const uint8_t TEST_CHANNELS[] = { 16, 22, 28, 34, 40 };

void setup() {
  if (DEBUG) Serial.begin(115200);
  link.setChannels(TEST_CHANNELS, sizeof(TEST_CHANNELS));
  link.setDataRate(RF24_250KBPS);
  link.setPALevel(RF24_PA_MAX);
  if (DEBUG) link.setDebugStream(Serial);
  link.beginMaster();
}

void loop() {
  link.update();    // hops the set above, beaconing on each channel
}
