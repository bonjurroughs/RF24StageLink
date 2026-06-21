/*
  RadioCheck_RF24_RX   (DIAGNOSTIC - bare RF24, does NOT use RF24StageLink)

  Listens on channel 76 and prints each number it receives from RadioCheck_RF24_TX.
  Auto-ACK is on (default), so receiving also acks the sender.

  Open Serial @115200 on THIS (RX) board:
    "got N" lines climbing  -> radios + wiring + power are fine; the issue is the
                               RF24StageLink hopping layer.
    nothing                 -> radios aren't linking at all (hardware: SPI/CE/CSN,
                               3.3V + 10uF cap, address, or the modules).

  Put the two boards ~0.3-1 m apart (not touching).
  WIRING: CE -> D9   CSN -> D10   MOSI/MISO/SCK on hardware SPI.
  LIBRARIES: RF24 only.
*/

#include <SPI.h>
#include <RF24.h>

RF24 radio(9, 10);                       // CE, CSN
const uint8_t address[5] = { 0xC1, 0x5A, 0x9E, 0x10, 0x01 };

const bool DEBUG = true;                 // ONE switch: set false to silence ALL Serial output here

void setup() {
  if (DEBUG) Serial.begin(115200);
  radio.begin();
  radio.setChannel(76);                  // must match the TX
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.openReadingPipe(1, address);
  radio.startListening();
  if (DEBUG) Serial.println(F("RadioCheck RX: listening on channel 76..."));
}

void loop() {
  if (radio.available()) {
    uint32_t value = 0;
    radio.read(&value, sizeof(value));
    if (DEBUG) { Serial.print(F("got ")); Serial.println(value); }
  }
}
