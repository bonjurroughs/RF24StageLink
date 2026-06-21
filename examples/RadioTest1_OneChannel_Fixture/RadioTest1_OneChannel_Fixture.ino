/*
  RF24StageLink - RadioTest1_OneChannel_Fixture   (DIAGNOSTIC - upload as-is)

  Pair of RadioTest1_OneChannel_Master. Single fixed channel (22), no hopping,
  no MIDI, no LEDs - just the radio link + sync status.

  Open Serial @115200. You want:
      [RF] sync acquired ch=22
      [RF] fixture synced=1 ch=22 pkt=N rx=0   <-- synced=1 should HOLD; pkt keeps climbing
  (rx stays 0 here - this test sends no MIDI, only sync beacons; pkt counts beacons.)

  If synced=1 holds  -> radios/power are good; full hopping is the next check -
                        default hop set. Run RadioTest2_HopSet_* next.
  If it keeps flapping (sync lost/acquired) on ONE channel -> power/wiring:
                        10uF cap across nRF24 VCC/GND, solid 3.3V, modules ~1 m apart.

  WIRING: CE -> D9   CSN -> D10   MOSI/MISO/SCK on hardware SPI.
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>

RF24 radio(9, 10);
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

static const uint8_t TEST_CHANNELS[] = { 22 };   // must match the master

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
