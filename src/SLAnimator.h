/*
  SLAnimator.h - FastLED animation engine for RF24StageLink fixtures.

  A single-active "scene selector": MIDI Note On selects one of up to 127 animation
  slots (slot == note number, 0..126); newest note wins, and releasing it falls back
  to the previously-held note (or idle). Velocity drives intensity. Control Change
  values land in a 128-entry table animations can read for live knob tweaking.

  TEMPO/BEAT SYNC WITHOUT SPAMMING THE AIR
    The master never relays 24-ppqn MIDI clock. It rides the tempo + beat phase in the
    hop beacon (~20/sec, free). onSync() feeds those sparse anchors into a tiny software
    PLL that reconstructs a smooth `timebase` such that FastLED's beat88()/beatsin88()
    reproduce the master's musical phase - so every fixture animates in lockstep. The
    PLL slews (never snaps) so jitter and tempo changes don't make the LEDs stutter.

  USAGE (fixture sketch): include AFTER <FastLED.h>. Own the CRGB strip yourself.
    SLAnimator anim;
    anim.begin(leds, NUM_LEDS);
    anim.setAnimation(60, slBeatPulse);     // note 60 -> this animation
    anim.setIdleAnimation(slIdleBreath);
    // wire RF24StageLink callbacks to anim.noteOn/noteOff/controlChange/onSync
    // loop(): link.update(); anim.renderAt(60);

  Copyright (C) 2026 bonjurroughs.  MIT License (see LICENSE).
*/
#ifndef RF24STAGELINK_SLANIMATOR_H
#define RF24STAGELINK_SLANIMATOR_H

#include <FastLED.h>

static const uint8_t SL_NO_NOTE  = 0xFF;  // "no animation active" sentinel
static const uint8_t SL_MAX_HELD = 16;    // held-note stack depth (newest-wins)
static const uint8_t SL_NUM_SLOTS= 127;   // notes 0..126 map to slots

// Everything an animation needs for one frame. `bpm88`/`timebase` are PLL-synced:
// pass them straight to FastLED beat functions, e.g. beatsin88(ctx.bpm88, lo, hi, ctx.timebase).
struct SLAnimContext {
  CRGB*    leds;
  uint16_t numLeds;
  uint8_t  note;       // active slot 0..126 (== triggering note), or SL_NO_NOTE
  uint8_t  velocity;   // 0..127 of the active note
  uint8_t  intensity;  // velocity scaled to 0..255 (brightness convenience)
  uint16_t bpm88;      // synced tempo, Q8.8 (0 = no tempo yet)
  uint32_t timebase;   // synced beat timebase for beat88/beatsin88
  bool     playing;    // transport running
  uint32_t nowMs;      // millis() captured for this frame
  const uint8_t* cc;   // 128-entry Control Change table (cc[controller] = last value)

  // Use the synced tempo if we have one, else fall back (e.g. ctx.effectiveBpm88(120<<8)).
  uint16_t effectiveBpm88(uint16_t fallback88) const { return bpm88 ? bpm88 : fallback88; }
};

typedef void (*SLAnimFn)(SLAnimContext& ctx);   // plain function pointer (no captures)

class SLAnimator {
public:
  SLAnimator()
    : _leds(0), _numLeds(0), _idle(0),
      _heldCount(0), _activeNote(SL_NO_NOTE), _activeVel(0),
      _bpm88(0), _timebase(0), _playing(false), _wasPlaying(false), _haveLock(false),
      _lastFrameMs(0)
  {
    for (uint16_t i = 0; i < SL_NUM_SLOTS; ++i) _slots[i] = 0;
    for (uint16_t i = 0; i < 128; ++i) _cc[i] = 0;
  }

  void begin(CRGB* leds, uint16_t numLeds) { _leds = leds; _numLeds = numLeds; }

  // ---- registration ----
  void setAnimation(uint8_t slot, SLAnimFn fn) { if (slot < SL_NUM_SLOTS) _slots[slot] = fn; }
  void setIdleAnimation(SLAnimFn fn)           { _idle = fn; }

  // ---- MIDI wiring (forward your RF24StageLink callbacks here) ----
  void noteOn(uint8_t note, uint8_t velocity) {
    if (note >= SL_NUM_SLOTS) return;
    removeHeld(note);                          // already held? lift it so it returns to the top
    if (_heldCount >= SL_MAX_HELD) {           // full -> drop the oldest
      for (uint8_t i = 1; i < _heldCount; ++i) { _heldNotes[i-1] = _heldNotes[i]; _heldVel[i-1] = _heldVel[i]; }
      _heldCount--;
    }
    _heldNotes[_heldCount] = note; _heldVel[_heldCount] = velocity; _heldCount++;
    _activeNote = note; _activeVel = velocity;
  }
  void noteOff(uint8_t note) {
    removeHeld(note);
    if (_heldCount) { _activeNote = _heldNotes[_heldCount-1]; _activeVel = _heldVel[_heldCount-1]; }
    else            { _activeNote = SL_NO_NOTE;               _activeVel = 0; }
  }
  void controlChange(uint8_t cc, uint8_t value) { _cc[cc & 0x7F] = value; }

