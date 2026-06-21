/*
  RF24StageLink - Fixture_FastLED

  A listening fixture wired up for FastLED. This is a STARTING SKELETON: setup()
  configures the strip and runs a quick R/G/B self-test, and the radio link is
  fully wired, but the loop() does NOT animate anything yet - add your own LED code.

  Like the other examples it's an OPTION MENU: only the essentials are active; the
  rest is commented (with defaults / alternatives) so every RF24StageLink and
  FastLED setup option is obvious at a glance.

  BOARDS
    Any board FastLED supports. On AVR (Uno/Leonardo/Micro) FastLED.show() briefly
    disables interrupts (~30us/LED), so we set FASTLED_ALLOW_INTERRUPTS 1 (below,
    BEFORE including FastLED) and keep NUM_LEDS modest (3 bytes/LED). For long
    strips use an ESP32 (FastLED there uses non-blocking RMT/I2S).

  WIRING
    Radio : CE -> D9   CSN -> D10   SPI on hardware pins
            (Uno/Nano: D11/D12/D13.  Leonardo/Micro: the ICSP header.)
    Strip : DATA -> D7 (WS2812 is one data pin, no SPI -> no conflict with the radio).
            (matches LED_DATA_PIN below - change both together if you move it.)
            Power the strip from 5V with a common ground; ~330R in series on DATA
            and a 1000uF cap across the strip's 5V/GND are recommended.

  LIBRARIES (Library Manager): RF24, FastLED, RF24StageLink.
*/

#define FASTLED_ALLOW_INTERRUPTS 1   // MUST be before <FastLED.h>: keeps millis()/radio timing alive on AVR
#include <FastLED.h>
#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>

// ---- LED strip configuration (FastLED) ----
#define LED_CHIPSET   WS2812         // clockless, 1 data pin. These take LED_ORDER: WS2812 | WS2812B | SK6812 | WS2811
                                     // (WS2812 == NeoPixel hardware. The bare NEOPIXEL alias is GRB-only and takes NO
                                     //  order - if you use it, switch to the addLeds<NEOPIXEL, LED_DATA_PIN> line below.)
#define LED_DATA_PIN  7              // strip DATA pin (keep off the radio's SPI pins)
//#define LED_CLK_PIN   4
#define LED_ORDER     GRB            // WS2812 is usually GRB (APA102 etc. use BGR)
#define NUM_LEDS      1             // keep modest on AVR
#define BRIGHTNESS    255             // master brightness 0..255
#define FRAMES_PER_SECOND  60
#define LISTEN_MIDI_CHANNEL 0   // this fixture's MIDI channel (1..16). Set 0 to listen to ALL channels.

RF24 radio(9, 10);                   // CE pin, CSN pin
RF24StageLink link(radio);
CRGB leds[NUM_LEDS];

const bool DEBUG = true;             // ONE switch: set false to silence ALL Serial output here

// Optional custom hop set / address (must match EVERY node).
// Keep every channel <= ch ~54 (the band the modules actually transmit on).
// static const uint8_t channels[] = { 16, 22, 28, 34, 40, 46 }; // max 16, all <= 54
// static const uint8_t address[5] = { 0xC1, 0x5A, 0x9E, 0x10, 0x01 };

// Two ways to receive MIDI - pick whichever suits your animation:

// (A) RAW catch-all: b0 = status (type<<4 | channel), b1 = data1, b2 = data2.
// void onMidi(uint8_t b0, uint8_t b1, uint8_t b2) {
//  (void)b0; (void)b1; (void)b2;          // ---- ADD YOUR LED HANDLING HERE ----
// }

// (B) TYPED handlers: decoded, channel is 1..16. Define the ones you want and
//     register them in setup(). Examples (uncomment + fill in):

uint8_t gHue = 0;     // last note -> color
uint8_t gSat = 255;     // saturation
uint8_t gBright = 0;  // last velocity -> brightness

/* light up */
void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  gHue    = note * 2;    // note 0..127 -> hue 0..254
  gBright = vel * 2;     // velocity 0..127 -> 0..254
  if (DEBUG) {           // <-- if you DON'T see this when playing, no notes are arriving
    Serial.print(F("NOTE ON  ch=")); Serial.print(ch);
    Serial.print(F(" note="));        Serial.print(note);
    Serial.print(F(" vel="));         Serial.println(vel);
  }
}
void onNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
  gBright = 0;           // note released -> off
  if (DEBUG) { Serial.print(F("NOTE OFF ch=")); Serial.print(ch); Serial.print(F(" note=")); Serial.println(note); }
}

