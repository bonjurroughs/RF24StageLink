/*
  RF24StageLink - RadioTest1_OneChannel_Master   (DIAGNOSTIC - upload as-is)

  Pins the radio to a SINGLE clean channel (22 = 2422 MHz, in-band) and just
  broadcasts sync beacons - no MIDI, no hopping. Pair this with
  RadioTest1_OneChannel_Fixture on the other board.

  PURPOSE - isolate radio/power from interference:
    * If the FIXTURE holds "synced=1" steady on this one fixed channel, your radios
      and power are FINE -> the real-world flapping is Wi-Fi interference across the
      hop set. Next run RadioTest2_HopSet_* .
    * If it STILL flaps here, it's power/wiring: add a 10uF cap across the nRF24
      VCC/GND, give it a solid 3.3V, keep the modules ~1 m apart.

  WIRING: CE -> D9   CSN -> D10   MOSI/MISO/SCK on hardware SPI.
  Watch Serial @115200 on the FIXTURE board.
*/

#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>

RF24 radio(9, 10);
RF24StageLink link(radio);

const bool DEBUG = true;           // ONE switch: set false to silence ALL Serial output here

static const uint8_t TEST_CHANNELS[] = { 22 };   // one channel = no hopping

void setup() {
  if (DEBUG) Serial.begin(115200);
  link.setChannels(TEST_CHANNELS, sizeof(TEST_CHANNELS));
  link.setDataRate(RF24_250KBPS);                 // most robust rate
  link.setPALevel(RF24_PA_MAX);                   // max power for the test
  if (DEBUG) link.setDebugStream(Serial);         // "[RF] master ch=22 tx=0"
  link.beginMaster();
}

void loop() {
  link.update();    // broadcasts a beacon every ~50 ms (stays on channel 22)
}
