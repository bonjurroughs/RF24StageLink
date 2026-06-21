/*
  SLAnimations.h - a starter pack of animations for SLAnimator.

  Each is a plain free function `void name(SLAnimContext&)`. Register them on slots:
    anim.setAnimation(60, slBeatPulse);
  then trigger by playing the matching MIDI note. They show the three building blocks:
    - note  -> color (ctx.note)         - velocity -> brightness (ctx.intensity)
    - tempo -> motion (ctx.bpm88 + ctx.timebase, via FastLED beat functions)
    - CC    -> live parameters (ctx.cc[...])
  Copy one and tweak it to build your own. Beat-driven ones fall back to 120 BPM when
  no tempo is synced yet (ctx.effectiveBpm88).

  Copyright (C) 2026 bonjurroughs.  MIT License (see LICENSE).
*/
#ifndef RF24STAGELINK_SLANIMATIONS_H
#define RF24STAGELINK_SLANIMATIONS_H

#include "SLAnimator.h"

// Solid color: note -> hue, velocity -> brightness. No tempo needed (good demo baseline).
inline void slSolidVelocity(SLAnimContext& ctx) {
  uint8_t hue = (ctx.note == SL_NO_NOTE) ? 0 : (uint8_t)(ctx.note * 2);
  fill_solid(ctx.leds, ctx.numLeds, CHSV(hue, 255, ctx.intensity));
}

// Palette wash that scrolls one cycle per beat; CC1 multiplies the speed (1..8x).
inline void slPaletteCycle(SLAnimContext& ctx) {
  uint16_t bpm    = ctx.effectiveBpm88(120 << 8);
  uint8_t  mult   = (uint8_t)(1 + (ctx.cc[1] >> 4));                 // CC1 -> 1..8x speed
  uint8_t  start  = (uint8_t)((beat88(bpm, ctx.timebase) >> 8) * mult);
  uint8_t  spread = (uint8_t)(256 / (ctx.numLeds ? ctx.numLeds : 1)); // palette across the strip
  for (uint16_t i = 0; i < ctx.numLeds; ++i)
    ctx.leds[i] = ColorFromPalette(PartyColors_p,
                                   (uint8_t)(start + (uint8_t)(i * spread)),
                                   ctx.intensity, LINEARBLEND);
}

// THE phase-lock demo: whole fixture pulses brightness in time with the beat.
// Two fixtures running this from one DAW peak together, every beat.
inline void slBeatPulse(SLAnimContext& ctx) {
  uint16_t bpm    = ctx.effectiveBpm88(120 << 8);
  uint8_t  wave   = (uint8_t)beatsin88(bpm, 40, 255, ctx.timebase);  // 40..255, one cycle/beat
  uint8_t  bright = scale8(wave, ctx.intensity);                     // velocity scales the pulse
  uint8_t  hue    = (ctx.note == SL_NO_NOTE) ? 0 : (uint8_t)(ctx.note * 2);
  fill_solid(ctx.leds, ctx.numLeds, CHSV(hue, 255, bright));
}

// Tempo-locked strobe, gated/served by CC: CC17 > 0 enables; CC16 sets flashes-per-beat (1..16).
inline void slStrobeCC(SLAnimContext& ctx) {
  if (ctx.cc[17] == 0) { fill_solid(ctx.leds, ctx.numLeds, CRGB::Black); return; }
  uint16_t bpm   = ctx.effectiveBpm88(120 << 8);
  uint8_t  mult  = (uint8_t)(1 + (ctx.cc[16] >> 3));                 // CC16 -> 1..16 flashes/beat
  uint8_t  phase = (uint8_t)((beat88(bpm, ctx.timebase) >> 8) * mult);
  bool     on    = phase < 128;                                     // 50% duty
  uint8_t  hue   = (ctx.note == SL_NO_NOTE) ? 0 : (uint8_t)(ctx.note * 2);
  CRGB c = on ? CRGB(CHSV(hue, 255, ctx.intensity)) : CRGB(CRGB::Black);
  fill_solid(ctx.leds, ctx.numLeds, c);
}

// A dot that runs the strip once per beat with a fading trail; CC2 sets the trail length.
inline void slChase(SLAnimContext& ctx) {
  uint16_t bpm  = ctx.effectiveBpm88(120 << 8);
  uint8_t  fade = ctx.cc[2] ? ctx.cc[2] : 40;                        // CC2 -> trail (higher = shorter)
  fadeToBlackBy(ctx.leds, ctx.numLeds, fade);
  uint16_t pos16 = beat88(bpm, ctx.timebase);                        // 0..65535 per beat
  uint16_t idx   = (uint16_t)(((uint32_t)pos16 * ctx.numLeds) >> 16);
  if (idx < ctx.numLeds) {
    uint8_t hue = (ctx.note == SL_NO_NOTE) ? 0 : (uint8_t)(ctx.note * 2);
    ctx.leds[idx] += CHSV(hue, 255, ctx.intensity);
  }
}

// Calm idle: a slow blue breathe at a fixed rate, independent of sync. Use for setIdleAnimation().
inline void slIdleBreath(SLAnimContext& ctx) {
  uint8_t b = (uint8_t)beatsin88(30 << 8, 4, 48, ctx.timebase);      // ~30 "BPM" breathe
  fill_solid(ctx.leds, ctx.numLeds, CHSV(160, 200, b));
}

#endif // RF24STAGELINK_SLANIMATIONS_H