  // ---- beat sync (forward RF24StageLink::onSync here) ----
  void onSync(uint16_t bpm88, uint16_t phase15, bool playing) {
    _playing = playing;
    if (bpm88 == 0) { _bpm88 = 0; return; }        // no tempo: animations use their fallback
    uint32_t now = millis();
    uint16_t phase16 = (uint16_t)(phase15 << 1);    // restore the LSB we dropped on the wire

    // Invert FastLED's beat88: find the timebase T where
    //   ((now - T) * bpm88 * 280) >> 16 == phase16   (mod one beat).
    // ms per beat (FastLED's 280 model): 2^32 / (280*bpm88) ~= 15339169/bpm88.
    uint32_t beatLenMs = 15339169UL / bpm88;
    uint32_t offsetMs  = (((uint32_t)phase16 << 16) / ((uint32_t)bpm88 * 280UL));
    uint32_t target    = now - offsetMs;

    bool resumed = playing && !_wasPlaying;
    _wasPlaying = playing;
    if (!_haveLock || resumed) {                    // first lock / transport resume: snap, no ramp
      _timebase = target; _haveLock = true; _bpm88 = bpm88; return;
    }
    _bpm88 = bpm88;                                 // tempo comes straight from the (already EMA'd) master
    if (!playing) return;                           // stopped: hold the phase, don't drift

    // Slew toward the target, wrapping the error into +/- half a beat so we always take
    // the SHORT way around. Bounded step => jitter nulls in a few anchors, big errors ramp.
    int32_t err  = (int32_t)(target - _timebase);
    int32_t half = (int32_t)(beatLenMs >> 1);
    while (err >  half) err -= (int32_t)beatLenMs;
    while (err < -half) err += (int32_t)beatLenMs;
    int32_t step = err / 4;
    if (step >  4) step =  4;
    if (step < -4) step = -4;
    _timebase = (uint32_t)((int32_t)_timebase + step);
  }

  // ---- rendering ----
  // render() runs the active animation into your buffer but does NOT push to the strip.
  void render() {
    SLAnimContext ctx;
    ctx.leds = _leds; ctx.numLeds = _numLeds;
    ctx.note = _activeNote;
    ctx.velocity = (_activeNote == SL_NO_NOTE) ? 0 : _activeVel;
    uint16_t i2 = (uint16_t)ctx.velocity * 2;       // 0..127 -> 0..254
    ctx.intensity = i2 > 255 ? 255 : (uint8_t)i2;
    ctx.bpm88 = _bpm88; ctx.timebase = _timebase; ctx.playing = _playing;
    ctx.nowMs = millis(); ctx.cc = _cc;

    SLAnimFn fn = (_activeNote != SL_NO_NOTE && _slots[_activeNote]) ? _slots[_activeNote] : _idle;
    if (fn) fn(ctx);
    else if (_leds) fill_solid(_leds, _numLeds, CRGB::Black);
  }

  // renderAt() rate-limits render()+FastLED.show() so it never starves the radio.
  // Returns true on the frames it actually drew. Call it every loop().
  bool renderAt(uint8_t fps) {
    uint32_t now = millis();
    uint16_t period = fps ? (uint16_t)(1000U / fps) : 0;
    if ((uint32_t)(now - _lastFrameMs) < period) return false;
    _lastFrameMs = now;
    render();
    FastLED.show();
    return true;
  }

  // ---- status ----
  uint8_t  activeNote() const { return _activeNote; }
  uint16_t bpm88()      const { return _bpm88; }
  uint32_t timebase()   const { return _timebase; }
  bool     playing()    const { return _playing; }
  bool     haveLock()   const { return _haveLock; }

private:
  void removeHeld(uint8_t note) {
    for (uint8_t i = 0; i < _heldCount; ++i) {
      if (_heldNotes[i] == note) {
        for (uint8_t j = i + 1; j < _heldCount; ++j) { _heldNotes[j-1] = _heldNotes[j]; _heldVel[j-1] = _heldVel[j]; }
        _heldCount--;
        return;
      }
    }
  }

  CRGB*    _leds;
  uint16_t _numLeds;
  SLAnimFn _slots[SL_NUM_SLOTS];
  SLAnimFn _idle;
  uint8_t  _cc[128];

  uint8_t  _heldNotes[SL_MAX_HELD];
  uint8_t  _heldVel[SL_MAX_HELD];
  uint8_t  _heldCount;
  uint8_t  _activeNote;
  uint8_t  _activeVel;

  // PLL state
  uint16_t _bpm88;
  uint32_t _timebase;
  bool     _playing;
  bool     _wasPlaying;
  bool     _haveLock;

  uint32_t _lastFrameMs;
};

#endif // RF24STAGELINK_SLANIMATOR_H
