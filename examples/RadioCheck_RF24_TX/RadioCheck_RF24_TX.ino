/*
  RadioCheck_RF24_TX   (DIAGNOSTIC - bare RF24, does NOT use RF24StageLink)

  Sends an incrementing number on channel 76 with auto-ACK on. Pair with
  RadioCheck_RF24_RX on the other board. This bypasses the hopping/sync layer
  entirely to answer one question: do these two radios talk at all?

  Open Serial @115200 on THIS (TX) board:
    "sent N  ACK"   -> the RX received it AND acked  => radios + wiring + power are
                       100% fine; the problem is in the RF24StageLink hopping layer
                       (tell me and I'll fix the library).
    "sent N  no-ack"-> the RX is not receiving => hardware level: SPI/CE/CSN wiring,
                       3.3V power + 10uF cap, address, or the modules themselves.

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
  radio.setChannel(76);                  // fixed, in-band, known-good
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.openWritingPipe(address);
  radio.stopListening();
  if (DEBUG) Serial.println(F("RadioCheck TX: sending on channel 76..."));
}

uint32_t counter = 0;
void loop() {
  bool ok = radio.write(&counter, sizeof(counter));   // blocks until ACK or retries exhausted
  if (DEBUG) {
    Serial.print(F("sent ")); Serial.print(counter);
    Serial.println(ok ? F("  ACK") : F("  no-ack"));
  }
  counter++;
  delay(250);
}
