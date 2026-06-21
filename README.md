# RF24StageLink

**Frequency-hopping USB-MIDI broadcast over nRF24L01(+).**

One **master** reads live USB-MIDI and broadcasts it to many **fixtures**. The master and all fixtures hop through
the same channel list in lockstep (FHSS) so the link rides out 2.4 GHz interference. It's a simple one-to-many
broadcast (no ACK) — every fixture receives the whole MIDI stream and hands each message to your code via a callback.

Layered on the [RF24](https://github.com/nRF24/RF24) library by composition — it never modifies RF24. Cross-platform;
all timing is a non-blocking `update()` pump (no AVR timers/ISRs).

> 📖 **Full documentation lives in the [Wiki](https://github.com/bonjurroughs/RF24StageLink/wiki).** New here? Start with
> **[Getting Started](https://github.com/bonjurroughs/RF24StageLink/wiki/Getting-Started)** and
> **[Hardware & Wiring](https://github.com/bonjurroughs/RF24StageLink/wiki/Hardware-and-Wiring)**. For the light-show side see
> **[Animation Engine](https://github.com/bonjurroughs/RF24StageLink/wiki/Animation-Engine)** and
> **[Tempo & Beat Sync](https://github.com/bonjurroughs/RF24StageLink/wiki/Tempo-and-Beat-Sync)**.

## How it works

The master hops every `hopInterval` ms and sends a tiny **beacon** on each new channel carrying the hop position;
MIDI packets carry it too. A fixture adopts the hop position from any packet it hears, tracks the schedule on its
own timer (re-aligning on each beacon), and — if it stops hearing the master — scans the channel list until it
re-acquires. No clock sync, no tempo, no per-fixture addressing. MIDI is sent the instant it arrives on the current
channel, so latency stays low and a jammed channel only costs one dwell.

## Install

1. Install **RF24** (Library Manager).
2. Copy this `RF24StageLink` folder into `Arduino/libraries/`.
3. For the master example, also install **MIDIUSB** by *Arduino*. (Fixtures need only RF24.)
4. Examples appear under **File → Examples → RF24StageLink**.

## Quick start

**Master** (USB-MIDI → air; needs a 32U4/SAMD native-USB board):
```cpp
#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>
#include <MIDIUSB.h>
RF24 radio(9, 10);
RF24StageLink link(radio);

void setup() { link.beginMaster(); }
void loop() {
  midiEventPacket_t rx;
  do { rx = MidiUSB.read(); if (rx.header) link.sendMidi(rx.byte1, rx.byte2, rx.byte3); } while (rx.header);
  link.update();
}
```

**Fixture** (air → your code; any board):
```cpp
#include <SPI.h>
#include <RF24.h>
#include <RF24StageLink.h>
RF24 radio(9, 10);
RF24StageLink link(radio);

void onMidi(uint8_t b0, uint8_t b1, uint8_t b2) { /* your handling here */ }

void setup() { link.onMidi(onMidi); link.beginFixture(); }
void loop()  { link.update(); }
```

## Examples

Each example is a verbose "option menu" — only the essentials are active, everything else is commented with its
default so the full API is visible at a glance.

| Example | Role | Notes |
|---------|------|-------|
| `Master_UsbMidi`  | Master  | USB-MIDI in → broadcast. Needs a native-USB board (32U4/SAMD) + the MIDIUSB library. |
| `Master_TempoSync`| Master  | Same, plus `setAbsorbClock(true)`: derives tempo from the DAW clock and rides it in the beacon for beat-synced animations. |
| `Fixture_Receive` | Fixture | Prints every received MIDI message, decoded. The minimal "is it working" fixture. |
| `Fixture_FastLED` | Fixture | Bare WS2812 + FastLED skeleton — notes pick color/brightness. The starting point for custom LED code. Needs FastLED. |
| `Fixture_Animator`| Fixture | Full animation engine: notes select animations, CC tweaks them live, all beat-synced. Needs FastLED. |

## Configuration (all optional, before `begin*`)

| Call | Default | Notes |
|------|---------|-------|
| `setChannels(arr, n)` | `{16, 22, 28, 34, 40}` (all <= ch 54, the band the modules actually transmit on) | the FHSS hop set (max 16); master + fixtures must match. Keep channels <= ~54 |
| `setHopInterval(ms)`  | `50` | dwell per channel |
| `setDataRate(dr)`     | `RF24_250KBPS` | best range/robustness |
| `setPALevel(lvl)`     | `RF24_PA_MAX` | lower it if a bare module browns out |
| `setRedundancy(n)`    | `2` | times each MIDI message is resent (no ACK, so repeats add robustness). Fixtures dedup by sequence number, so callbacks still fire once per message |
| `setRadioAddress(a5)` | `C1 5A 9E 10 01` | shared 5-byte broadcast address |
| `setReliable(b)`      | `false` | point-to-point ACK mode — see below. **ONE fixture only**; must match on both ends |

Unmodified master + fixture sketches pair automatically (matching defaults).

## Reliable point-to-point mode (one fixture)

By default the link is **broadcast** (no ACK) so one master feeds *many* fixtures. If
you have **exactly one** fixture and want *guaranteed* delivery, call `setReliable(true)`
on **both** the master and the fixture (before `begin*`):

- MIDI is sent with **auto-ACK + hardware retries** — the radio re-sends each message
  until the fixture confirms it (within the hop dwell), and `sendMidi()` **returns
  `true`/`false`** so the master knows if it got through.
- Beacons are still broadcast (NO_ACK) — frequency hopping works exactly as before.
- Set `setRedundancy(1)` — hardware retries replace the rebroadcast.

**Do not use reliable mode with more than one fixture.** nRF24 auto-ACK expects exactly
one receiver to ACK each packet; multiple fixtures collide on the ACK and the link stalls.
For a multi-fixture rig, leave `setReliable` at its default (broadcast). See the
`Master_UsbMidi_Reliable` / `Fixture_Reliable` example pair.

## MIDI events & filtering (fixture)

Instead of decoding raw bytes, subscribe to **typed events** — the library parses the stream for you. Channels are
reported **1–16**.

```cpp
void noteOn(uint8_t ch, uint8_t note, uint8_t vel) { /* light up */ }

void setup() {
  link.onNoteOn(noteOn);
  link.listenChannel(1);     // react to MIDI channel 1 only
  link.beginFixture();
}
```

| Callback | Fires on | Args |
|----------|----------|------|
| `onNoteOn` / `onNoteOff` | Note On / Off (Note On vel 0 → `onNoteOff`) | `(ch, note, velocity)` |
| `onControlChange` | CC | `(ch, controller, value)` |
| `onProgramChange` | Program Change | `(ch, program)` |
| `onPitchBend` | Pitch Bend | `(ch, bend)` — decoded 14-bit, centered −8192…+8191 |
| `onClock`/`onStart`/`onStop`/`onContinue` | System Real-Time | `()` |
| `onMidi` | **everything** (raw catch-all, incl. SysEx/MTC/aftertouch) | `(b0, b1, b2)` |

**Channel filter:** `listenChannel(n)`, `setChannelMask(mask)` (bit 0 = ch 1), `listenAllChannels()` (default). It
applies to channel-voice messages for both the typed callbacks and raw `onMidi`; System Real-Time/Common always pass.

**Master TX filter:** the master drops **Active Sensing (0xFE)** by default to save airtime
(`setDropActiveSensing(false)` to keep it); `setDropMtcQuarterFrame(true)` also drops 0xF1. Clock/notes/CC always sent.

## Animations & beat sync (FastLED)

`SLAnimator` is an optional fixture-side engine that turns the MIDI stream into a light show.
It needs **FastLED**; the core transport does not, so masters and non-LED fixtures stay FastLED-free.

- **127 animation slots** — MIDI **Note On** selects an animation (slot = note number, 0–126); newest
  note wins, releasing it falls back to the previously-held note, then to an idle animation. **Velocity**
  drives intensity.
- **CC → live parameters** — every Control Change lands in a 128-entry table animations read for knob tweaking.
- **Zones** — use the existing `listenChannel(n)` filter so different fixtures react to different channels.

```cpp
#include <FastLED.h>
#include <RF24StageLink.h>
#include <SLAnimator.h>
#include <SLAnimations.h>
SLAnimator anim;  CRGB leds[NUM_LEDS];

void onNoteOn (uint8_t ch,uint8_t n,uint8_t v){ anim.noteOn(n,v); }
void onNoteOff(uint8_t ch,uint8_t n,uint8_t v){ anim.noteOff(n); }
void onCC     (uint8_t ch,uint8_t c,uint8_t v){ anim.controlChange(c,v); }
void onSync   (uint16_t bpm88,uint16_t ph,bool pl){ anim.onSync(bpm88,ph,pl); }

void setup() {
  FastLED.addLeds<WS2812, 7, GRB>(leds, NUM_LEDS);
  anim.begin(leds, NUM_LEDS);
  anim.setAnimation(60, slBeatPulse);     // note 60 -> a beat-synced pulse
  anim.setIdleAnimation(slIdleBreath);
  link.onNoteOn(onNoteOn); link.onNoteOff(onNoteOff);
  link.onControlChange(onCC); link.onSync(onSync);
  link.beginFixture();
}
void loop() { link.update(); anim.renderAt(60); }
```

### Beat sync without clock spam

Relaying 24-ppqn MIDI clock would flood the radio (~48 msgs/sec at 120 BPM). Instead, call
`link.setAbsorbClock(true)` on the **master**: it swallows the clock/transport, measures the tempo,
and rides **BPM + beat phase in the hop beacon it already sends ~20×/sec**. Each fixture feeds those
sparse anchors through a tiny software **PLL** (inside `SLAnimator::onSync`) that reconstructs a smooth
`timebase` for FastLED's `beat88()`/`beatsin88()` — so every fixture animates in lockstep with **zero
dedicated clock packets**. The PLL slews rather than snaps, so jitter and tempo changes don't stutter
the LEDs. (With absorb on, `onClock`/`onStart` no longer fire — tempo arrives via `onSync` instead.)

Writing an animation is just a function — read color from the note, motion from the synced beat, and
parameters from CC:

```cpp
void myPulse(SLAnimContext& ctx) {
  uint16_t bpm = ctx.effectiveBpm88(120 << 8);                  // synced tempo (or 120 fallback)
  uint8_t b = beatsin88(bpm, 40, 255, ctx.timebase);            // on-beat brightness, phase-locked
  fill_solid(ctx.leds, ctx.numLeds, CHSV(ctx.note * 2, 255, scale8(b, ctx.intensity)));
}
```

Starter animations in `SLAnimations.h`: `slBeatPulse`, `slPaletteCycle` (CC1 = speed), `slChase`
(CC2 = trail), `slStrobeCC` (CC17 enable / CC16 rate), `slSolidVelocity`, `slIdleBreath`.

## Optional diagnostics

Radio/sync status is a **library feature you opt into** — it's not baked into the examples. Call
`link.setDebugStream(Serial)` (after `Serial.begin(115200)`) on either role to get a once-a-second status line plus
sync-change events:

```
[RF] fixture synced=1 ch=80 rx=42     // fixture: locked, on channel 80, 42 MIDI msgs received
[RF] master ch=74 tx=128              // master: on channel 74, 128 MIDI msgs broadcast
[RF] sync acquired ch=80              // printed when a fixture locks/loses the master
```

This prints only radio status — your sketch still decides what (if anything) to print for the MIDI itself. Call
`link.disableDebug()` to turn it off.

## Notes

- **Wiring:** CE=D9, CSN=D10, with the radio on hardware SPI (Uno/Nano D11/12/13; Leonardo/Micro the ICSP header).
  Power the module from 3.3 V and add a ~10 µF cap across its VCC/GND — flaky nRF24 links are usually power.
- **SysEx** (multi-chunk) is not forwarded by this simple version; 1–3 byte channel-voice and realtime messages
  (notes, CC, PB, PC, clock/start/stop) are.
- **License:** RF24StageLink is MIT; firmware built against RF24 (GPL-2.0-only) is GPL-2.0-only as a combined work.