// void onCC     (uint8_t ch, uint8_t cc,   uint8_t val) { /* brightness/hue/speed */ }
// void onProgram(uint8_t ch, uint8_t prog)              { /* pick a scene/pattern  */ }
// void onBend   (uint8_t ch, int16_t bend)              { /* -8192..+8191 */ }
// void onClock()  { /* 24x per beat - count for tempo-synced effects */ }
// void onStart()  { /* reset to beat 0 */ }

// Boot self-test: flash the strip R, G, B, then off. If you DON'T see this, the
// problem is the strip wiring / data pin / chipset / power - not the radio.
void ledSelfTest() {
  const CRGB seq[3] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (uint8_t i = 0; i < 3; i++) { fill_solid(leds, NUM_LEDS, seq[i]); FastLED.show(); delay(300); }
  fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show();
}

void setup() {
  // ---- FastLED setup ----
  FastLED.addLeds<LED_CHIPSET, LED_DATA_PIN, LED_ORDER>(leds, NUM_LEDS);
  // If you use the bare NEOPIXEL alias instead, it takes NO color order - use this form:
  // FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);
  // Clocked strip (APA102 / SK9822 / DOTSTAR) - data + clock pins, keep them OFF the radio SPI:
  // FastLED.addLeds<LED_CHIPSET, LED_DATA_PIN, LED_CLOCK_PIN, LED_ORDER>(leds, NUM_LEDS);
  FastLED.setDither(0);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);              // color correction
  // FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);      // brownout protection for big strips
  ledSelfTest();                                          // remove if you don't want the R/G/B flash

  // ---- optional RF24StageLink configuration: call BEFORE beginFixture() ----
  // Radio settings (setChannels/Address/HopInterval/DataRate/PALevel) MUST match
  // the master and every other fixture. Defaults are shown in the comments.
  // link.setChannels(channels, sizeof(channels)); // hop set        (default: 5 in-band channels 16,22,28,34,40)
  // link.setRadioAddress(address);                // shared address (default: C1 5A 9E 10 01)
  // link.setHopInterval(50);                       // ms per channel (default: 50)
  link.setDataRate(RF24_250KBPS);              // RF24_1MBPS | RF24_2MBPS | RF24_250KBPS (default; best range)
  link.setPALevel(RF24_PA_MAX);                  // RF24_PA_MIN | _LOW | _HIGH | _MAX (default)
  //link.setRedundancy(1);                         // (master TX only; has no effect on a fixture)

  // ---- optional diagnostics (radio/sync status) ----
  if (DEBUG) {
    Serial.begin(115200);                        // needed for the debug stream below
    link.setDebugStream(Serial);                 // prints "[RF] fixture synced=.. ch=.. pkt=.. rx=.." @1Hz
  }

  // ---- receive MIDI: raw catch-all, and/or typed handlers (register what you use) ----
  // link.onMidi(onMidi);                             // (A) raw
  link.onNoteOn(onNoteOn);  link.onNoteOff(onNoteOff);  // (B) typed (define above)
  // link.onControlChange(onCC);  link.onProgramChange(onProgram);
  // link.onPitchBend(onBend);
  // link.onClock(onClock);  link.onStart(onStart);  link.onStop(onStop);  link.onContinue(onContinue);

  // ---- MIDI channel filter ----
  link.listenChannel(LISTEN_MIDI_CHANNEL);       // react ONLY to this fixture's channel (0 = all)
  // link.setChannelMask(0b0000000000000011);     // or a custom subset: bit0=ch1, bit1=ch2, ...

  link.beginFixture();                             // REQUIRED
}

void loop() {
  link.update();                                   // REQUIRED: track hops + dispatch MIDI (keep this hot)

  // Status you can read any time:
  // link.isSynced();                              // true once locked onto the master
  // link.currentChannel();                        // current hop channel (0..125)

  // ---- your LED animation goes here ----
  // IMPORTANT: rate-limit FastLED.show(). Calling it every loop iteration hammers
  // the CPU and starves the radio (the fixture loses sync). ~60 FPS is plenty.
  // (Do NOT use FastLED.delay() - it BLOCKS the whole frame and starves the radio.)
  static uint32_t lastFrame = 0;
  if (millis() - lastFrame >= (1000UL / FRAMES_PER_SECOND)) {
    lastFrame = millis();
    fill_solid(leds, NUM_LEDS, CHSV(gHue, gSat, gBright));  // build a frame from the globals
    FastLED.show();                                          // push it to the strip
  }
}
