/*
  RF24StageLink - RadioTest2_HopSet_Fixture   (DIAGNOSTIC - upload as-is)

  Pair of RadioTest2_HopSet_Master. Hops the same in-band default channel set.
  No MIDI, no LEDs - just the radio link + sync status.

  Open Serial @115200. You want "synced=1" to HOLD while ch= keeps changing
  (it's hopping). If it holds here, full frequency hopping works and the normal
  examples (Master_UsbMidi / Fixture_*) - which use this same default - will too.

  WIRING: CE -> D9   CSN -> D10   MOSI/MISO/SCK on hardware SPI.
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>

RF24 radio(9, 10);
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

// Must match the master (and the library default).
static const uint8_t TEST_CHANNELS[] = { 16, 22, 28, 34, 40 };

void setup() {
  if (DEBUG) Serial.begin(115200);
  link.setChannels(TEST_CHANNELS, sizeof(TEST_CHANNELS));
  link.setDataRate(RF24_250KBPS);
  link.setPALevel(RF24_PA_MAX);
  if (DEBUG) link.setDebugStream(Serial);
  link.beginFixture();
}

void loop() {
  link.update();
}
