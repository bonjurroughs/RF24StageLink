/*
  RF24StageLink - Fixture_Animator

  A full animation fixture: MIDI Note On/Off (received over the radio) selects one of
  up to 127 animation slots, velocity drives intensity, Control Change knobs tweak
  parameters live, and everything beat-syncs to the master's tempo WITHOUT any MIDI
  clock on the air (it rides the hop beacon - see Master_TempoSync).

  WHAT TO PLAY
    Notes 60..64 are mapped below. Play note 60 -> beat-pulse, 61 -> palette wash,
    62 -> chase, 63 -> tempo strobe (turn CC17 up to enable it), 64 -> solid color.
    Release everything -> the idle breathe. Map any note 0..126 to any animation.

  KNOBS (Control Change)
    CC1  -> palette speed     CC2  -> chase trail
    CC16 -> strobe rate       CC17 -> strobe enable (0 = off)

  ZONES
    Set LISTEN_MIDI_CHANNEL to this fixture's channel (1..16) so stage-left and
    stage-right rigs can run different notes on different channels. 0 = listen to all.

  BOARDS / WIRING: see Fixture_FastLED (radio on SPI + CE/CSN, strip DATA on D7).
  LIBRARIES (Library Manager): RF24, FastLED, RF24StageLink.
*/

#define FASTLED_ALLOW_INTERRUPTS 1   // MUST be before <FastLED.h>: keeps millis()/radio timing alive on AVR
#include <FastLED.h>
#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>
#include <SLAnimator.h>
#include <SLAnimations.h>

// ---- LED strip configuration (FastLED) ----
#define LED_CHIPSET   WS2812
#define LED_DATA_PIN  7
#define LED_ORDER     GRB
#define NUM_LEDS      30             // keep modest on AVR (3 bytes/LED)
#define BRIGHTNESS    255
#define FRAMES_PER_SECOND  60
#define LISTEN_MIDI_CHANNEL 1        // this fixture's zone (1..16); 0 = listen to ALL channels

RF24 radio(9, 10);                   // CE pin, CSN pin
RF24StageLink link(radio);
SLAnimator anim;
CRGB leds[NUM_LEDS];

const bool DEBUG = true;

// ---- forward the radio callbacks into the animator (plain free functions: no captures) ----
void onNoteOn (uint8_t ch, uint8_t note, uint8_t vel) { (void)ch; anim.noteOn(note, vel); }
void onNoteOff(uint8_t ch, uint8_t note, uint8_t vel) { (void)ch; (void)vel; anim.noteOff(note); }
void onCC     (uint8_t ch, uint8_t cc,   uint8_t val) { (void)ch; anim.controlChange(cc, val); }
void onSync   (uint16_t bpm88, uint16_t phase15, bool playing) { anim.onSync(bpm88, phase15, playing); }

void setup() {
  // ---- FastLED ----
  FastLED.addLeds<LED_CHIPSET, LED_DATA_PIN, LED_ORDER>(leds, NUM_LEDS);
  FastLED.setDither(0);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setCorrection(TypicalLEDStrip);

  // ---- animation engine ----
  anim.begin(leds, NUM_LEDS);
  anim.setAnimation(60, slBeatPulse);     // play note 60 -> pulse on the beat
  anim.setAnimation(61, slPaletteCycle);  // 61 -> palette wash   (CC1 = speed)
  anim.setAnimation(62, slChase);         // 62 -> chasing dot    (CC2 = trail)
  anim.setAnimation(63, slStrobeCC);      // 63 -> tempo strobe   (CC17 enable, CC16 rate)
  anim.setAnimation(64, slSolidVelocity); // 64 -> solid color
  anim.setIdleAnimation(slIdleBreath);    // nothing held -> breathe

  // ---- radio config (MUST match the master and every fixture) ----
  link.setDataRate(RF24_250KBPS);
  link.setPALevel(RF24_PA_MAX);

  if (DEBUG) {
    Serial.begin(115200);
    link.setDebugStream(Serial);          // "[RF] fixture synced=.. ch=.. pkt=.. rx=.." @1Hz
  }

  // ---- wire callbacks + zone, then start ----
  link.onNoteOn(onNoteOn);  link.onNoteOff(onNoteOff);
  link.onControlChange(onCC);
  link.onSync(onSync);                    // <- tempo/phase from the beacon -> the PLL
  link.listenChannel(LISTEN_MIDI_CHANNEL);
  link.beginFixture();                    // REQUIRED
}

void loop() {
  link.update();                          // REQUIRED: hops + dispatch MIDI + tempo (keep hot)
  anim.renderAt(FRAMES_PER_SECOND);       // rate-limited render + FastLED.show() (won't starve the radio)
}
